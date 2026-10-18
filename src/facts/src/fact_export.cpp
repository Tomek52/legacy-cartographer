#include "facts/fact_export.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace lcart::facts {

namespace {

using common::Error;
using common::ErrorCode;
using common::Status;

constexpr std::string_view replacement_character = "\xEF\xBF\xBD";

void append_escaped(std::string& out, char character) {
    static constexpr std::string_view hex_digits = "0123456789abcdef";
    switch (character) {
    case '"':
        out += "\\\"";
        return;
    case '\\':
        out += "\\\\";
        return;
    case '\n':
        out += "\\n";
        return;
    case '\r':
        out += "\\r";
        return;
    case '\t':
        out += "\\t";
        return;
    case '\b':
        out += "\\b";
        return;
    case '\f':
        out += "\\f";
        return;
    default:
        break;
    }
    const auto code = static_cast<unsigned char>(character);
    constexpr unsigned char first_printable = 0x20;
    if (code < first_printable) {
        out += "\\u00";
        out += hex_digits[code >> 4U];
        out += hex_digits[code & 0x0FU];
        return;
    }
    out += character;
}

/// A JSON value written by hand, so key order and number format are fixed.
class JsonObject {
public:
    explicit JsonObject(std::string_view record) { text_ = "{\"record\":" + json_string(record); }

    JsonObject& string(std::string_view key, std::string_view value) {
        add_key(key);
        text_ += json_string(value);
        return *this;
    }
    JsonObject& optional_string(std::string_view key, const std::optional<std::string>& value) {
        add_key(key);
        text_ += value ? json_string(*value) : "null";
        return *this;
    }
    JsonObject& number(std::string_view key, std::int64_t value) {
        add_key(key);
        text_ += std::to_string(value);
        return *this;
    }
    JsonObject& optional_number(std::string_view key, std::optional<std::int64_t> value) {
        add_key(key);
        text_ += value ? std::to_string(*value) : "null";
        return *this;
    }
    JsonObject& boolean(std::string_view key, bool value) {
        add_key(key);
        text_ += value ? "true" : "false";
        return *this;
    }
    JsonObject& optional_boolean(std::string_view key, std::optional<bool> value) {
        add_key(key);
        if (value) {
            text_ += *value ? "true" : "false";
        } else {
            text_ += "null";
        }
        return *this;
    }

    /// The object as one line, newline included.
    [[nodiscard]] std::string line() const { return text_ + "}\n"; }

private:
    void add_key(std::string_view key) {
        text_ += ',';
        text_ += json_string(key);
        text_ += ':';
    }

    std::string text_;
};

const char* to_text(Linkage linkage) {
    switch (linkage) {
    case Linkage::internal:
        return "internal";
    case Linkage::external:
        return "external";
    }
    std::unreachable();
}

const char* to_text(CallKind kind) {
    switch (kind) {
    case CallKind::direct:
        return "direct";
    case CallKind::indirect:
        return "indirect";
    case CallKind::may_call:
        return "may_call";
    }
    std::unreachable();
}

const char* to_text(DependencyVia via) {
    switch (via) {
    case DependencyVia::include:
        return "include";
    case DependencyVia::call:
        return "call";
    }
    std::unreachable();
}

const char* to_text(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    case DiagnosticSeverity::fatal:
        return "fatal";
    }
    std::unreachable();
}

/// One line of the export and the key it is sorted by within its kind.
using Record = std::pair<std::vector<std::string>, std::string>;

void write_sorted(std::vector<Record> records, std::ostream& out) {
    std::ranges::sort(records);
    for (const Record& record : records) {
        out << record.second;
    }
}

/// A sort key part for a number: fixed width, so text order is numeric order.
std::string number_key(std::int64_t value) {
    constexpr std::size_t width = 20;
    const std::string text = std::to_string(value);
    return std::string(width - std::min(width, text.size()), '0') + text;
}

/// A sort key part for an optional text: null sorts before any text.
std::string optional_key(const std::optional<std::string>& value) {
    return value ? "1" + *value : std::string{"0"};
}

/// Everything the export reads, and the names it gives files, modules and
/// functions instead of row ids.
struct Snapshot {
    AnalysisCoverage coverage;
    std::vector<ModuleFact> modules;
    std::vector<FileFact> files;
    std::vector<FunctionFact> functions;
    std::vector<IncludeFact> includes;
    std::vector<CallFact> calls;
    std::vector<CallFact> may_calls;
    std::vector<DiagnosticFact> diagnostics;
    std::vector<ModuleDependencyFact> dependencies;
    std::map<FileId, ExportedFile> file_names;
    std::map<std::string, std::string> file_names_by_stored_path;
    std::map<ModuleId, std::string> module_names;
    std::map<FunctionId, std::string> usrs;

