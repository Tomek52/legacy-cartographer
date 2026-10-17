// Verifies: fr009-structured-claims-v2 (criteria 1 and 2: invalid claims
// are rejected with schema_violation and a list of problems, unknown kinds
// included; criterion 3 is claim_json_fuzzer), claim-json-schema-v2,
// b25-claim-parser-v1 (criteria 1-3), nfr010-untrusted-input-v1.
#include "claims/claim_parser.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using lcart::claims::Claim;
using lcart::claims::ClaimKind;
using lcart::claims::EntityType;
using lcart::claims::max_claims_input_size;
using lcart::claims::parse_claims;
using lcart::claims::Polarity;
using lcart::common::ErrorCode;
using nlohmann::json;

[[nodiscard]] std::filesystem::path data_dir() {
    return LCART_CLAIMS_DATA_DIR;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    EXPECT_TRUE(in.good()) << file;
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A document with one valid `calls` claim, for the tests to break.
[[nodiscard]] json valid_document() {
    return json::parse(R"({
        "schema": "lcart:claim:v1",
        "claims": [{
            "id": "c-0001",
            "kind": "calls",
            "subject": {"type": "function", "name": "modbus_receive"},
            "object": {"type": "function", "name": "_modbus_receive_msg"},
            "polarity": "affirmed",
            "text": "modbus_receive delegates to _modbus_receive_msg.",
            "source": {"document": "modules/core.md", "section": "Receiving"}
        }]
    })");
}

