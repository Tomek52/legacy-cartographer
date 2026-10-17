#include "claims/claim_parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The JSON Schema is implemented by hand on nlohmann_json: one fixed schema
// does not justify a JSON Schema library (B-25). Exceptions stay enabled
// in nlohmann_json, so this file only calls functions that cannot throw
// for any input: sax_parse() with a handler that does not throw, parse()
// with allow_exceptions = false, and get_ptr(), which returns nullptr on a
// type mismatch instead of throwing.

namespace lcart::claims {

namespace {

using common::Error;
using common::ErrorCode;
using common::Result;
using Json = nlohmann::json;

// Limits of the claim JSON Schema (docs/specs/claim-schema.md).
constexpr std::size_t max_claims = 500;
constexpr std::size_t max_name_length = 512;
constexpr std::size_t max_file_length = 1024;
constexpr std::size_t max_text_length = 2000;

/// Property names come from the input: a message shows at most this many
/// bytes of one, so that a problem line stays readable.
constexpr std::size_t max_shown_name_bytes = 64;

constexpr std::array<std::pair<std::string_view, ClaimKind>, 5> claim_kinds{{
    {"calls", ClaimKind::calls},
    {"includes", ClaimKind::includes},
    {"depends_on", ClaimKind::depends_on},
    {"defines", ClaimKind::defines},
    {"belongs_to", ClaimKind::belongs_to},
}};

constexpr std::array<std::pair<std::string_view, EntityType>, 3> entity_types{{
    {"function", EntityType::function},
    {"file", EntityType::file},
    {"module", EntityType::module},
}};

constexpr std::array<std::pair<std::string_view, Polarity>, 2> polarities{{
    {"affirmed", Polarity::affirmed},
    {"negated", Polarity::negated},
}};

template <typename Enum, std::size_t Size>
[[nodiscard]] std::string_view
name_of(const std::array<std::pair<std::string_view, Enum>, Size>& names, Enum value) {
    for (const auto& [name, candidate] : names) {
        if (candidate == value) {
            return name;
        }
    }
    std::unreachable();
}

[[nodiscard]] bool is_utf8_continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

/// The length JSON Schema's minLength and maxLength count: characters, not
/// bytes. nlohmann_json has already rejected strings that are not UTF-8.
[[nodiscard]] std::size_t characters(std::string_view text) {
    std::size_t count = 0;
    for (const char byte : text) {
        if (!is_utf8_continuation(byte)) {
            ++count;
        }
    }
    return count;
}

/// A property name from the input as a JSON pointer reference token
/// (RFC 6901: `~` is `~0`, `/` is `~1`), with control characters escaped so
/// it cannot break the one-problem-per-line message (and `\` doubled, so an
/// escape cannot be mistaken for a key that spells one), and cut after
/// max_shown_name_bytes at a character boundary.
[[nodiscard]] std::string pointer_token(std::string_view name) {
    std::string token;
    for (std::size_t at = 0; at < name.size(); ++at) {
        const char c = name[at];
        if (at >= max_shown_name_bytes && !is_utf8_continuation(c)) {
            token += "...";
            break;
        }
        const auto byte = static_cast<unsigned char>(c);
        if (c == '~') {
            token += "~0";
        } else if (c == '/') {
            token += "~1";
        } else if (c == '\\') {
            token += "\\\\";
        } else if (byte < 0x20U || byte == 0x7FU) {
            constexpr std::string_view hex = "0123456789abcdef";
            token += "\\u00";
            token += hex[byte >> 4U];
            token += hex[byte & 0x0FU];
        } else {
            token += c;
        }
    }
    return token;
}

[[nodiscard]] std::string child(std::string_view pointer, std::string_view name) {
    return std::string{pointer} + '/' + pointer_token(name);
}

/// Every problem found, in the order the document is checked.
class Problems {
public:
    void add(std::string_view pointer, std::string_view description) {
        lines_.push_back((pointer.empty() ? std::string{R"("")"} : std::string{pointer}) + ": " +
                         std::string{description});
    }

