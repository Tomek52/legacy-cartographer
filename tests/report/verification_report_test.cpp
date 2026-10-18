// Verifies: fr011-verification-report-v2 (criterion 1: status names in JSON;
// criterion 3: tool, fact schema and handoff contract versions and the
// agent recorded; counts per status, contradicted and unverified claims with
// reason and evidence, and the coverage), b28-verification-report-v1
// (criterion 2: versioned JSON format, Markdown with counts per status and
// kind, then contradicted and unverified claims with evidence as file and
// line, then the coverage; criterion 3: the same input gives the same bytes;
// criteria 4 and 5: contract version null and agent empty unless given),
// nfr004-deterministic-output-v1, claim-verification-report-format-v1,
// adr0030-report-format-v1 and adr0030-evidence-and-file-names-v1.
#include "common/version.hpp"
#include "facts/sqlite_fact_store.hpp"
#include "report/report_writer.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace lcart::facts;
using lcart::claims::Claim;
using lcart::claims::ClaimKind;
using lcart::claims::EntityType;
using lcart::claims::Polarity;
using lcart::common::ErrorCode;
using lcart::report::ReportFormat;
using lcart::report::VerificationReport;
using lcart::report::write_verification_report;
using lcart::verify::FactRef;
using lcart::verify::FactTable;
using lcart::verify::VerdictReason;
using lcart::verify::VerificationStatus;
using Json = nlohmann::json;