    [[nodiscard]] const std::string& file_name(FileId id) const { return file_names.at(id).name; }

    /// A stored path of a diagnostic: a recorded file's export name, a
    /// root-relative path as it is, and an absolute path of a file that is
    /// not recorded as nothing, so no machine-specific path leaks out.
    [[nodiscard]] std::optional<std::string> exported_path(const std::string& stored) const {
        if (stored.empty()) {
            return std::nullopt;
        }
        if (const auto known = file_names_by_stored_path.find(stored);
            known != file_names_by_stored_path.end()) {
            return known->second;
        }
        if (stored.front() == '/') {
            return std::nullopt;
        }
        return stored;
    }
};

template <typename T>
Status read_into(common::Result<T> result, T& target) {
    if (!result) {
        return std::unexpected(result.error());
    }
    target = std::move(*result);
    return {};
}

common::Result<Snapshot> read_snapshot(const FactQuery& facts) {
    Snapshot snapshot;
    for (const Status& read :
         {read_into(facts.coverage(), snapshot.coverage),
          read_into(facts.modules(), snapshot.modules), read_into(facts.files(), snapshot.files),
          read_into(facts.functions(), snapshot.functions),
          read_into(facts.includes(), snapshot.includes), read_into(facts.calls(), snapshot.calls),
          read_into(facts.may_calls(), snapshot.may_calls),
          read_into(facts.diagnostics(), snapshot.diagnostics)}) {
        if (!read) {
            return std::unexpected(read.error());
        }
    }
    for (const ModuleFact& module : snapshot.modules) {
        auto edges = facts.module_dependency_edges(module.id);
        if (!edges) {
            return std::unexpected(edges.error());
        }
        std::ranges::move(*edges, std::back_inserter(snapshot.dependencies));
        snapshot.module_names.emplace(module.id, module.name);
    }
    snapshot.file_names =
        exported_file_names(snapshot.files, snapshot.includes,
                            [](std::string_view spelled) { return std::string{spelled}; });
    for (const FileFact& file : snapshot.files) {
        snapshot.file_names_by_stored_path.emplace(file.path, snapshot.file_name(file.id));
    }
    for (const FunctionFact& function : snapshot.functions) {
        snapshot.usrs.emplace(function.id, function.usr);
    }
    return snapshot;
}

std::vector<Record> file_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const FileFact& file : snapshot.files) {
        const ExportedFile& exported = snapshot.file_names.at(file.id);
        std::optional<std::string> module;
        if (file.module) {
            module = snapshot.module_names.at(*file.module);
        }
        // A file outside the root differs between machines (system headers),
        // so its hash is left out.
        std::optional<std::string> sha256;
        if (!exported.is_external) {
            sha256 = file.content_sha256;
        }
        records.emplace_back(
            std::vector<std::string>{exported.is_external ? "1" : "0", exported.name},
            JsonObject{"file"}
                .string("path", exported.name)
                .boolean("external", exported.is_external)
                .boolean("header", file.is_header)
                .optional_string("module", module)
                .optional_string("sha256", sha256)
                .line());
    }
    return records;
}

std::vector<Record> function_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const FunctionFact& function : snapshot.functions) {
        const std::string& file = snapshot.file_name(function.file);
        records.emplace_back(
            std::vector<std::string>{file, number_key(function.line_begin), function.usr},
            JsonObject{"function"}
                .string("usr", function.usr)
                .string("name", function.name)
                .string("qualified_name", function.qualified_name)
                .string("signature", function.signature)
                .string("file", file)
                .number("line_begin", function.line_begin)
                .number("line_end", function.line_end)
                .boolean("definition", function.is_definition)
                .string("linkage", to_text(function.linkage))
                .line());
    }
    return records;
}

std::vector<Record> include_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const IncludeFact& include : snapshot.includes) {
        std::optional<std::string> target;
        std::optional<bool> target_external;
        if (include.included) {
            const ExportedFile& exported = snapshot.file_names.at(*include.included);
            target = exported.name;
            target_external = exported.is_external;
        }
        const std::string& file = snapshot.file_name(include.includer);
        records.emplace_back(std::vector<std::string>{file, number_key(include.line),
                                                      include.spelled, optional_key(target)},
                             JsonObject{"include"}
                                 .string("file", file)
                                 .number("line", include.line)
                                 .string("spelled", include.spelled)
                                 .boolean("system", include.is_system)
                                 .optional_string("target", target)
                                 .optional_boolean("target_external", target_external)
                                 .line());
    }
    return records;
}