    [[nodiscard]] bool empty() const { return lines_.empty(); }

    [[nodiscard]] Error error() const {
        std::string message;
        for (const std::string& line : lines_) {
            if (!message.empty()) {
                message += '\n';
            }
            message += line;
        }
        return Error{.code = ErrorCode::schema_violation, .message = std::move(message)};
    }

private:
    std::vector<std::string> lines_;
};

/// First pass, without building a document: finds syntax errors and
/// nesting deeper than max_claims_nesting_depth. nlohmann_json's parser
/// keeps its own nesting on the heap, and this handler stops it at the
/// first level too deep, so neither recursion nor memory depends on the
/// input's depth. The member functions are those of nlohmann::json_sax,
/// which sax_parse() checks at compile time; deriving from it is not
/// needed.
class NestingCheck {
public:
    explicit NestingCheck(std::size_t input_size) : input_size_(input_size) {}

    [[nodiscard]] const Problems& problems() const { return problems_; }

    bool null() { return value(); }
    bool boolean(bool /*val*/) { return value(); }
    bool number_integer(Json::number_integer_t /*val*/) { return value(); }
    bool number_unsigned(Json::number_unsigned_t /*val*/) { return value(); }
    bool number_float(Json::number_float_t /*val*/, const Json::string_t& /*s*/) { return value(); }
    bool string(Json::string_t& /*val*/) { return value(); }
    bool binary(Json::binary_t& /*val*/) { return value(); }
    bool start_object(std::size_t /*elements*/) { return start(false); }
    bool start_array(std::size_t /*elements*/) { return start(true); }

    bool key(Json::string_t& val) {
        levels_.back().key = val;
        return true;
    }

    bool end_object() { return end(); }
    bool end_array() { return end(); }

    bool parse_error(std::size_t position, const std::string& /*last_token*/,
                     const Json::exception& ex) {
        // The position only: nlohmann_json's message quotes the input. It
        // counts from 1, and reports the end of input as one past the last
        // byte. Its id 406 is a number too large for a double: valid JSON,
        // so not called a syntax error.
        constexpr int number_out_of_range_id = 406;
        if (ex.id == number_out_of_range_id) {
            problems_.add("", "number out of range at byte " + std::to_string(position));
            return false;
        }
        problems_.add("", position > input_size_
                              ? std::string{"not valid JSON (unexpected end of input)"}
                              : "not valid JSON (syntax error at byte " + std::to_string(position) +
                                    ")");
        return false;
    }

private:
    struct Level {
        bool is_array{};
        std::size_t next_index{};
        std::string key;
        std::string pointer;
    };

    /// The pointer of the value that starts now.
    [[nodiscard]] std::string next_pointer() {
        if (levels_.empty()) {
            return {};
        }
        Level& parent = levels_.back();
        if (parent.is_array) {
            return parent.pointer + '/' + std::to_string(parent.next_index++);
        }
        return child(parent.pointer, parent.key);
    }

    [[nodiscard]] bool value() {
        if (!levels_.empty() && levels_.back().is_array) {
            ++levels_.back().next_index;
        }
        return true;
    }

    [[nodiscard]] bool start(bool is_array) {
        std::string pointer = next_pointer();
        if (levels_.size() == max_claims_nesting_depth) {
            problems_.add(pointer, "nested deeper than " +
                                       std::to_string(max_claims_nesting_depth) +
                                       " levels, the most the claim schema has");
            return false;
        }
        levels_.push_back(
            Level{.is_array = is_array, .next_index = 0, .key = {}, .pointer = std::move(pointer)});
        return true;
    }

    [[nodiscard]] bool end() {
        levels_.pop_back();
        return true;
    }