template <typename T>
T value_of(lcart::common::Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

void expect_ok(const lcart::common::Status& status) {
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

/// Two modules, a file outside the root included by an absolute spelling
/// (a bundle names it `<outside-root>/x.h`, ADR-0028), a direct and an
/// indirect call, complete coverage.
struct Facts {
    std::unique_ptr<SqliteFactStore> store = value_of(SqliteFactStore::open(":memory:"));
    ModuleId core{};
    FileId core_c{};
    FileId main_c{};
    FunctionId core_length{};
    FunctionId main{};
    CallId direct_call{};
    CallId indirect_call{};
    IncludeId absolute_include{};

    Facts() {
        core = value_of(store->add_module({.name = "core"}));
        const ModuleId app = value_of(store->add_module({.name = "app"}));
        core_c = value_of(store->add_file(
            {.path = "core/core.c", .content_sha256 = "h", .is_header = false, .module = core}));
        main_c = value_of(store->add_file(
            {.path = "app/main.c", .content_sha256 = "h", .is_header = false, .module = app}));
        const FileId x_h = value_of(store->add_file(
            {.path = "/home/alice/lib/x.h", .is_header = true, .is_external = true}));
        const FileId stdio_h = value_of(store->add_file(
            {.path = "/usr/include/stdio.h", .is_header = true, .is_external = true}));
        expect_ok(store->add_include(
            {.includer = main_c, .included = x_h, .spelled = "/home/alice/lib/x.h", .line = 1}));
        expect_ok(store->add_include({.includer = main_c,
                                      .included = stdio_h,
                                      .spelled = "stdio.h",
                                      .line = 2,
                                      .is_system = true}));
        core_length = value_of(store->add_function({.usr = "c:@F@core_length",
                                                    .name = "core_length",
                                                    .qualified_name = "core_length",
                                                    .file = core_c,
                                                    .line_begin = 5,
                                                    .line_end = 7,
                                                    .is_definition = true}));
        main = value_of(store->add_function({.usr = "c:@F@main",
                                             .name = "main",
                                             .qualified_name = "main",
                                             .file = main_c,
                                             .line_begin = 9,
                                             .line_end = 12,
                                             .is_definition = true}));
        expect_ok(store->add_call({.caller = main,
                                   .callee = core_length,
                                   .site = {.file = main_c, .line = 10, .column = 12}}));
        expect_ok(store->add_call({.caller = main,
                                   .kind = CallKind::indirect,
                                   .via = "ops.send",
                                   .site = {.file = main_c, .line = 11, .column = 5}}));
        expect_ok(store->derive_module_dependencies());
        expect_ok(
            store->set_coverage({.translation_units_total = 2, .translation_units_parsed = 2}));
        for (const CallFact& call : value_of(store->calls_from(main))) {
            (call.kind == CallKind::direct ? direct_call : indirect_call) = call.id;
        }
        absolute_include = value_of(store->includes_of(main_c)).at(0).id;
    }
};

Claim claim(std::string id, ClaimKind kind, EntityType subject_type, std::string subject,
            EntityType object_type, std::string object, std::string text = "A sentence.") {
    return {.id = std::move(id),
            .kind = kind,
            .subject = {.type = subject_type, .name = std::move(subject), .file = std::nullopt},
            .object = {.type = object_type, .name = std::move(object), .file = std::nullopt},
            .polarity = Polarity::affirmed,
            .source = {.document = "modules/app.md", .section = "Calls"},
            .text = std::move(text)};
}

/// Six claims, one verdict each: two confirmed, one unverified, three
/// contradicted, with evidence from every fact table.
VerificationReport report_of(const Facts& facts) {
    VerificationReport report;
    const auto add = [&report](Claim made, VerificationStatus status, VerdictReason reason,
                               std::vector<FactRef> evidence) {
        report.verdicts.push_back({.claim_id = made.id,
                                   .status = status,
                                   .reason = reason,
                                   .evidence = std::move(evidence)});
        report.claims.push_back(std::move(made));
    };
    using enum EntityType;
    add(claim("c-0001", ClaimKind::calls, function, "main", function, "core_length"),
        VerificationStatus::confirmed, VerdictReason::relation_found,
        {{.table = FactTable::call_edge, .id = facts.direct_call.value}});
    add(claim("c-0002", ClaimKind::includes, file, "app/main.c", file, "x.h"),
        VerificationStatus::confirmed, VerdictReason::relation_found,
        {{.table = FactTable::include_edge, .id = facts.absolute_include.value}});
    add(claim("c-0003", ClaimKind::depends_on, module, "core", module, "app"),
        VerificationStatus::contradicted, VerdictReason::relation_absent,
        {{.table = FactTable::module, .id = facts.core.value}});
    add(claim("c-0004", ClaimKind::calls, function, "main", function, "printf"),
        VerificationStatus::contradicted, VerdictReason::entity_not_found, {});
    add(claim("c-0005", ClaimKind::calls, function, "main", function, "core_send"),
        VerificationStatus::unverified, VerdictReason::indirect_calls_present,
        {{.table = FactTable::call_edge, .id = facts.indirect_call.value}});
    add(claim("c-0006", ClaimKind::belongs_to, function, "core_length", module, "app"),
        VerificationStatus::contradicted, VerdictReason::relation_absent,
        {{.table = FactTable::function, .id = facts.core_length.value},
         {.table = FactTable::file, .id = facts.core_c.value}});
    return report;
}

std::string written(const VerificationReport& report, const FactQuery& facts, ReportFormat format) {
    std::ostringstream out;
    expect_ok(write_verification_report(report, facts, format, out));
    return std::move(out).str();
}

Json json_report(const VerificationReport& report, const FactQuery& facts) {
    return Json::parse(written(report, facts, ReportFormat::json));
}

const Json& claim_entry(const Json& json, std::string_view id) {
    for (const Json& entry : json.at("claims")) {
        if (entry.at("id") == id) {
            return entry;
        }
    }
    ADD_FAILURE() << id << " not in the report";
    static const Json none;
    return none;
}

TEST(VerificationReport, JsonRecordsFormatAndVersions) {
    const Facts facts;
    const std::string text = written(report_of(facts), *facts.store, ReportFormat::json);
    // The first two lines identify the file, so lcart can recognise its own
    // report before replacing it (ADR-0030).
    EXPECT_TRUE(text.starts_with("{\n  \"format\": \"lcart-verification-report\",\n")) << text;
    EXPECT_TRUE(text.ends_with("}\n"));
    const Json json = Json::parse(text);
    EXPECT_EQ(json.at("format_version"), 1);
    EXPECT_EQ(json.at("tool_version"), std::string{lcart::common::version});
    EXPECT_EQ(json.at("fact_schema_version"), schema_version);
    EXPECT_TRUE(json.at("handoff_contract_version").is_null());
    EXPECT_EQ(json.at("agent"), "");
}

TEST(VerificationReport, ContractVersionAndAgentAsGiven) {
    const Facts facts;
    VerificationReport report = report_of(facts);
    report.handoff_contract_version = 1;
    report.agent = "Example agent, example model";
    const Json json = json_report(report, *facts.store);
    EXPECT_EQ(json.at("handoff_contract_version"), 1);
    EXPECT_EQ(json.at("agent"), "Example agent, example model");
    const std::string markdown = written(report, *facts.store, ReportFormat::markdown);
    EXPECT_NE(markdown.find("Example agent, example model"), std::string::npos) << markdown;
}

TEST(VerificationReport, StatusNamesAreExactAndCountedPerKind) {
    const Facts facts;
    const Json json = json_report(report_of(facts), *facts.store);
    EXPECT_EQ(json.at("counts").at("by_status"),
              Json::parse(R"({"confirmed": 2, "unverified": 1, "contradicted": 3})"));
    EXPECT_EQ(json.at("counts").at("by_kind"), Json::parse(R"({
        "calls": {"confirmed": 1, "unverified": 1, "contradicted": 1},
        "includes": {"confirmed": 1, "unverified": 0, "contradicted": 0},
        "depends_on": {"confirmed": 0, "unverified": 0, "contradicted": 1},
        "defines": {"confirmed": 0, "unverified": 0, "contradicted": 0},
        "belongs_to": {"confirmed": 0, "unverified": 0, "contradicted": 1}})"));
    EXPECT_EQ(claim_entry(json, "c-0001").at("status"), "confirmed");
    EXPECT_EQ(claim_entry(json, "c-0005").at("status"), "unverified");
    EXPECT_EQ(claim_entry(json, "c-0004").at("status"), "contradicted");
    EXPECT_EQ(claim_entry(json, "c-0004").at("reason"), "entity_not_found");
    EXPECT_EQ(json.at("coverage"), Json::parse(R"({"translation_units_total": 2,
        "translation_units_parsed": 2, "complete": true, "error_diagnostics": 0})"));
}

TEST(VerificationReport, JsonRepeatsEveryClaimInInputOrder) {
    const Facts facts;
    const Json json = json_report(report_of(facts), *facts.store);
    ASSERT_EQ(json.at("claims").size(), 6U);
    EXPECT_EQ(json.at("claims").at(0), Json::parse(R"({
        "id": "c-0001", "kind": "calls", "polarity": "affirmed",
        "subject": {"type": "function", "name": "main", "file": null},
        "object": {"type": "function", "name": "core_length", "file": null},
        "text": "A sentence.",
        "source": {"document": "modules/app.md", "section": "Calls"},
        "status": "confirmed", "reason": "relation_found",
        "evidence": [{"table": "call_edge", "kind": "direct", "caller": "main",
                      "callee": "core_length", "via": "", "file": "app/main.c",
                      "line": 10, "column": 12}]})"));
}

TEST(VerificationReport, EvidenceNamesFileAndLineForEveryTable) {
    const Facts facts;
    const Json json = json_report(report_of(facts), *facts.store);
    EXPECT_EQ(claim_entry(json, "c-0002").at("evidence"),
              Json::parse(R"([{"table": "include_edge", "file": "app/main.c", "line": 1,
                               "target": "<outside-root>/x.h"}])"));
    EXPECT_EQ(claim_entry(json, "c-0003").at("evidence"),
              Json::parse(R"([{"table": "module", "module": "core"}])"));
    EXPECT_EQ(claim_entry(json, "c-0004").at("evidence"), Json::array());
    EXPECT_EQ(claim_entry(json, "c-0005").at("evidence"),
              Json::parse(R"([{"table": "call_edge", "kind": "indirect", "caller": "main",
                               "callee": null, "via": "ops.send", "file": "app/main.c",
                               "line": 11, "column": 5}])"));
    EXPECT_EQ(claim_entry(json, "c-0006").at("evidence"),
              Json::parse(R"([{"table": "function", "function": "core_length",
                               "file": "core/core.c", "line": 5, "definition": true},
                              {"table": "file", "file": "core/core.c"}])"));
}

