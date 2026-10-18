#include "common/version.hpp"
#include "facts/fact_model.hpp"
#include "handoff/handoff.hpp"
#include "report/report_writer.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "markdown_text.hpp"

namespace lcart::report {

namespace {

using claims::ClaimKind;
using claims::EntityType;
using claims::Polarity;
using common::Error;
using common::ErrorCode;
using common::Result;
using detail::code_span;
using facts::FactQuery;
using verify::FactRef;
using verify::FactTable;
using verify::VerdictReason;
using verify::VerificationStatus;
/// Keys in the order ADR-0030 fixes, not sorted: the first two lines
/// identify the file.
using Json = nlohmann::ordered_json;

constexpr std::string_view report_format = "lcart-verification-report";
constexpr int report_format_version = 1;

constexpr std::array<VerificationStatus, 3> statuses{VerificationStatus::confirmed,
                                                     VerificationStatus::unverified,
                                                     VerificationStatus::contradicted};
constexpr std::array<ClaimKind, 5> kinds{ClaimKind::calls, ClaimKind::includes,
                                         ClaimKind::depends_on, ClaimKind::defines,
                                         ClaimKind::belongs_to};

/// The names of the claim schema (docs/specs/claim-schema.md).
std::string_view to_string(ClaimKind kind) {
    switch (kind) {
    case ClaimKind::calls:
        return "calls";
    case ClaimKind::includes:
        return "includes";
    case ClaimKind::depends_on:
        return "depends_on";
    case ClaimKind::defines:
        return "defines";
    case ClaimKind::belongs_to:
        return "belongs_to";
    }
    std::unreachable();
}

std::string_view to_string(EntityType type) {
    switch (type) {
    case EntityType::function:
        return "function";
    case EntityType::file:
        return "file";
    case EntityType::module:
        return "module";
    }
    std::unreachable();
}

std::string_view to_string(Polarity polarity) {
    switch (polarity) {
    case Polarity::affirmed:
        return "affirmed";
    case Polarity::negated:
        return "negated";
    }
    std::unreachable();
}

std::string_view to_string(VerdictReason reason) {
    switch (reason) {
    case VerdictReason::relation_found:
        return "relation_found";
    case VerdictReason::relation_absent:
        return "relation_absent";
    case VerdictReason::entity_not_found:
        return "entity_not_found";
    case VerdictReason::entity_ambiguous:
        return "entity_ambiguous";
    case VerdictReason::coverage_incomplete:
        return "coverage_incomplete";
    case VerdictReason::indirect_calls_present:
        return "indirect_calls_present";
    }
    std::unreachable();
}

/// The tables of the data model (ADR-0029).
std::string_view to_string(FactTable table) {
    switch (table) {
    case FactTable::module:
        return "module";
    case FactTable::file:
        return "file";
    case FactTable::function:
        return "function";
    case FactTable::include_edge:
        return "include_edge";
    case FactTable::call_edge:
        return "call_edge";
    }
    std::unreachable();
}

std::string_view to_string(facts::CallKind kind) {
    switch (kind) {
    case facts::CallKind::direct:
        return "direct";
    case facts::CallKind::indirect:
        return "indirect";
    case facts::CallKind::may_call:
        return "may_call";
    }
    std::unreachable();
}

Error invalid(std::string message) {
    return Error{.code = ErrorCode::invalid_argument, .message = std::move(message)};
}

template <typename T>
common::Status read_into(Result<T> result, T& target) {
    if (!result) {
        return std::unexpected(result.error());
    }
    target = std::move(*result);
    return {};
}

/// The coverage section of both formats.
struct Coverage {
    facts::AnalysisCoverage counts;
    std::size_t error_diagnostics{};