    Problems problems_;
    std::size_t input_size_;
    std::vector<Level> levels_; // at most max_claims_nesting_depth
};

/// Second pass: the schema rules on the document, then the entity types
/// per kind and unique claim IDs.
class Validator {
public:
    [[nodiscard]] const Problems& problems() const { return problems_; }

    [[nodiscard]] std::vector<Claim> document(const Json& root) {
        std::vector<Claim> claims;
        const Json::object_t* object = object_at(root, "");
        if (object == nullptr) {
            return claims;
        }
        if (const Json* schema = required(*object, "", "schema")) {
            const std::string* id = schema->get_ptr<const std::string*>();
            if (id == nullptr || *id != claim_schema_id) {
                problems_.add("/schema", R"(must be ")" + std::string{claim_schema_id} + '"');
            }
        }
        if (const Json* items = required(*object, "", "claims")) {
            claims = claim_list(*items);
        }
        reject_unknown(*object, "", {"schema", "claims"});
        return claims;
    }

private:
    [[nodiscard]] std::vector<Claim> claim_list(const Json& value) {
        std::vector<Claim> claims;
        const Json::array_t* items = value.get_ptr<const Json::array_t*>();
        if (items == nullptr) {
            problems_.add("/claims", "must be an array");
            return claims;
        }
        if (items->size() > max_claims) {
            problems_.add("/claims", "more than " + std::to_string(max_claims) + " claims");
            return claims;
        }
        claims.reserve(items->size());
        for (std::size_t index = 0; index < items->size(); ++index) {
            claims.push_back(claim((*items)[index], "/claims/" + std::to_string(index)));
        }
        return claims;
    }

    [[nodiscard]] Claim claim(const Json& value, const std::string& pointer) {
        Claim claim;
        const Json::object_t* object = object_at(value, pointer);
        if (object == nullptr) {
            return claim;
        }
        if (const Json* id = required(*object, pointer, "id")) {
            claim_id(*id, pointer + "/id", claim);
        }
        std::optional<ClaimKind> kind;
        if (const Json* field = required(*object, pointer, "kind")) {
            kind = enum_at(*field, pointer + "/kind", claim_kinds);
            claim.kind = kind.value_or(ClaimKind::calls);
        }
        std::optional<EntityType> subject;
        if (const Json* field = required(*object, pointer, "subject")) {
            subject = entity(*field, pointer + "/subject", claim.subject);
        }
        std::optional<EntityType> object_type;
        if (const Json* field = required(*object, pointer, "object")) {
            object_type = entity(*field, pointer + "/object", claim.object);
        }
        if (const Json* field = required(*object, pointer, "polarity")) {
            claim.polarity =
                enum_at(*field, pointer + "/polarity", polarities).value_or(Polarity::affirmed);
        }
        if (const Json* field = required(*object, pointer, "text")) {
            claim.text = bounded_string(*field, pointer + "/text", max_text_length);
        }
        if (const auto source = object->find("source"); source != object->end()) {
            claim.source = claim_source(source->second, pointer + "/source");
        }
        reject_unknown(*object, pointer,
                       {"id", "kind", "subject", "object", "polarity", "text", "source"});
        if (kind) {
            check_entity_types(*kind, subject, object_type, pointer);
        }
        return claim;
    }

    void claim_id(const Json& value, const std::string& pointer, Claim& claim) {
        const std::string* id = string_at(value, pointer);
        if (id == nullptr) {
            return;
        }
        if (!matches_id_pattern(*id)) {
            problems_.add(pointer, "does not match ^c-[0-9]{4,6}$");
        } else if (const auto [first, inserted] = first_use_.try_emplace(*id, pointer); !inserted) {
            problems_.add(pointer, "duplicate claim ID, first used at " + first->second);
        }
        claim.id = *id;
    }