TEST(VerificationReport, NoAbsolutePathInEitherFormat) {
    const Facts facts;
    for (const ReportFormat format : {ReportFormat::json, ReportFormat::markdown}) {
        const std::string text = written(report_of(facts), *facts.store, format);
        EXPECT_EQ(text.find("/home/alice"), std::string::npos) << text;
        EXPECT_EQ(text.find("/usr/include"), std::string::npos) << text;
    }
}

std::string markdown_of(const Facts& facts) {
    return written(report_of(facts), *facts.store, ReportFormat::markdown);
}

/// The part of `markdown` from `heading` to the next second-level heading.
std::string section(const std::string& markdown, std::string_view heading) {
    const std::size_t begin = markdown.find("\n## " + std::string{heading} + "\n");
    if (begin == std::string::npos) {
        ADD_FAILURE() << heading << " missing in\n" << markdown;
        return {};
    }
    const std::size_t end = markdown.find("\n## ", begin + 1);
    return markdown.substr(begin, end == std::string::npos ? end : end - begin);
}

TEST(VerificationReport, MarkdownHasCountsThenContradictedThenUnverifiedThenCoverage) {
    const Facts facts;
    const std::string markdown = markdown_of(facts);
    EXPECT_TRUE(markdown.starts_with("# Verification report\n")) << markdown;
    std::size_t previous = 0;
    for (const char* heading : {"\n## Counts\n", "\n## Contradicted claims\n",
                                "\n## Unverified claims\n", "\n## Coverage\n"}) {
        const std::size_t position = markdown.find(heading);
        EXPECT_NE(position, std::string::npos) << heading;
        EXPECT_LT(previous, position) << heading;
        previous = position;
    }
}