    /// As the claim schema defines it (entity resolution): at least one
    /// translation unit, every one parsed.
    [[nodiscard]] bool complete() const {
        return counts.translation_units_total > 0 &&
               counts.translation_units_parsed == counts.translation_units_total;
    }
};

/// The facts the report names, read once, and the evidence of each
/// verdict turned into what a reader can find in the code: file and line,
/// never row ids, which only this database knows (ADR-0030).
class Facts {
public:
    static Result<Facts> read(const FactQuery& query) {
        Facts facts;
        std::vector<facts::ModuleFact> modules;
        std::vector<facts::FileFact> files;
        std::vector<facts::FunctionFact> functions;
        std::vector<facts::IncludeFact> includes;
        std::vector<facts::CallFact> calls;
        std::vector<facts::CallFact> may_calls;
        std::vector<facts::DiagnosticFact> diagnostics;
        for (const common::Status& read :
             {read_into(query.modules(), modules), read_into(query.files(), files),
              read_into(query.functions(), functions), read_into(query.includes(), includes),
              read_into(query.calls(), calls), read_into(query.may_calls(), may_calls),
              read_into(query.diagnostics(), diagnostics),
              read_into(query.coverage(), facts.coverage_.counts)}) {
            if (!read) {
                return std::unexpected(read.error());
            }
        }
        facts.file_names_ = handoff::file_names_in_bundle(files, includes);
        for (const facts::ModuleFact& module : modules) {
            facts.modules_.emplace(module.id.value, module.name);
        }
        for (facts::FunctionFact& function : functions) {
            facts.functions_.emplace(function.id.value, std::move(function));
        }
        for (facts::IncludeFact& include : includes) {
            facts.includes_.emplace(include.id.value, std::move(include));
        }
        for (std::vector<facts::CallFact>* list : {&calls, &may_calls}) {
            for (facts::CallFact& call : *list) {
                facts.calls_.emplace(call.id.value, std::move(call));
            }
        }
        for (const facts::DiagnosticFact& diagnostic : diagnostics) {
            if (diagnostic.severity != facts::DiagnosticSeverity::warning) {
                ++facts.coverage_.error_diagnostics;
            }
        }
        return facts;
    }

    [[nodiscard]] const Coverage& coverage() const { return coverage_; }

    /// `reference` as a JSON object of the keys ADR-0030 lists for its table.
    [[nodiscard]] Result<Json> evidence(const FactRef& reference) const {
        Json json{{"table", to_string(reference.table)}};
        bool found = false;
        switch (reference.table) {
        case FactTable::module:
            found = add_module(reference.id, json);
            break;
        case FactTable::file:
            found = add_file(reference.id, json);
            break;
        case FactTable::function:
            found = add_function(reference.id, json);
            break;
        case FactTable::include_edge:
            found = add_include(reference.id, json);
            break;
        case FactTable::call_edge:
            found = add_call(reference.id, json);
            break;
        }
        if (!found) {
            return std::unexpected(
                invalid("evidence names no fact: " + std::string{to_string(reference.table)} + " " +
                        std::to_string(reference.id)));
        }
        return json;
    }

private:
    // Each adds the keys of its table to `json`; false if `id` names no
    // fact of the database (an inconsistent verdict).

    [[nodiscard]] bool add_module(std::int64_t id, Json& json) const {
        const auto found = modules_.find(id);
        if (found == modules_.end()) {
            return false;
        }
        json["module"] = found->second;
        return true;
    }

    [[nodiscard]] bool add_file(std::int64_t id, Json& json) const {
        const std::string* name = file_name(facts::FileId{id});
        if (name == nullptr) {
            return false;
        }
        json["file"] = *name;
        return true;
    }

    [[nodiscard]] bool add_function(std::int64_t id, Json& json) const {
        const facts::FunctionFact* function = function_of(id);
        if (function == nullptr) {
            return false;
        }
        const std::string* file = file_name(function->file);
        if (file == nullptr) {
            return false;
        }
        json["function"] = function->name;
        json["file"] = *file;
        json["line"] = function->line_begin;
        json["definition"] = function->is_definition;
        return true;
    }

    [[nodiscard]] bool add_include(std::int64_t id, Json& json) const {
        const auto found = includes_.find(id);
        if (found == includes_.end()) {
            return false;
        }
        const facts::IncludeFact& include = found->second;
        const std::string* file = file_name(include.includer);
        if (file == nullptr) {
            return false;
        }
        json["file"] = *file;
        json["line"] = include.line;
        const std::string* target =
            include.included.has_value() ? file_name(*include.included) : nullptr;
        json["target"] = target != nullptr ? Json(*target) : Json(nullptr);
        return true;
    }