    /// `^c-[0-9]{4,6}$` with ECMA-262 semantics, as in JSON Schema: `$`
    /// matches only at the end, and `[0-9]` only ASCII digits.
    [[nodiscard]] static bool matches_id_pattern(std::string_view id) {
        constexpr std::string_view prefix = "c-";
        if (!id.starts_with(prefix)) {
            return false;
        }
        const std::string_view digits = id.substr(prefix.size());
        if (digits.size() < 4 || digits.size() > 6) {
            return false;
        }
        return std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
    }

    /// The entity at `value` into `entity`; its type if that is valid.
    [[nodiscard]] std::optional<EntityType> entity(const Json& value, const std::string& pointer,
                                                   EntityRef& entity) {
        const Json::object_t* object = object_at(value, pointer);
        if (object == nullptr) {
            return std::nullopt;
        }
        std::optional<EntityType> type;
        if (const Json* field = required(*object, pointer, "type")) {
            type = enum_at(*field, pointer + "/type", entity_types);
            entity.type = type.value_or(EntityType::function);
        }
        if (const Json* field = required(*object, pointer, "name")) {
            if (const std::string* name = field->get_ptr<const std::string*>();
                name != nullptr && name->empty()) {
                problems_.add(pointer + "/name", "must not be empty");
            } else {
                entity.name = bounded_string(*field, pointer + "/name", max_name_length);
            }
        }
        if (const auto file = object->find("file"); file != object->end()) {
            entity.file = bounded_string(file->second, pointer + "/file", max_file_length);
        }
        reject_unknown(*object, pointer, {"type", "name", "file"});
        return type;
    }

    [[nodiscard]] ClaimSource claim_source(const Json& value, const std::string& pointer) {
        ClaimSource source;
        const Json::object_t* object = object_at(value, pointer);
        if (object == nullptr) {
            return source;
        }
        // No maxLength in the schema: the input size limit bounds them.
        if (const auto document = object->find("document"); document != object->end()) {
            if (const std::string* text = string_at(document->second, pointer + "/document")) {
                source.document = *text;
            }
        }
        if (const auto section = object->find("section"); section != object->end()) {
            if (const std::string* text = string_at(section->second, pointer + "/section")) {
                source.section = *text;
            }
        }
        reject_unknown(*object, pointer, {"document", "section"});
        return source;
    }

    /// The table of docs/specs/claim-schema.md#claim-kinds-v1.
    [[nodiscard]] static bool subject_fits(ClaimKind kind, EntityType type) {
        switch (kind) {
        case ClaimKind::calls:
            return type == EntityType::function;
        case ClaimKind::includes:
        case ClaimKind::defines:
            return type == EntityType::file;
        case ClaimKind::depends_on:
            return type == EntityType::module;
        case ClaimKind::belongs_to:
            return type == EntityType::file || type == EntityType::function;
        }
        std::unreachable();
    }

    [[nodiscard]] static std::string_view subject_needed(ClaimKind kind) {
        switch (kind) {
        case ClaimKind::calls:
            return "a function";
        case ClaimKind::includes:
        case ClaimKind::defines:
            return "a file";
        case ClaimKind::depends_on:
            return "a module";
        case ClaimKind::belongs_to:
            return "a file or function";
        }
        std::unreachable();
    }

    [[nodiscard]] static EntityType object_needed(ClaimKind kind) {
        switch (kind) {
        case ClaimKind::calls:
        case ClaimKind::defines:
            return EntityType::function;
        case ClaimKind::includes:
            return EntityType::file;
        case ClaimKind::depends_on:
        case ClaimKind::belongs_to:
            return EntityType::module;
        }
        std::unreachable();
    }

    void check_entity_types(ClaimKind kind, std::optional<EntityType> subject,
                            std::optional<EntityType> object, const std::string& pointer) {
        const std::string needs = "a " + std::string{name_of(claim_kinds, kind)} + " claim needs ";
        if (subject && !subject_fits(kind, *subject)) {
            problems_.add(pointer + "/subject/type",
                          needs + std::string{subject_needed(kind)} + " here, not a " +
                              std::string{name_of(entity_types, *subject)});
        }
        if (object && *object != object_needed(kind)) {
            problems_.add(pointer + "/object/type",
                          needs + "a " + std::string{name_of(entity_types, object_needed(kind))} +
                              " here, not a " + std::string{name_of(entity_types, *object)});
        }
    }