std::vector<Record> call_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const CallFact& call : snapshot.calls) {
        std::optional<std::string> callee;
        if (call.callee) {
            callee = snapshot.usrs.at(*call.callee);
        }
        const std::string& file = snapshot.file_name(call.site.file);
        const std::string& caller = snapshot.usrs.at(call.caller);
        records.emplace_back(
            std::vector<std::string>{file, number_key(call.site.line), number_key(call.site.column),
                                     caller, to_text(call.kind), optional_key(callee), call.via},
            JsonObject{"call"}
                .string("caller", caller)
                .string("file", file)
                .number("line", call.site.line)
                .number("column", call.site.column)
                .string("kind", to_text(call.kind))
                .optional_string("callee", callee)
                .string("via", call.via)
                .line());
    }
    return records;
}

std::vector<Record> may_call_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const CallFact& call : snapshot.may_calls) {
        if (!call.callee) {
            continue; // the schema's CHECK gives every may_call edge a callee
        }
        const std::string& file = snapshot.file_name(call.site.file);
        const std::string& caller = snapshot.usrs.at(call.caller);
        const std::string& callee = snapshot.usrs.at(*call.callee);
        records.emplace_back(std::vector<std::string>{file, number_key(call.site.line),
                                                      number_key(call.site.column), caller, callee,
                                                      call.via},
                             JsonObject{"may_call"}
                                 .string("caller", caller)
                                 .string("file", file)
                                 .number("line", call.site.line)
                                 .number("column", call.site.column)
                                 .string("callee", callee)
                                 .string("via", call.via)
                                 .line());
    }
    return records;
}

std::vector<Record> module_dependency_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const ModuleDependencyFact& edge : snapshot.dependencies) {
        // An include edge has no column (0 in the store).
        std::optional<std::int64_t> column;
        if (edge.via == DependencyVia::call) {
            column = edge.site.column;
        }
        const std::string& from = snapshot.module_names.at(edge.from);
        const std::string& to = snapshot.module_names.at(edge.to);
        const std::string& file = snapshot.file_name(edge.site.file);
        records.emplace_back(
            std::vector<std::string>{from, to, edge.via == DependencyVia::include ? "0" : "1", file,
                                     number_key(edge.site.line), number_key(column.value_or(0))},
            JsonObject{"module_dependency"}
                .string("from", from)
                .string("to", to)
                .string("via", to_text(edge.via))
                .string("file", file)
                .number("line", edge.site.line)
                .optional_number("column", column)
                .line());
    }
    return records;
}

std::vector<Record> diagnostic_records(const Snapshot& snapshot) {
    std::vector<Record> records;
    for (const DiagnosticFact& diagnostic : snapshot.diagnostics) {
        const std::optional<std::string> unit = snapshot.exported_path(diagnostic.translation_unit);
        const std::optional<std::string> file = snapshot.exported_path(diagnostic.file);
        records.emplace_back(std::vector<std::string>{optional_key(unit), optional_key(file),
                                                      number_key(diagnostic.line),
                                                      to_text(diagnostic.severity),
                                                      diagnostic.message},
                             JsonObject{"diagnostic"}
                                 .optional_string("translation_unit", unit)
                                 .optional_string("file", file)
                                 .number("line", diagnostic.line)
                                 .string("severity", to_text(diagnostic.severity))
                                 .string("message", diagnostic.message)
                                 .line());
    }
    return records;
}

} // namespace

std::size_t utf8_sequence_length(std::string_view text) {
    const auto byte = [&text](std::size_t i) {
        return static_cast<unsigned char>(text[i]);
    };
    const unsigned char lead = byte(0);
    std::size_t length = 0;
    unsigned char second_min = 0x80;
    unsigned char second_max = 0xBF;
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        second_min = lead == 0xE0 ? 0xA0 : 0x80; // overlong
        second_max = lead == 0xED ? 0x9F : 0xBF; // surrogates
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        second_min = lead == 0xF0 ? 0x90 : 0x80; // overlong
        second_max = lead == 0xF4 ? 0x8F : 0xBF; // above U+10FFFF
    } else {
        return 0;
    }
    if (text.size() < length || byte(1) < second_min || byte(1) > second_max) {
        return 0;
    }
    for (std::size_t i = 2; i < length; ++i) {
        if (byte(i) < 0x80 || byte(i) > 0xBF) {
            return 0;
        }
    }
    return length;
}