    [[nodiscard]] bool add_call(std::int64_t id, Json& json) const {
        const auto found = calls_.find(id);
        if (found == calls_.end()) {
            return false;
        }
        const facts::CallFact& call = found->second;
        const facts::FunctionFact* caller = function_of(call.caller.value);
        const std::string* file = file_name(call.site.file);
        if (caller == nullptr || file == nullptr) {
            return false;
        }
        const facts::FunctionFact* callee =
            call.callee.has_value() ? function_of(call.callee->value) : nullptr;
        json["kind"] = to_string(call.kind);
        json["caller"] = caller->name;
        json["callee"] = callee != nullptr ? Json(callee->name) : Json(nullptr);
        json["via"] = call.via;
        json["file"] = *file;
        json["line"] = call.site.line;
        json["column"] = call.site.column;
        return true;
    }

    [[nodiscard]] const std::string* file_name(facts::FileId id) const {
        const auto found = file_names_.find(id);
        return found == file_names_.end() ? nullptr : &found->second.name;
    }

    [[nodiscard]] const facts::FunctionFact* function_of(std::int64_t id) const {
        const auto found = functions_.find(id);
        return found == functions_.end() ? nullptr : &found->second;
    }

    std::map<facts::FileId, facts::ExportedFile> file_names_;
    std::map<std::int64_t, std::string> modules_;
    std::map<std::int64_t, facts::FunctionFact> functions_;
    std::map<std::int64_t, facts::IncludeFact> includes_;
    std::map<std::int64_t, facts::CallFact> calls_;
    Coverage coverage_;
};

Json entity_json(const claims::EntityRef& entity) {
    return Json{{"type", to_string(entity.type)},
                {"name", entity.name},
                {"file", entity.file ? Json(*entity.file) : Json(nullptr)}};
}

/// One claim, its verdict and its evidence: the input to both formats.
struct Entry {
    const claims::Claim* claim;
    const verify::Verdict* verdict;
    Json evidence; ///< Array, in the verdict's order.
};

struct Counts {
    std::map<ClaimKind, std::map<VerificationStatus, std::size_t>> by_kind;

    [[nodiscard]] std::size_t of(ClaimKind kind, VerificationStatus status) const {
        const auto found = by_kind.find(kind);
        if (found == by_kind.end()) {
            return 0;
        }
        const auto count = found->second.find(status);
        return count == found->second.end() ? 0 : count->second;
    }