    [[nodiscard]] const Json::object_t* object_at(const Json& value, std::string_view pointer) {
        const Json::object_t* object = value.get_ptr<const Json::object_t*>();
        if (object == nullptr) {
            problems_.add(pointer, "must be an object");
        }
        return object;
    }

    [[nodiscard]] const std::string* string_at(const Json& value, std::string_view pointer) {
        const std::string* text = value.get_ptr<const std::string*>();
        if (text == nullptr) {
            problems_.add(pointer, "must be a string");
        }
        return text;
    }

    /// The string at `value`, empty if it is none or too long.
    [[nodiscard]] std::string bounded_string(const Json& value, const std::string& pointer,
                                             std::size_t max_length) {
        const std::string* text = string_at(value, pointer);
        if (text == nullptr) {
            return {};
        }
        if (characters(*text) > max_length) {
            problems_.add(pointer, "longer than " + std::to_string(max_length) + " characters");
            return {};
        }
        return *text;
    }

    template <typename Enum, std::size_t Size>
    [[nodiscard]] std::optional<Enum>
    enum_at(const Json& value, const std::string& pointer,
            const std::array<std::pair<std::string_view, Enum>, Size>& names) {
        const std::string* text = string_at(value, pointer);
        if (text == nullptr) {
            return std::nullopt;
        }
        for (const auto& [name, enumerator] : names) {
            if (*text == name) {
                return enumerator;
            }
        }
        std::string allowed;
        for (const auto& [name, enumerator] : names) {
            allowed += allowed.empty() ? "" : ", ";
            allowed += name;
        }
        problems_.add(pointer, "not one of: " + allowed);
        return std::nullopt;
    }

    /// The property `name` of `object`; reports it at its own pointer if it
    /// is missing.
    [[nodiscard]] const Json* required(const Json::object_t& object, std::string_view pointer,
                                       std::string_view name) {
        const auto found = object.find(std::string{name});
        if (found == object.end()) {
            problems_.add(child(pointer, name), "required property missing");
            return nullptr;
        }
        return &found->second;
    }

    /// `additionalProperties: false`. The object is a std::map, so unknown
    /// names are reported in byte order, independent of the input's order.
    void reject_unknown(const Json::object_t& object, std::string_view pointer,
                        std::initializer_list<std::string_view> known) {
        for (const auto& [name, value] : object) {
            if (std::ranges::find(known, name) == known.end()) {
                problems_.add(child(pointer, name), "property not allowed by the claim schema");
            }
        }
    }

    Problems problems_;
    /// Claim ID -> pointer of its first use.
    std::map<std::string, std::string, std::less<>> first_use_;
};

} // namespace

Result<std::vector<Claim>> parse_claims(std::string_view json) {
    if (json.size() > max_claims_input_size) {
        Problems too_large;
        too_large.add("", "larger than " + std::to_string(max_claims_input_size / 1024 / 1024) +
                              " MiB (" + std::to_string(max_claims_input_size) + " bytes)");
        return std::unexpected(too_large.error());
    }
    NestingCheck nesting{json.size()};
    if (!Json::sax_parse(json.begin(), json.end(), &nesting)) {
        return std::unexpected(nesting.problems().error());
    }
    // Without exceptions: a document the first pass accepted parses here.
    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded()) {
        Problems not_json;
        not_json.add("", "not valid JSON");
        return std::unexpected(not_json.error());
    }
    Validator validator;
    std::vector<Claim> claims = validator.document(document);
    if (!validator.problems().empty()) {
        return std::unexpected(validator.problems().error());
    }
    return claims;
}

} // namespace lcart::claims