TEST(VerificationReport, MarkdownCountsPerKindAndStatus) {
    const Facts facts;
    const std::string counts = section(markdown_of(facts), "Counts");
    EXPECT_NE(counts.find("| `calls` | 1 | 1 | 1 | 3 |"), std::string::npos) << counts;
    EXPECT_NE(counts.find("| all | 2 | 1 | 3 | 6 |"), std::string::npos) << counts;
}

TEST(VerificationReport, MarkdownListsContradictedClaimsWithReasonAndEvidence) {
    const Facts facts;
    const std::string markdown = markdown_of(facts);
    const std::string contradicted = section(markdown, "Contradicted claims");
    for (const char* id : {"c-0003", "c-0004", "c-0006"}) {
        EXPECT_NE(contradicted.find(id), std::string::npos) << id;
    }
    EXPECT_EQ(markdown.find("c-0001"), std::string::npos) << "confirmed claims are counted only";
    EXPECT_NE(contradicted.find("`relation_absent`"), std::string::npos) << contradicted;
    EXPECT_NE(contradicted.find("`core/core.c:5`"), std::string::npos) << contradicted;
}

TEST(VerificationReport, MarkdownListsUnverifiedClaimsThenTheCoverage) {
    const Facts facts;
    const std::string markdown = markdown_of(facts);
    const std::string unverified = section(markdown, "Unverified claims");
    EXPECT_NE(unverified.find("c-0005"), std::string::npos) << unverified;
    EXPECT_NE(unverified.find("`indirect_calls_present`"), std::string::npos) << unverified;
    EXPECT_NE(unverified.find("`app/main.c:11:5`"), std::string::npos) << unverified;
    EXPECT_NE(section(markdown, "Coverage").find("2 of 2 translation units parsed"),
              std::string::npos)
        << markdown;
}

TEST(VerificationReport, UntrustedTextCannotAddMarkdown) {
    const Facts facts;
    VerificationReport report = report_of(facts);
    report.claims.at(2).text = "evil\n# Heading <script>`x`|";
    report.claims.at(2).subject.name = "core\n## Coverage";
    const std::string markdown = written(report, *facts.store, ReportFormat::markdown);
    EXPECT_EQ(markdown.find("\n# Heading"), std::string::npos) << markdown;
    EXPECT_EQ(markdown.find("\n## Coverage"), markdown.rfind("\n## Coverage")) << markdown;
    EXPECT_NE(markdown.find("``evil\xEF\xBF\xBD# Heading <script>`x`|``"), std::string::npos)
        << markdown;
}

TEST(VerificationReport, SameInputGivesTheSameBytes) {
    const Facts facts;
    for (const ReportFormat format : {ReportFormat::json, ReportFormat::markdown}) {
        EXPECT_EQ(written(report_of(facts), *facts.store, format),
                  written(report_of(facts), *facts.store, format));
    }
}

TEST(VerificationReport, EveryClaimNeedsItsVerdict) {
    const Facts facts;
    VerificationReport report = report_of(facts);
    report.verdicts.pop_back();
    std::ostringstream out;
    const auto written_report =
        write_verification_report(report, *facts.store, ReportFormat::json, out);
    ASSERT_FALSE(written_report.has_value());
    EXPECT_EQ(written_report.error().code, ErrorCode::invalid_argument);
    report = report_of(facts);
    report.verdicts.at(0).claim_id = "c-0099";
    EXPECT_FALSE(write_verification_report(report, *facts.store, ReportFormat::markdown, out));
}

} // namespace