/// The problems reported for `input`, which must be rejected with
/// schema_violation: one per line of the message.
[[nodiscard]] std::vector<std::string> problems_of(std::string_view input) {
    auto claims = parse_claims(input);
    EXPECT_FALSE(claims.has_value()) << input.substr(0, 400);
    if (claims.has_value()) {
        return {};
    }
    EXPECT_EQ(claims.error().code, ErrorCode::schema_violation) << claims.error().message;
    std::vector<std::string> lines;
    std::istringstream message{claims.error().message};
    for (std::string line; std::getline(message, line);) {
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] std::vector<std::string> problems_of_document(const json& document) {
    return problems_of(document.dump());
}

[[nodiscard]] std::vector<Claim> parsed(std::string_view input) {
    auto claims = parse_claims(input);
    EXPECT_TRUE(claims.has_value()) << claims.error().message;
    return claims.value_or(std::vector<Claim>{});
}

using Problems = std::vector<std::string>;

TEST(ClaimParser, ParsesTheExampleOfTheSchemaPage) {
    // The example file is the one on the schema page, so the page and the
    // fuzzer's seed corpus cannot drift apart.
    const std::string page = read_file(LCART_DOCS_DIR "/specs/claim-schema.md");
    const std::string example = read_file(data_dir() / "valid/schema-example.json");
    const std::size_t at = page.find("Example:\n\n```json\n");
    ASSERT_NE(at, std::string::npos);
    const std::size_t begin = page.find('\n', page.find("```json", at)) + 1;
    EXPECT_EQ(page.substr(begin, page.find("```", begin) - begin), example);

    const std::vector<Claim> claims = parsed(example);
    ASSERT_EQ(claims.size(), 1U);
    const Claim& claim = claims[0];
    EXPECT_EQ(claim.id, "c-0001");
    EXPECT_EQ(claim.kind, ClaimKind::calls);
    EXPECT_EQ(claim.subject.type, EntityType::function);
    EXPECT_EQ(claim.subject.name, "modbus_receive");
    EXPECT_EQ(claim.subject.file, std::nullopt);
    EXPECT_EQ(claim.object.type, EntityType::function);
    EXPECT_EQ(claim.object.name, "_modbus_receive_msg");
    EXPECT_EQ(claim.polarity, Polarity::affirmed);
    EXPECT_EQ(claim.text, "modbus_receive delegates frame reception to _modbus_receive_msg.");
    EXPECT_EQ(claim.source.document, "modules/core.md");
    EXPECT_EQ(claim.source.section, "Receiving requests");
}

TEST(ClaimParser, ParsesEveryKindInDocumentOrder) {
    const std::vector<Claim> claims = parsed(read_file(data_dir() / "valid/every-kind.json"));
    ASSERT_EQ(claims.size(), 6U);
    EXPECT_EQ(claims[0].kind, ClaimKind::calls);
    EXPECT_EQ(claims[0].subject.file, "src/modbus.c");
    EXPECT_EQ(claims[1].kind, ClaimKind::includes);
    EXPECT_EQ(claims[1].subject.type, EntityType::file);
    EXPECT_EQ(claims[1].polarity, Polarity::negated);
    EXPECT_EQ(claims[1].source.document, "modules/tcp.md");
    EXPECT_EQ(claims[1].source.section, "");
    EXPECT_EQ(claims[2].id, "c-123456");
    EXPECT_EQ(claims[2].kind, ClaimKind::depends_on);
    EXPECT_EQ(claims[2].object.type, EntityType::module);
    EXPECT_EQ(claims[3].kind, ClaimKind::defines);
    EXPECT_EQ(claims[3].text, "");
    EXPECT_EQ(claims[4].kind, ClaimKind::belongs_to);
    EXPECT_EQ(claims[4].subject.type, EntityType::file);
    EXPECT_EQ(claims[4].source.section, "Files");
    EXPECT_EQ(claims[5].kind, ClaimKind::belongs_to);
    EXPECT_EQ(claims[5].subject.type, EntityType::function);
    EXPECT_EQ(claims[5].subject.file, "src/modbus-rtu.c");
}

TEST(ClaimParser, AcceptsADocumentWithoutClaims) {
    EXPECT_TRUE(parsed(read_file(data_dir() / "valid/no-claims.json")).empty());
}

// The valid and invalid files double as the fuzzer's seed corpus
// (scripts/fuzz-smoke.sh), so each must be what its directory says.
TEST(ClaimParser, AcceptsEveryValidFileAndRejectsEveryInvalidOne) {
    for (const char* kind : {"valid", "invalid"}) {
        for (const auto& entry : std::filesystem::directory_iterator{data_dir() / kind}) {
            const auto claims = parse_claims(read_file(entry.path()));
            EXPECT_EQ(claims.has_value(), std::string_view{kind} == "valid") << entry.path();
        }
    }
}

TEST(ClaimParser, ReportsEveryProblemWithItsPointer) {
    const std::string belongs_to_module =
        std::string{"/claims/2/subject/type: a belongs_to claim needs a file or function "} +
        "here, not a module";
    EXPECT_EQ(problems_of(read_file(data_dir() / "invalid/many-problems.json")),
              (Problems{
                  R"(/schema: must be "lcart:claim:v1")",
                  "/claims/0/object/name: must not be empty",
                  "/claims/0/polarity: not one of: affirmed, negated",
                  "/claims/0/subject/type: a calls claim needs a function here, not a file",
                  "/claims/1/id: does not match ^c-[0-9]{4,6}$",
                  "/claims/1/subject/line: property not allowed by the claim schema",
                  "/claims/1/object: required property missing",
                  "/claims/1/text: must be a string",
                  "/claims/1/confidence: property not allowed by the claim schema",
                  "/claims/2/id: duplicate claim ID, first used at /claims/0/id",
                  "/claims/2/source/page: property not allowed by the claim schema",
                  belongs_to_module,
                  "/comment: property not allowed by the claim schema",
              }));
}

TEST(ClaimParser, RejectsInputThatIsNotJson) {
    EXPECT_EQ(problems_of(read_file(data_dir() / "invalid/truncated.json")),
              Problems{R"("": not valid JSON (unexpected end of input))"});
    EXPECT_EQ(problems_of(""), Problems{R"("": not valid JSON (unexpected end of input))"});
    // Bytes count from 1.
    EXPECT_EQ(problems_of(R"({"schema": "lcart:claim:v1", "claims": []} [])"),
              Problems{R"("": not valid JSON (syntax error at byte 44))"});
    // Invalid UTF-8 in a string; nlohmann_json rejects it while parsing.
    EXPECT_EQ(problems_of("{\"schema\": \"\xff\"}"),
              Problems{R"("": not valid JSON (syntax error at byte 13))"});
}

TEST(ClaimParser, RejectsADocumentThatIsNotAnObject) {
    EXPECT_EQ(problems_of("[]"), Problems{R"("": must be an object)"});
    EXPECT_EQ(problems_of(R"("lcart:claim:v1")"), Problems{R"("": must be an object)"});
    EXPECT_EQ(problems_of("null"), Problems{R"("": must be an object)"});
}

TEST(ClaimParser, RejectsMissingRequiredProperties) {
    EXPECT_EQ(problems_of("{}"), (Problems{"/schema: required property missing",
                                           "/claims: required property missing"}));
    for (const char* property : {"id", "kind", "subject", "object", "polarity", "text"}) {
        json document = valid_document();
        document["claims"][0].erase(property);
        EXPECT_EQ(problems_of_document(document),
                  Problems{"/claims/0/" + std::string{property} + ": required property missing"});
    }
    for (const char* property : {"type", "name"}) {
        json document = valid_document();
        document["claims"][0]["object"].erase(property);
        EXPECT_EQ(
            problems_of_document(document),
            Problems{"/claims/0/object/" + std::string{property} + ": required property missing"});
    }
}

TEST(ClaimParser, RejectsPropertiesTheSchemaDoesNotAllow) {
    json document = valid_document();
    document["version"] = 2;
    document["claims"][0]["confidence"] = 0.9;
    document["claims"][0]["subject"]["line"] = 12;
    document["claims"][0]["source"]["url"] = "https://example.org";
    EXPECT_EQ(problems_of_document(document),
              (Problems{"/claims/0/subject/line: property not allowed by the claim schema",
                        "/claims/0/source/url: property not allowed by the claim schema",
                        "/claims/0/confidence: property not allowed by the claim schema",
                        "/version: property not allowed by the claim schema"}));
}

TEST(ClaimParser, RejectsAnotherSchemaId) {
    for (const json& schema : {json("lcart:claim:v2"), json(""), json(1)}) {
        json document = valid_document();
        document["schema"] = schema;
        EXPECT_EQ(problems_of_document(document), Problems{R"(/schema: must be "lcart:claim:v1")"});
    }
}

TEST(ClaimParser, RejectsValuesOfTheWrongType) {
    json document = valid_document();
    document["claims"] = json::object();
    EXPECT_EQ(problems_of_document(document), Problems{"/claims: must be an array"});

    document = valid_document();
    document["claims"].push_back("c-0002");
    EXPECT_EQ(problems_of_document(document), Problems{"/claims/1: must be an object"});

    document = valid_document();
    json& claim = document["claims"][0];
    claim["id"] = 1;
    claim["kind"] = json::array({"calls"});
    claim["subject"] = "modbus_receive";
    claim["object"]["name"] = nullptr;
    claim["object"]["file"] = false;
    claim["polarity"] = true;
    claim["text"] = json::object();
    claim["source"] = json::array();
    EXPECT_EQ(problems_of_document(document), (Problems{
                                                  "/claims/0/id: must be a string",
                                                  "/claims/0/kind: must be a string",
                                                  "/claims/0/subject: must be an object",
                                                  "/claims/0/object/name: must be a string",
                                                  "/claims/0/object/file: must be a string",
                                                  "/claims/0/polarity: must be a string",
                                                  "/claims/0/text: must be a string",
                                                  "/claims/0/source: must be an object",
                                              }));

    document = valid_document();
    document["claims"][0]["source"]["document"] = 7;
    document["claims"][0]["source"]["section"] = false;
    EXPECT_EQ(problems_of_document(document),
              (Problems{"/claims/0/source/document: must be a string",
                        "/claims/0/source/section: must be a string"}));
}

TEST(ClaimParser, RejectsMoreThan500Claims) {
    json document = valid_document();
    json claims = json::array();
    for (int number = 1; number <= 500; ++number) {
        json claim = document["claims"][0];
        claim["id"] = "c-" + std::to_string(10000 + number);
        claims.push_back(std::move(claim));
    }
    document["claims"] = claims;
    EXPECT_EQ(parsed(document.dump()).size(), 500U);

    document["claims"].push_back(document["claims"][0]);
    EXPECT_EQ(problems_of_document(document), Problems{"/claims: more than 500 claims"});
}

TEST(ClaimParser, RejectsIdsThatDoNotMatchThePattern) {
    for (const char* id : {"c-0001", "c-12345", "c-123456"}) {
        json document = valid_document();
        document["claims"][0]["id"] = id;
        EXPECT_EQ(parsed(document.dump()).at(0).id, id);
    }
    // c-0001 in full-width digits (U+FF10, U+FF11): not ASCII [0-9].
    const std::string full_width =
        std::string{"c-"} + "\xef\xbc\x90\xef\xbc\x90" + "\xef\xbc\x90\xef\xbc\x91";
    for (const std::string& id :
         {std::string{}, std::string{"c-"}, std::string{"c-123"}, std::string{"c-1234567"},
          std::string{"C-0001"}, std::string{"d-0001"}, std::string{"c_0001"},
          std::string{"c-00a1"}, std::string{" c-0001"}, std::string{"c-0001 "},
          std::string{"c-0001\n"}, full_width}) {
        json document = valid_document();
        document["claims"][0]["id"] = id;
        EXPECT_EQ(problems_of_document(document),
                  Problems{"/claims/0/id: does not match ^c-[0-9]{4,6}$"})
            << id;
    }
}

TEST(ClaimParser, RejectsStringsLongerThanTheirMaxLength) {
    struct Limit {
        const char* pointer;
        std::size_t max_length;
    };
    for (const Limit limit : {Limit{.pointer = "/claims/0/subject/name", .max_length = 512},
                              Limit{.pointer = "/claims/0/object/file", .max_length = 1024},
                              Limit{.pointer = "/claims/0/text", .max_length = 2000}}) {
        const json::json_pointer at{limit.pointer};
        json document = valid_document();
        document[at] = std::string(limit.max_length, 'x');
        EXPECT_EQ(parsed(document.dump()).size(), 1U) << limit.pointer;
        // Characters, not bytes, as in JSON Schema: two bytes each here.
        std::string accented;
        for (std::size_t i = 0; i < limit.max_length; ++i) {
            accented += "\xc3\xa9";
        }
        document[at] = accented;
        EXPECT_EQ(parsed(document.dump()).size(), 1U) << limit.pointer;

        document[at] = std::string(limit.max_length + 1, 'x');
        EXPECT_EQ(problems_of_document(document),
                  Problems{std::string{limit.pointer} + ": longer than " +
                           std::to_string(limit.max_length) + " characters"});
    }
}

TEST(ClaimParser, RejectsAnEmptyEntityName) {
    json document = valid_document();
    document["claims"][0]["subject"]["name"] = "";
    EXPECT_EQ(problems_of_document(document),
              Problems{"/claims/0/subject/name: must not be empty"});
}

TEST(ClaimParser, RejectsKindsNotInTheSchema) {
    EXPECT_EQ(problems_of(read_file(data_dir() / "invalid/unknown-kind.json")),
              Problems{"/claims/0/kind: not one of: calls, includes, depends_on, defines, "
                       "belongs_to"});
    for (const char* kind : {"", "Calls", "may_call", "calls "}) {
        json document = valid_document();
        document["claims"][0]["kind"] = kind;
        EXPECT_EQ(problems_of_document(document).size(), 1U) << kind;
    }
}

TEST(ClaimParser, RejectsEntityTypesAndPolaritiesOutsideTheirEnums) {
    json document = valid_document();
    document["claims"][0]["subject"]["type"] = "macro";
    document["claims"][0]["polarity"] = "Negated";
    EXPECT_EQ(problems_of_document(document),
              (Problems{"/claims/0/subject/type: not one of: function, file, module",
                        "/claims/0/polarity: not one of: affirmed, negated"}));
}

/// One row of the table of docs/specs/claim-schema.md#claim-kinds-v1.
struct KindRule {
    std::string kind;
    std::vector<std::string> subjects;
    std::string object;
};

/// The problems a claim of `rule.kind` with these entity types must have.
[[nodiscard]] Problems entity_type_problems(const KindRule& rule, const std::string& subject,
                                            const std::string& object) {
    const std::string needs = "a " + rule.kind + " claim needs ";
    Problems expected;
    if (std::ranges::find(rule.subjects, subject) == rule.subjects.end()) {
        std::string line = "/claims/0/subject/type: " + needs;
        line += rule.kind == "belongs_to" ? "a file or function" : "a " + rule.subjects[0];
        line += " here, not a ";
        line += subject;
        expected.push_back(std::move(line));
    }
    if (object != rule.object) {
        std::string line = "/claims/0/object/type: " + needs;
        line += "a ";
        line += rule.object;
        line += " here, not a ";
        line += object;
        expected.push_back(std::move(line));
    }
    return expected;
}

/// A claim of `rule.kind` with these entity types is accepted if they fit
/// the rule and otherwise rejected with entity_type_problems().
void expect_entity_types_checked(const KindRule& rule, const std::string& subject,
                                 const std::string& object) {
    json document = valid_document();
    document["claims"][0]["kind"] = rule.kind;
    document["claims"][0]["subject"]["type"] = subject;
    document["claims"][0]["object"]["type"] = object;
    const Problems expected = entity_type_problems(rule, subject, object);
    if (expected.empty()) {
        EXPECT_EQ(parsed(document.dump()).size(), 1U) << rule.kind << subject << object;
    } else {
        EXPECT_EQ(problems_of_document(document), expected) << rule.kind << subject << object;
    }
}

TEST(ClaimParser, RejectsEntityTypesThatDoNotFitTheKind) {
    const std::vector<KindRule> rules{
        {.kind = "calls", .subjects = {"function"}, .object = "function"},
        {.kind = "includes", .subjects = {"file"}, .object = "file"},
        {.kind = "depends_on", .subjects = {"module"}, .object = "module"},
        {.kind = "defines", .subjects = {"file"}, .object = "function"},
        {.kind = "belongs_to", .subjects = {"file", "function"}, .object = "module"},
    };
    const std::vector<std::string> types{"function", "file", "module"};
    for (const KindRule& rule : rules) {
        for (const std::string& subject : types) {
            for (const std::string& object : types) {
                expect_entity_types_checked(rule, subject, object);
            }
        }
    }
}

TEST(ClaimParser, RejectsDuplicateClaimIds) {
    json document = valid_document();
    document["claims"].push_back(document["claims"][0]);
    document["claims"].push_back(document["claims"][0]);
    document["claims"][1]["id"] = "c-0002";
    EXPECT_EQ(problems_of_document(document),
              Problems{"/claims/2/id: duplicate claim ID, first used at /claims/0/id"});
}

TEST(ClaimParser, EscapesPropertyNamesInPointers) {
    json document = valid_document();
    document["a/b~c"] = 1;
    document["line\nbreak"] = 1;
    document[std::string(300, 'k')] = 1;
    EXPECT_EQ(
        problems_of_document(document),
        (Problems{"/a~1b~0c: property not allowed by the claim schema",
                  "/" + std::string(64, 'k') + "...: property not allowed by the claim schema",
                  R"(/line\u000abreak: property not allowed by the claim schema)"}));
}

TEST(ClaimParser, RejectsInputOverTheSizeLimitBeforeParsing) {
    std::string at_limit = valid_document().dump();
    at_limit.resize(max_claims_input_size, ' ');
    EXPECT_EQ(parsed(at_limit).size(), 1U);

    // Not JSON at all: the size alone decides, so nothing was parsed.
    const std::string over_limit(max_claims_input_size + 1, '[');
    EXPECT_EQ(problems_of(over_limit), Problems{R"("": larger than 4 MiB (4194304 bytes))"});
}

TEST(ClaimParser, RejectsNestingDeeperThanTheSchemaAllowsBeforeBuildingADocument) {
    EXPECT_EQ(problems_of(read_file(data_dir() / "invalid/too-deep.json")),
              Problems{"/claims/0/subject/file: nested deeper than 4 levels, the most the "
                       "claim schema has"});
    // Deep enough to overflow the stack of any recursive parser or validator.
    const std::string deep(std::size_t{1'000'000}, '[');
    EXPECT_EQ(problems_of(deep),
              Problems{"/0/0/0/0: nested deeper than 4 levels, the most the claim schema has"});
    EXPECT_EQ(problems_of(R"({"claims": [{"subject": {"name": {"x": 1}}}]})"),
              Problems{"/claims/0/subject/name: nested deeper than 4 levels, the most the "
                       "claim schema has"});
}

} // namespace