    [[nodiscard]] std::size_t of(VerificationStatus status) const {
        std::size_t total = 0;
        for (const ClaimKind kind : kinds) {
            total += of(kind, status);
        }
        return total;
    }
};

Json status_counts_json(const auto& count_of) {
    Json json = Json::object();
    for (const VerificationStatus status : statuses) {
        json[std::string{verify::to_string(status)}] = count_of(status);
    }
    return json;
}

std::string json_report(const VerificationReport& report, const std::vector<Entry>& entries,
                        const Counts& counts, const Coverage& coverage) {
    Json by_kind = Json::object();
    for (const ClaimKind kind : kinds) {
        by_kind[std::string{to_string(kind)}] = status_counts_json(
            [&counts, kind](VerificationStatus status) { return counts.of(kind, status); });
    }
    Json claims = Json::array();
    for (const Entry& entry : entries) {
        const claims::Claim& claim = *entry.claim;
        claims.push_back(Json{{"id", claim.id},
                              {"kind", to_string(claim.kind)},
                              {"polarity", to_string(claim.polarity)},
                              {"subject", entity_json(claim.subject)},
                              {"object", entity_json(claim.object)},
                              {"text", claim.text},
                              {"source", Json{{"document", claim.source.document},
                                              {"section", claim.source.section}}},
                              {"status", verify::to_string(entry.verdict->status)},
                              {"reason", to_string(entry.verdict->reason)},
                              {"evidence", entry.evidence}});
    }
    const Json json{
        {"format", report_format},
        {"format_version", report_format_version},
        {"tool_version", common::version},
        {"fact_schema_version", facts::schema_version},
        {"handoff_contract_version",
         report.handoff_contract_version ? Json(*report.handoff_contract_version) : Json(nullptr)},
        {"agent", report.agent},
        {"coverage", Json{{"translation_units_total", coverage.counts.translation_units_total},
                          {"translation_units_parsed", coverage.counts.translation_units_parsed},
                          {"complete", coverage.complete()},
                          {"error_diagnostics", coverage.error_diagnostics}}},
        {"counts", Json{{"by_status", status_counts_json([&counts](VerificationStatus status) {
                             return counts.of(status);
                         })},
                        {"by_kind", std::move(by_kind)}}},
        {"claims", std::move(claims)}};
    constexpr int indentation = 2;
    // Agent text and analysed names are untrusted: invalid UTF-8 becomes
    // U+FFFD rather than an exception (ADR-0028's JSON rule).
    return json.dump(indentation, ' ', false, Json::error_handler_t::replace) + '\n';
}

std::string entity_markdown(const claims::EntityRef& entity) {
    std::string text = std::string{to_string(entity.type)} + ' ' + code_span(entity.name);
    if (entity.file) {
        text += " in " + code_span(*entity.file);
    }
    return text;
}

std::string location(const Json& evidence, bool with_column) {
    std::string text = evidence.at("file").get<std::string>() + ':' +
                       std::to_string(evidence.at("line").get<std::uint32_t>());
    if (with_column) {
        text += ':' + std::to_string(evidence.at("column").get<std::uint32_t>());
    }
    return code_span(text);
}

/// One evidence item as a line a reader can follow to the code.
std::string evidence_markdown(const Json& evidence) {
    const std::string table = evidence.at("table").get<std::string>();
    const auto text = [&evidence](const char* key) {
        return code_span(evidence.at(key).get<std::string>());
    };
    if (table == "module") {
        return "module " + text("module");
    }
    if (table == "file") {
        return "file " + text("file");
    }
    if (table == "function") {
        return location(evidence, false) + " function " + text("function") +
               (evidence.at("definition").get<bool>() ? " (definition)" : " (declaration)");
    }
    if (table == "include_edge") {
        return location(evidence, false) + " include of " +
               (evidence.at("target").is_null() ? std::string{"an unresolved file"}
                                                : text("target"));
    }
    const std::string kind = evidence.at("kind").get<std::string>();
    // A may_call edge is a candidate, never a call (ADR-0026).
    std::string line = location(evidence, true) + ' ' +
                       (kind == "may_call" ? "`may_call` edge" : kind + " call") + " from " +
                       text("caller");
    if (!evidence.at("callee").is_null()) {
        line += " to " + text("callee");
    }
    if (!evidence.at("via").get<std::string>().empty()) {
        line += " via " + text("via");
    }
    return line;
}

void claims_markdown(std::string& doc, const std::vector<Entry>& entries,
                     VerificationStatus status) {
    bool any = false;
    for (const Entry& entry : entries) {
        if (entry.verdict->status != status) {
            continue;
        }
        any = true;
        const claims::Claim& claim = *entry.claim;
        doc += "\n### " + code_span(claim.id) + "\n\n";
        doc += "- Claim (" + code_span(to_string(claim.kind)) + ", " +
               std::string{to_string(claim.polarity)} + "): " + entity_markdown(claim.subject) +
               " → " + entity_markdown(claim.object) + '\n';
        doc += "- Text: " + code_span(claim.text) + '\n';
        if (!claim.source.document.empty() || !claim.source.section.empty()) {
            doc += "- Source: " + code_span(claim.source.document) + ", section " +
                   code_span(claim.source.section) + '\n';
        }
        doc += "- Reason: " + code_span(to_string(entry.verdict->reason)) + '\n';
        if (entry.evidence.empty()) {
            doc += entry.verdict->reason == VerdictReason::entity_not_found
                       ? "- Evidence: no fact to cite; the coverage below is the evidence\n"
                       : "- Evidence: none\n";
            continue;
        }
        doc += "- Evidence:\n";
        for (const Json& evidence : entry.evidence) {
            doc += "  - " + evidence_markdown(evidence) + '\n';
        }
    }
    if (!any) {
        doc += "\nNone.\n";
    }
}

std::string markdown_report(const VerificationReport& report, const std::vector<Entry>& entries,
                            const Counts& counts, const Coverage& coverage) {
    std::string doc = "# Verification report\n\n| Item | Value |\n|---|---|\n";
    doc += "| Tool version | " + code_span(common::version) + " |\n";
    doc += "| Fact schema version | " + std::to_string(facts::schema_version) + " |\n";
    doc += "| Handoff contract version | " +
           (report.handoff_contract_version ? std::to_string(*report.handoff_contract_version)
                                            : std::string{"none"}) +
           " |\n";
    doc += "| Agent | " + (report.agent.empty() ? std::string{"none"} : code_span(report.agent)) +
           " |\n";

    doc += "\n## Counts\n\n| Kind | confirmed | unverified | contradicted | total |\n"
           "|---|---|---|---|---|\n";
    const auto row = [&doc](const std::string& label, const auto& count_of) {
        std::size_t total = 0;
        doc += "| " + label + " |";
        for (const VerificationStatus status : statuses) {
            total += count_of(status);
            doc += ' ' + std::to_string(count_of(status)) + " |";
        }
        doc += ' ' + std::to_string(total) + " |\n";
    };
    for (const ClaimKind kind : kinds) {
        row(code_span(to_string(kind)),
            [&counts, kind](VerificationStatus status) { return counts.of(kind, status); });
    }
    row("all", [&counts](VerificationStatus status) { return counts.of(status); });

    doc += "\n## Contradicted claims\n";
    claims_markdown(doc, entries, VerificationStatus::contradicted);
    doc += "\n## Unverified claims\n";
    claims_markdown(doc, entries, VerificationStatus::unverified);

    doc += "\n## Coverage\n\n" + std::to_string(coverage.counts.translation_units_parsed) + " of " +
           std::to_string(coverage.counts.translation_units_total) +
           " translation units parsed: coverage " +
           (coverage.complete() ? "complete" : "incomplete") + ". " +
           std::to_string(coverage.error_diagnostics) +
           " error diagnostics; with any, an absent `calls`, `includes` or `defines` relation "
           "is unverified.\n";
    return doc;
}

} // namespace

common::Status write_verification_report(const VerificationReport& report,
                                         const facts::FactQuery& facts, ReportFormat format,
                                         std::ostream& out) {
    if (report.claims.size() != report.verdicts.size()) {
        return std::unexpected(invalid("the report has " + std::to_string(report.claims.size()) +
                                       " claims but " + std::to_string(report.verdicts.size()) +
                                       " verdicts"));
    }
    auto read = Facts::read(facts);
    if (!read) {
        return std::unexpected(read.error());
    }
    std::vector<Entry> entries;
    Counts counts;
    for (std::size_t i = 0; i < report.claims.size(); ++i) {
        const claims::Claim& claim = report.claims[i];
        const verify::Verdict& verdict = report.verdicts[i];
        if (verdict.claim_id != claim.id) {
            return std::unexpected(
                invalid("verdict " + verdict.claim_id + " is not for claim " + claim.id));
        }
        Json evidence = Json::array();
        for (const FactRef& reference : verdict.evidence) {
            auto item = read->evidence(reference);
            if (!item) {
                return std::unexpected(item.error());
            }
            evidence.push_back(std::move(*item));
        }
        ++counts.by_kind[claim.kind][verdict.status];
        entries.push_back({.claim = &claim, .verdict = &verdict, .evidence = std::move(evidence)});
    }
    switch (format) {
    case ReportFormat::json:
        out << json_report(report, entries, counts, read->coverage());
        break;
    case ReportFormat::markdown:
        out << markdown_report(report, entries, counts, read->coverage());
        break;
    }
    if (!out) {
        return std::unexpected(
            Error{.code = ErrorCode::io_error, .message = "cannot write the verification report"});
    }
    return {};
}

} // namespace lcart::report