std::string json_string(std::string_view text) {
    std::string out = "\"";
    out.reserve(text.size() + 2);
    while (!text.empty()) {
        const std::size_t length = utf8_sequence_length(text);
        if (length == 0) {
            out += replacement_character;
            text.remove_prefix(1);
        } else if (length == 1) {
            append_escaped(out, text.front());
            text.remove_prefix(1);
        } else {
            out += text.substr(0, length);
            text.remove_prefix(length);
        }
    }
    out += '"';
    return out;
}

std::map<FileId, ExportedFile>
exported_file_names(const std::vector<FileFact>& files, const std::vector<IncludeFact>& includes,
                    const std::function<std::string(std::string_view)>& spelling) {
    std::map<FileId, ExportedFile> exported;
    std::map<FileId, const FileFact*> external;
    for (const FileFact& file : files) {
        if (file.is_external) {
            external.emplace(file.id, &file);
        } else {
            exported.emplace(file.id, ExportedFile{.name = file.path, .is_external = false});
        }
    }

    // Every directive naming each external file, sorted: the first one
    // gives the name, the rest break ties between files that one directive
    // reached in different translation units (different -I).
    using Directive = std::tuple<std::string, std::uint32_t, std::string>;
    std::map<FileId, std::vector<Directive>> directives;
    for (const IncludeFact& include : includes) {
        if (!include.included || !external.contains(*include.included)) {
            continue;
        }
        const auto includer = exported.find(include.includer);
        if (includer == exported.end()) {
            continue; // directives in external files are not recorded
        }
        directives[*include.included].emplace_back(includer->second.name, include.line,
                                                   spelling(include.spelled));
    }

    // Names are handed out in an order that does not depend on row ids
    // (insertion order): by first directive, then by all directives, then
    // by content hash; only files equal in all of these fall back to their
    // absolute path. Files no directive names come last, by file name.
    struct Candidate {
        bool unnamed{};
        std::vector<Directive> directives;
        std::string name;
        std::string sha256;
        std::string path;
        FileId id{};
    };
    std::vector<Candidate> order;
    for (const auto& [id, file] : external) {
        Candidate candidate{.unnamed = true,
                            .directives = {},
                            .name = file->path.substr(file->path.find_last_of('/') + 1),
                            .sha256 = file->content_sha256,
                            .path = file->path,
                            .id = id};
        if (auto found = directives.find(id); found != directives.end()) {
            std::ranges::sort(found->second);
            candidate.unnamed = false;
            candidate.name = std::get<2>(found->second.front());
            candidate.directives = std::move(found->second);
        }
        order.push_back(std::move(candidate));
    }
    std::ranges::sort(order, [](const Candidate& left, const Candidate& right) {
        return std::tie(left.unnamed, left.directives, left.name, left.sha256, left.path) <
               std::tie(right.unnamed, right.directives, right.name, right.sha256, right.path);
    });

    std::set<std::string> taken;
    for (const Candidate& candidate : order) {
        std::string key = candidate.name;
        for (int suffix = 2; taken.contains(key); ++suffix) {
            key = candidate.name + '#' + std::to_string(suffix);
        }
        taken.insert(key);
        exported.emplace(candidate.id, ExportedFile{.name = std::move(key), .is_external = true});
    }
    return exported;
}

Status write_jsonl_export(const FactQuery& facts, std::ostream& out) {
    const auto snapshot = read_snapshot(facts);
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }
    out << JsonObject{"header"}
               .string("format", "lcart-facts")
               .number("format_version", export_format_version)
               .number("translation_units_total", snapshot->coverage.translation_units_total)
               .number("translation_units_parsed", snapshot->coverage.translation_units_parsed)
               .line();
    for (const ModuleFact& module : snapshot->modules) { // by name
        out << JsonObject{"module"}.string("name", module.name).line();
    }
    write_sorted(file_records(*snapshot), out);
    write_sorted(function_records(*snapshot), out);
    write_sorted(include_records(*snapshot), out);
    write_sorted(call_records(*snapshot), out);
    write_sorted(may_call_records(*snapshot), out);
    write_sorted(module_dependency_records(*snapshot), out);
    write_sorted(diagnostic_records(*snapshot), out);

    out.flush();
    if (!out) {
        return std::unexpected(
            Error{.code = ErrorCode::io_error, .message = "cannot write the facts export"});
    }
    return {};
}

} // namespace lcart::facts
