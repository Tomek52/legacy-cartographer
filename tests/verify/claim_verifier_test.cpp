// Verifies: fr010-verify-claims-v1 (criteria 1 and 2: every rule of the
// claim schema, each possible outcome, against a FactQuery fake; no LLM),
// b27-claim-verifier-v1 (criteria 1-3), claim-entity-resolution-v2,
// claim-relation-rules-v2, claim-negation-rule-v2,
// claim-verification-rules-v2, adr0029-fact-references-v1.
// Criterion 4 of B-27 runs on a real analysis: claim_verifier_analysis_test.cpp.
#include "verify/claim_verifier.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcart::verify {
// Readable failure messages for the verdicts compared below. Internal, yet
// members of the verdict's namespace (inline), so GoogleTest finds them by
// ADL.
inline namespace {

std::ostream& operator<<(std::ostream& out, const FactRef& ref) {
    constexpr std::array<std::string_view, 5> tables{"module", "file", "function", "include_edge",
                                                     "call_edge"};
    return out << tables.at(static_cast<std::size_t>(ref.table)) << ':' << ref.id;
}

std::ostream& operator<<(std::ostream& out, const Verdict& verdict) {
    out << verdict.claim_id << ' ' << to_string(verdict.status) << " / reason "
        << static_cast<int>(verdict.reason) << " [";
    for (const FactRef& ref : verdict.evidence) {
        out << ' ' << ref;
    }
    return out << " ]";
}

} // namespace
} // namespace lcart::verify

namespace {

using lcart::claims::Claim;
using lcart::claims::ClaimKind;
using lcart::claims::EntityRef;
using lcart::claims::EntityType;
using lcart::claims::Polarity;
using lcart::common::Error;
using lcart::common::ErrorCode;
using lcart::common::Result;
using lcart::verify::FactRef;
using lcart::verify::FactTable;
using lcart::verify::Verdict;
using lcart::verify::VerdictReason;
using lcart::verify::VerificationStatus;
using namespace lcart::facts;

/// An in-memory fact database. Sequences come back in insertion order, so
/// a test adds facts in the order the real queries document.
class FakeFacts final : public FactQuery {
public:
    FakeFacts() = default;

    // Builders.

    ModuleId add_module(const std::string& name) {
        modules_.push_back({.id = ModuleId{next_id()}, .name = name});
        return modules_.back().id;
    }
    FileId add_file(const std::string& path, std::optional<ModuleId> module) {
        files_.push_back({.id = FileId{next_id()},
                          .path = path,
                          .is_header = path.ends_with(".h"),
                          .is_external = !module.has_value(),
                          .module = module});
        return files_.back().id;
    }
    FunctionId add_function(const std::string& name, FileId file, bool is_definition = true) {
        functions_.push_back({.id = FunctionId{next_id()},
                              .usr = "c:@F@" + name + "#" + std::to_string(next_id_),
                              .name = name,
                              .qualified_name = name,
                              .file = file,
                              .is_definition = is_definition});
        return functions_.back().id;
    }
    CallId add_call(FunctionId caller, FunctionId callee) {
        return add_edge({.caller = caller, .callee = callee, .kind = CallKind::direct});
    }
    CallId add_indirect_call(FunctionId caller) {
        return add_edge({.caller = caller, .kind = CallKind::indirect, .via = "backend.send"});
    }
    CallId add_may_call(FunctionId caller, FunctionId callee) {
        return add_edge({.caller = caller,
                         .callee = callee,
                         .kind = CallKind::may_call,
                         .via = "backend.send"});
    }
    IncludeId add_include(FileId includer, FileId included) {
        includes_.push_back(
            {.id = IncludeId{next_id()}, .includer = includer, .included = included});
        return includes_.back().id;
    }
    void add_dependency(ModuleId from, ModuleId to, DependencyVia via, std::int64_t edge_id) {
        dependencies_.push_back({.from = from, .to = to, .via = via, .edge_id = edge_id});
    }
    void add_diagnostic(const std::string& translation_unit, const std::string& file,
                        DiagnosticSeverity severity) {
        diagnostics_.push_back(
            {.translation_unit = translation_unit, .file = file, .severity = severity});
    }
    void set_coverage(std::uint32_t total, std::uint32_t parsed) {
        coverage_ = {.translation_units_total = total, .translation_units_parsed = parsed};
    }
    void fail_queries() { failing_ = true; }

    // FactQuery.

    [[nodiscard]] Result<std::optional<ModuleFact>>
    find_module(std::string_view name) const override {
        return first(modules_, [&](const ModuleFact& m) { return m.name == name; });
    }
    [[nodiscard]] Result<std::optional<FileFact>> find_file(std::string_view path) const override {
        return first(files_, [&](const FileFact& f) { return f.path == path; });
    }
    [[nodiscard]] Result<std::optional<FileFact>> file(FileId id) const override {
        return first(files_, [&](const FileFact& f) { return f.id == id; });
    }
    [[nodiscard]] Result<std::vector<ModuleFact>> modules() const override {
        return all(modules_, [](const ModuleFact&) { return true; });
    }
    [[nodiscard]] Result<std::vector<FileFact>> files() const override {
        return all(files_, [](const FileFact&) { return true; });
    }
    [[nodiscard]] Result<std::vector<FunctionFact>>
    find_functions(std::string_view name) const override {
        return all(functions_, [&](const FunctionFact& f) { return f.name == name; });
    }
    [[nodiscard]] Result<std::optional<FunctionFact>>
    find_function_by_usr(std::string_view usr) const override {
        return first(functions_, [&](const FunctionFact& f) { return f.usr == usr; });
    }
    [[nodiscard]] Result<std::optional<FunctionFact>> function(FunctionId id) const override {
        return first(functions_, [&](const FunctionFact& f) { return f.id == id; });
    }
    [[nodiscard]] Result<std::vector<FunctionFact>> functions() const override {
        return all(functions_, [](const FunctionFact&) { return true; });
    }
    [[nodiscard]] Result<std::vector<CallFact>> calls_from(FunctionId caller) const override {
        return all(calls_, [&](const CallFact& c) {
            return c.caller == caller && c.kind != CallKind::may_call;
        });
    }
    [[nodiscard]] Result<std::vector<CallFact>> calls_to(FunctionId callee) const override {
        return all(calls_, [&](const CallFact& c) {
            return c.callee == callee && c.kind == CallKind::direct;
        });
    }
    [[nodiscard]] Result<std::vector<CallFact>> calls() const override {
        return all(calls_, [](const CallFact& c) { return c.kind != CallKind::may_call; });
    }
    [[nodiscard]] Result<std::vector<CallFact>> may_calls_from(FunctionId caller) const override {
        return all(calls_, [&](const CallFact& c) {
            return c.caller == caller && c.kind == CallKind::may_call;
        });
    }
    [[nodiscard]] Result<std::vector<CallFact>> may_calls() const override {
        return all(calls_, [](const CallFact& c) { return c.kind == CallKind::may_call; });
    }
    [[nodiscard]] Result<std::vector<IncludeFact>> includes_of(FileId includer) const override {
        return all(includes_, [&](const IncludeFact& i) { return i.includer == includer; });
    }
    [[nodiscard]] Result<std::vector<IncludeFact>> included_by(FileId included) const override {
        return all(includes_, [&](const IncludeFact& i) { return i.included == included; });
    }
    [[nodiscard]] Result<std::vector<IncludeFact>> includes() const override {
        return all(includes_, [](const IncludeFact&) { return true; });
    }
    [[nodiscard]] Result<std::vector<ModuleFact>>
    module_dependencies(ModuleId module) const override {
        return all(modules_, [&](const ModuleFact& m) {
            return std::ranges::any_of(dependencies_, [&](const ModuleDependencyFact& d) {
                return d.from == module && d.to == m.id;
            });
        });
    }
    [[nodiscard]] Result<std::vector<ModuleDependencyFact>>
    module_dependency_edges(ModuleId module) const override {
        return all(dependencies_, [&](const ModuleDependencyFact& d) { return d.from == module; });
    }
    [[nodiscard]] Result<std::vector<DiagnosticFact>> diagnostics() const override {
        return all(diagnostics_, [](const DiagnosticFact&) { return true; });
    }
    [[nodiscard]] Result<AnalysisCoverage> coverage() const override {
        if (failing_) {
            return std::unexpected(Error{.code = ErrorCode::io_error, .message = "disk gone"});
        }
        return coverage_;
    }

private:
    std::int64_t next_id() { return ++next_id_; }

    CallId add_edge(CallFact call) {
        call.id = CallId{next_id()};
        calls_.push_back(call);
        return call.id;
    }

    template <typename T, typename Pred>
    [[nodiscard]] Result<std::optional<T>> first(const std::vector<T>& rows, Pred pred) const {
        if (failing_) {
            return std::unexpected(Error{.code = ErrorCode::io_error, .message = "disk gone"});
        }
        const auto found = std::ranges::find_if(rows, pred);
        return found == rows.end() ? std::nullopt : std::optional<T>{*found};
    }

    template <typename T, typename Pred>
    [[nodiscard]] Result<std::vector<T>> all(const std::vector<T>& rows, Pred pred) const {
        if (failing_) {
            return std::unexpected(Error{.code = ErrorCode::io_error, .message = "disk gone"});
        }
        std::vector<T> result;
        std::ranges::copy_if(rows, std::back_inserter(result), pred);
        return result;
    }

    // One id sequence for all tables, so a test cannot pass by citing the
    // right id from the wrong table.
    std::int64_t next_id_{};
    std::vector<ModuleFact> modules_;
    std::vector<FileFact> files_;
    std::vector<FunctionFact> functions_;
    std::vector<CallFact> calls_;
    std::vector<IncludeFact> includes_;
    std::vector<ModuleDependencyFact> dependencies_;
    std::vector<DiagnosticFact> diagnostics_;
    AnalysisCoverage coverage_{.translation_units_total = 3, .translation_units_parsed = 3};
    bool failing_{};
};

EntityRef function_named(const std::string& name) {
    return {.type = EntityType::function, .name = name, .file = std::nullopt};
}
EntityRef function_in(const std::string& name, const std::string& file) {
    return {.type = EntityType::function, .name = name, .file = file};
}
EntityRef file_named(const std::string& path) {
    return {.type = EntityType::file, .name = path, .file = std::nullopt};
}
EntityRef module_named(const std::string& name) {
    return {.type = EntityType::module, .name = name, .file = std::nullopt};
}

Claim claim(ClaimKind kind, EntityRef subject, EntityRef object,
            Polarity polarity = Polarity::affirmed) {
    return {.id = "c-0001",
            .kind = kind,
            .subject = std::move(subject),
            .object = std::move(object),
            .polarity = polarity,
            .source = {},
            .text = "a structural statement"};
}

Verdict verify(const Claim& checked, const FactQuery& facts) {
    auto verdict = lcart::verify::verify_claim(checked, facts);
    EXPECT_TRUE(verdict.has_value()) << (verdict ? "" : verdict.error().message);
    return verdict ? std::move(*verdict) : Verdict{};
}

Verdict expected(VerificationStatus status, VerdictReason reason,
                 std::vector<FactRef> evidence = {}) {
    return {
        .claim_id = "c-0001", .status = status, .reason = reason, .evidence = std::move(evidence)};
}

FactRef ref(FunctionId id) {
    return {.table = FactTable::function, .id = id.value};
}
FactRef ref(FileId id) {
    return {.table = FactTable::file, .id = id.value};
}
FactRef ref(ModuleId id) {
    return {.table = FactTable::module, .id = id.value};
}
FactRef ref(CallId id) {
    return {.table = FactTable::call_edge, .id = id.value};
}
FactRef ref(IncludeId id) {
    return {.table = FactTable::include_edge, .id = id.value};
}

constexpr auto confirmed = VerificationStatus::confirmed;
constexpr auto unverified = VerificationStatus::unverified;
constexpr auto contradicted = VerificationStatus::contradicted;

/// A small project: module `core` (src/core.c, src/core.h, src/util.c) and
/// `app` (app/main.c), one external header. `run` and `main` are defined;
/// `init` is defined in src/core.c and declared in src/core.h.
struct Project {
    FakeFacts facts;
    ModuleId core = facts.add_module("core");
    ModuleId app = facts.add_module("app");
    FileId main_c = facts.add_file("app/main.c", app);
    FileId core_c = facts.add_file("src/core.c", core);
    FileId core_h = facts.add_file("src/core.h", core);
    FileId util_c = facts.add_file("src/util.c", core);
    FileId stdio_h = facts.add_file("/usr/include/stdio.h", std::nullopt);
    FunctionId init = facts.add_function("init", core_c);
    FunctionId run = facts.add_function("run", core_c);
    FunctionId main_fn = facts.add_function("main", main_c);
};

// --- Step 1: entity resolution --------------------------------------------

TEST(ClaimVerifierResolution, FunctionByNameResolves) {
    Project p;
    const CallId call = p.facts.add_call(p.main_fn, p.init);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("main"), function_named("init")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(call)}));
}

TEST(ClaimVerifierResolution, FunctionWithSeveralMatchesIsAmbiguous) {
    Project p;
    const FileId lib_util_c = p.facts.add_file("lib/util.c", p.core);
    const FunctionId first = p.facts.add_function("helper", lib_util_c);
    const FunctionId second = p.facts.add_function("helper", p.util_c);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("helper"), function_named("init")), p.facts),
        expected(unverified, VerdictReason::entity_ambiguous, {ref(first), ref(second)}));
}

TEST(ClaimVerifierResolution, FunctionFileSelectsAmongSameName) {
    Project p;
    const FileId lib_helpers_c = p.facts.add_file("lib/helpers.c", p.core);
    p.facts.add_function("helper", lib_helpers_c);
    const FunctionId wanted = p.facts.add_function("helper", p.util_c);
    const CallId call = p.facts.add_call(wanted, p.init);
    // By exact path and by unique suffix.
    for (const std::string file : {"src/util.c", "util.c"}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::calls, function_in("helper", file), function_named("init")),
                   p.facts),
            expected(confirmed, VerdictReason::relation_found, {ref(call)}))
            << file;
    }
}

TEST(ClaimVerifierResolution, FunctionFileMatchingSeveralFilesKeepsThemAll) {
    Project p;
    const FileId lib_util_c = p.facts.add_file("lib/util.c", p.core);
    const FunctionId first = p.facts.add_function("helper", lib_util_c);
    const FunctionId second = p.facts.add_function("helper", p.util_c);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_in("helper", "util.c"), function_named("init")),
               p.facts),
        expected(unverified, VerdictReason::entity_ambiguous, {ref(first), ref(second)}));
}

TEST(ClaimVerifierResolution, FunctionFileLocatingNoneOnlyDisambiguates) {
    Project p;
    // init is located at its definition, src/core.c; its declaration in
    // src/core.h is not recorded, so naming the header still finds it.
    const CallId call = p.facts.add_call(p.run, p.init);
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("run"), function_in("init", "core.h")),
                     p.facts),
              expected(confirmed, VerdictReason::relation_found, {ref(call)}));
}

TEST(ClaimVerifierResolution, FileExactPathWinsOverSuffix) {
    Project p;
    const FileId root_util_c = p.facts.add_file("util.c", p.core);
    // src/util.c also ends in /util.c, but the exact match decides.
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, file_named("util.c"), module_named("core")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(root_util_c)}));
}

TEST(ClaimVerifierResolution, FileByUniqueSuffix) {
    Project p;
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, file_named("core.c"), module_named("core")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(p.core_c)}));
    // Outside the root too: stdio.h is recorded with its absolute path.
    const IncludeId include = p.facts.add_include(p.main_c, p.stdio_h);
    EXPECT_EQ(
        verify(claim(ClaimKind::includes, file_named("main.c"), file_named("stdio.h")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(include)}));
}

TEST(ClaimVerifierResolution, FileSuffixOnlyAtSlash) {
    const Project p;
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, file_named("ore.c"), module_named("core")), p.facts),
        expected(contradicted, VerdictReason::entity_not_found));
}

TEST(ClaimVerifierResolution, FileWithSeveralSuffixMatchesIsAmbiguous) {
    Project p;
    const FileId lib_util_c = p.facts.add_file("lib/util.c", p.core);
    // Candidates in the order of FactQuery::files() (here: insertion).
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, file_named("util.c"), module_named("core")), p.facts),
        expected(unverified, VerdictReason::entity_ambiguous, {ref(p.util_c), ref(lib_util_c)}));
}

TEST(ClaimVerifierResolution, ModuleByExactNameOnly) {
    const Project p;
    EXPECT_EQ(verify(claim(ClaimKind::belongs_to, file_named("app/main.c"), module_named("App")),
                     p.facts),
              expected(contradicted, VerdictReason::entity_not_found));
}

TEST(ClaimVerifierResolution, NotFoundUnderCompleteCoverageIsContradicted) {
    const Project p;
    // No fact to cite: the evidence is the coverage (b27 criterion 3).
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("main"), function_named("imagined")),
                     p.facts),
              expected(contradicted, VerdictReason::entity_not_found));
}

TEST(ClaimVerifierResolution, NotFoundUnderIncompleteCoverageIsUnverified) {
    Project p;
    p.facts.set_coverage(3, 2);
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("main"), function_named("imagined")),
                     p.facts),
              expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierResolution, NotFoundWithoutCoverageIsUnverified) {
    Project p;
    p.facts.set_coverage(0, 0);
    EXPECT_EQ(verify(claim(ClaimKind::depends_on, module_named("app"), module_named("imagined")),
                     p.facts),
              expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierResolution, OutcomesDoNotDependOnPolarity) {
    Project p;
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("main"), function_named("imagined"),
                           Polarity::negated),
                     p.facts),
              expected(contradicted, VerdictReason::entity_not_found));
    p.facts.add_function("main", p.core_c);
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("main"), function_named("init"),
                           Polarity::negated),
                     p.facts)
                  .reason,
              VerdictReason::entity_ambiguous);
}

TEST(ClaimVerifierResolution, SubjectIsResolvedBeforeObject) {
    Project p;
    const FunctionId second_main = p.facts.add_function("main", p.core_c);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("main"), function_named("imagined")),
               p.facts),
        expected(unverified, VerdictReason::entity_ambiguous, {ref(p.main_fn), ref(second_main)}));
}

// --- Step 2: relation rules, one test per cell ----------------------------

TEST(ClaimVerifierCalls, RelationFoundIsConfirmedWithTheCalls) {
    Project p;
    const CallId first = p.facts.add_call(p.run, p.init);
    p.facts.add_indirect_call(p.run);
    const CallId second = p.facts.add_call(p.run, p.init);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(first), ref(second)}));
}

TEST(ClaimVerifierCalls, AbsentWithParsedDefinitionAndNoIndirectCallsIsContradicted) {
    Project p;
    p.facts.add_call(p.main_fn, p.init);
    // Coverage elsewhere does not matter: run's definition was parsed.
    p.facts.set_coverage(3, 2);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts),
        expected(contradicted, VerdictReason::relation_absent, {ref(p.run)}));
}

TEST(ClaimVerifierCalls, AbsentWithIndirectCallsIsUnverified) {
    Project p;
    const CallId indirect = p.facts.add_indirect_call(p.run);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts),
        expected(unverified, VerdictReason::indirect_calls_present, {ref(indirect)}));
}

// Verifies: fr015-resolve-function-pointer-tables-v1 (criterion 2: may_call
// edges never turn a `calls` claim into `confirmed`).
TEST(ClaimVerifierCalls, MayCallEdgeNeverConfirms) {
    Project p;
    // A may_call edge to the object is derived from an indirect call.
    const CallId indirect = p.facts.add_indirect_call(p.run);
    p.facts.add_may_call(p.run, p.main_fn); // to another function: not evidence
    const CallId to_init = p.facts.add_may_call(p.run, p.init);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts),
        expected(unverified, VerdictReason::indirect_calls_present, {ref(indirect), ref(to_init)}));
}

TEST(ClaimVerifierCalls, AbsentWithDeclarationOnlyIsCoverageIncomplete) {
    Project p;
    p.facts.add_function("extern_only", p.core_h, /*is_definition=*/false);
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("extern_only"), function_named("init")),
                     p.facts),
              expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierCalls, AbsentWithDefinitionInFileWithErrorIsCoverageIncomplete) {
    Project p;
    p.facts.add_diagnostic("src/other.c", "src/core.c", DiagnosticSeverity::error);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts),
        expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierCalls, AbsentWithDefinitionInFailedTranslationUnitIsCoverageIncomplete) {
    Project p;
    p.facts.add_diagnostic("src/core.c", "/usr/include/stdio.h", DiagnosticSeverity::fatal);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts),
        expected(unverified, VerdictReason::coverage_incomplete));
}

/// The reviewer's case: main.c includes h.h, which includes h2.h (a fatal
/// error: missing.h) and then h3.h, which defines `k`. Clang reported
/// nothing in h3.h, and dropped k's call to `g` while recovering.
struct FatalInHeader {
    Project p;
    FileId h_h = p.facts.add_file("app/h.h", p.app);
    FileId h2_h = p.facts.add_file("app/h2.h", p.app);
    FileId h3_h = p.facts.add_file("app/h3.h", p.app);
    FunctionId k = p.facts.add_function("k", h3_h);
    FunctionId g = p.facts.add_function("g", p.core_c);

    FatalInHeader() {
        p.facts.add_include(p.main_c, h_h);
        p.facts.add_include(h_h, h2_h);
        p.facts.add_include(h_h, h3_h);
        p.facts.add_diagnostic("app/main.c", "app/h2.h", DiagnosticSeverity::fatal);
        p.facts.set_coverage(3, 2);
    }
};

TEST(ClaimVerifierCalls, AbsentWithDefinitionReachedAfterAFatalErrorIsCoverageIncomplete) {
    const FatalInHeader f;
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::calls, function_named("k"), function_named("g"), polarity),
                   f.p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierCalls, AbsentAfterAFatalErrorInAnyUnitIsCoverageIncomplete) {
    // run is in src/core.c, which the failed unit does not include; after
    // any error in the analysis no definition counts as parsed.
    const FatalInHeader f;
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), f.p.facts),
        expected(unverified, VerdictReason::coverage_incomplete));
}

/// The second review's case: unit main.c includes h1.h, whose declaration
/// of `g` has an unknown parameter type (a non-fatal error in h1.h), and,
/// through h.h, h2.h, which defines `k`. Clang reported nothing in h2.h and
/// dropped k's call to the invalid `g`.
struct ErrorInIncludedHeader {
    Project p;
    FileId h1_h = p.facts.add_file("app/h1.h", p.app);
    FileId h_h = p.facts.add_file("app/h.h", p.app);
    FileId h2_h = p.facts.add_file("app/h2.h", p.app);
    FunctionId k = p.facts.add_function("k", h2_h);
    FunctionId g = p.facts.add_function("g", p.core_c);

    ErrorInIncludedHeader() {
        p.facts.add_include(p.main_c, h1_h);
        p.facts.add_include(p.main_c, h_h);
        p.facts.add_include(h_h, h2_h);
        p.facts.add_diagnostic("app/main.c", "app/h1.h", DiagnosticSeverity::error);
        p.facts.set_coverage(3, 2);
    }
};

TEST(ClaimVerifierCalls, AbsentWithDefinitionReachedByAUnitWithAnErrorIsCoverageIncomplete) {
    const ErrorInIncludedHeader f;
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::calls, function_named("k"), function_named("g"), polarity),
                   f.p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierCalls, AbsentWithAnErrorAnywhereIsCoverageIncomplete) {
    // No recorded edge leads from main.c to src/core.c, but edges cannot
    // bound what a unit with an error reached.
    const ErrorInIncludedHeader f;
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::calls, function_named("run"), function_named("init"), polarity),
                   f.p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierCalls, AbsentWithAnErrorInAUnitOutsideTheRootIsCoverageIncomplete) {
    // A generated main file outside the root: its diagnostic names no unit
    // spelled like a recorded file, and it has no include edges.
    Project p;
    p.facts.add_diagnostic("", "src/core.h", DiagnosticSeverity::error);
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::calls, function_named("run"), function_named("init"), polarity),
                   p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierCalls, AbsentWithAnErrorInAUnitWithoutEdgeToTheSubjectIsCoverageIncomplete) {
    // main.c includes an external wrapper that includes src/core.c's
    // header; directives of external files are not recorded, so no edge.
    Project p;
    p.facts.add_include(p.main_c, p.stdio_h);
    p.facts.add_diagnostic("app/main.c", "app/main.c", DiagnosticSeverity::error);
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::defines, file_named("core.h"), function_named("run"), polarity),
                   p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierIncludes, AbsentWithSubjectReachedByAUnitWithAnErrorIsUnverified) {
    const ErrorInIncludedHeader f;
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::includes, file_named("h2.h"), file_named("h1.h"), polarity),
                   f.p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierCalls, WarningsDoNotMakeADefinitionUnparsed) {
    Project p;
    p.facts.add_diagnostic("src/core.c", "src/core.c", DiagnosticSeverity::warning);
    EXPECT_EQ(
        verify(claim(ClaimKind::calls, function_named("run"), function_named("init")), p.facts)
            .status,
        contradicted);
}

TEST(ClaimVerifierIncludes, RelationFoundIsConfirmedWithTheDirectives) {
    Project p;
    const IncludeId include = p.facts.add_include(p.core_c, p.core_h);
    EXPECT_EQ(
        verify(claim(ClaimKind::includes, file_named("src/core.c"), file_named("core.h")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(include)}));
}

TEST(ClaimVerifierIncludes, AbsentWithParsedSubjectIsContradicted) {
    Project p;
    p.facts.add_include(p.main_c, p.core_h);
    EXPECT_EQ(
        verify(claim(ClaimKind::includes, file_named("src/core.c"), file_named("core.h")), p.facts),
        expected(contradicted, VerdictReason::relation_absent, {ref(p.core_c)}));
}

TEST(ClaimVerifierIncludes, AbsentWithUnparsedSubjectIsUnverified) {
    Project p;
    p.facts.add_diagnostic("src/core.c", "src/core.c", DiagnosticSeverity::fatal);
    EXPECT_EQ(
        verify(claim(ClaimKind::includes, file_named("src/core.c"), file_named("core.h")), p.facts),
        expected(unverified, VerdictReason::coverage_incomplete));
    // A file outside the root: its directives are not recorded.
    EXPECT_EQ(
        verify(claim(ClaimKind::includes, file_named("stdio.h"), file_named("core.h")), p.facts),
        expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierDependsOn, RelationFoundIsConfirmedWithTheEdges) {
    Project p;
    const IncludeId include = p.facts.add_include(p.main_c, p.core_h);
    const CallId call = p.facts.add_call(p.main_fn, p.init);
    p.facts.add_dependency(p.app, p.core, DependencyVia::include, include.value);
    p.facts.add_dependency(p.app, p.core, DependencyVia::call, call.value);
    EXPECT_EQ(
        verify(claim(ClaimKind::depends_on, module_named("app"), module_named("core")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(include), ref(call)}));
}

TEST(ClaimVerifierDependsOn, AbsentUnderCompleteCoverageIsContradicted) {
    const Project p;
    EXPECT_EQ(
        verify(claim(ClaimKind::depends_on, module_named("core"), module_named("app")), p.facts),
        expected(contradicted, VerdictReason::relation_absent, {ref(p.core)}));
}

TEST(ClaimVerifierDependsOn, AbsentUnderIncompleteCoverageIsUnverified) {
    Project p;
    p.facts.set_coverage(3, 2);
    EXPECT_EQ(
        verify(claim(ClaimKind::depends_on, module_named("core"), module_named("app")), p.facts),
        expected(unverified, VerdictReason::coverage_incomplete));
}

// Verifies: fr015-resolve-function-pointer-tables-v1 (criterion 2: may_call
// edges are reported apart from direct calls and confirm nothing).
TEST(ClaimVerifierDependsOn, AbsentWithMayCallEdgesIsUnverified) {
    Project p;
    // main (module app) may reach init (core) through a table only.
    p.facts.add_indirect_call(p.main_fn);
    const CallId may_call = p.facts.add_may_call(p.main_fn, p.init);
    p.facts.add_may_call(p.main_fn, p.main_fn); // within app: not evidence
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(verify(claim(ClaimKind::depends_on, module_named("app"), module_named("core"),
                               polarity),
                         p.facts),
                  expected(unverified, VerdictReason::indirect_calls_present, {ref(may_call)}));
    }
    // Ahead of the coverage, which says nothing about pointers.
    p.facts.set_coverage(3, 2);
    EXPECT_EQ(
        verify(claim(ClaimKind::depends_on, module_named("app"), module_named("core")), p.facts)
            .reason,
        VerdictReason::indirect_calls_present);
}

TEST(ClaimVerifierDefines, RelationFoundIsConfirmedWithTheFunction) {
    const Project p;
    EXPECT_EQ(
        verify(claim(ClaimKind::defines, file_named("core.c"), function_named("init")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(p.init)}));
}

TEST(ClaimVerifierDefines, AbsentWithParsedSubjectIsContradicted) {
    Project p;
    // Defined elsewhere, and declared only.
    p.facts.add_function("extern_only", p.core_h, /*is_definition=*/false);
    for (const std::string function : {"main", "extern_only"}) {
        EXPECT_EQ(verify(claim(ClaimKind::defines, file_named("core.h"), function_named(function)),
                         p.facts),
                  expected(contradicted, VerdictReason::relation_absent, {ref(p.core_h)}))
            << function;
    }
}

TEST(ClaimVerifierDefines, AbsentWithUnparsedSubjectIsUnverified) {
    Project p;
    p.facts.add_function("extern_only", p.core_h, /*is_definition=*/false);
    p.facts.add_diagnostic("src/core.c", "src/core.c", DiagnosticSeverity::fatal);
    EXPECT_EQ(verify(claim(ClaimKind::defines, file_named("core.c"), function_named("extern_only")),
                     p.facts),
              expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierDefines, AbsentAfterAFatalErrorIsUnverified) {
    const FatalInHeader f;
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::defines, file_named("h3.h"), function_named("g"), polarity),
                   f.p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierIncludes, AbsentAfterAFatalErrorIsUnverified) {
    const FatalInHeader f;
    for (const Polarity polarity : {Polarity::affirmed, Polarity::negated}) {
        EXPECT_EQ(
            verify(claim(ClaimKind::includes, file_named("h3.h"), file_named("h2.h"), polarity),
                   f.p.facts),
            expected(unverified, VerdictReason::coverage_incomplete));
    }
}

TEST(ClaimVerifierBelongsTo, FileRelationFoundIsConfirmed) {
    const Project p;
    EXPECT_EQ(verify(claim(ClaimKind::belongs_to, file_named("app/main.c"), module_named("app")),
                     p.facts),
              expected(confirmed, VerdictReason::relation_found, {ref(p.main_c)}));
}

TEST(ClaimVerifierBelongsTo, FunctionRelationFoundIsConfirmedWithItsFile) {
    const Project p;
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, function_named("run"), module_named("core")), p.facts),
        expected(confirmed, VerdictReason::relation_found, {ref(p.run), ref(p.core_c)}));
}

TEST(ClaimVerifierBelongsTo, AbsentIsContradicted) {
    Project p;
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, function_named("run"), module_named("app")), p.facts),
        expected(contradicted, VerdictReason::relation_absent, {ref(p.run), ref(p.core_c)}));
    // A file outside the root belongs to no module, even under incomplete
    // coverage.
    p.facts.set_coverage(3, 2);
    EXPECT_EQ(
        verify(claim(ClaimKind::belongs_to, file_named("stdio.h"), module_named("core")), p.facts),
        expected(contradicted, VerdictReason::relation_absent, {ref(p.stdio_h)}));
}

// --- Step 3: negation -----------------------------------------------------

TEST(ClaimVerifierNegation, RelationFoundIsContradicted) {
    Project p;
    const CallId call = p.facts.add_call(p.run, p.init);
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("run"), function_named("init"),
                           Polarity::negated),
                     p.facts),
              expected(contradicted, VerdictReason::relation_found, {ref(call)}));
}

TEST(ClaimVerifierNegation, RelationAbsentIsConfirmed) {
    const Project p;
    EXPECT_EQ(verify(claim(ClaimKind::includes, file_named("core.c"), file_named("stdio.h"),
                           Polarity::negated),
                     p.facts),
              expected(confirmed, VerdictReason::relation_absent, {ref(p.core_c)}));
}

TEST(ClaimVerifierNegation, UnverifiedStays) {
    Project p;
    p.facts.set_coverage(3, 2);
    EXPECT_EQ(verify(claim(ClaimKind::depends_on, module_named("core"), module_named("app"),
                           Polarity::negated),
                     p.facts),
              expected(unverified, VerdictReason::coverage_incomplete));
}

TEST(ClaimVerifierNegation, CallsWithIndirectCallsStaysUnverified) {
    Project p;
    const CallId indirect = p.facts.add_indirect_call(p.run);
    EXPECT_EQ(verify(claim(ClaimKind::calls, function_named("run"), function_named("init"),
                           Polarity::negated),
                     p.facts),
              expected(unverified, VerdictReason::indirect_calls_present, {ref(indirect)}));
}

/// The affirmed claim gets `affirmed_status`; negated, it gets the same
/// verdict with confirmed and contradicted swapped.
void expect_negation_swaps(Claim claimed, VerificationStatus affirmed_status,
                           const FactQuery& facts) {
    const Verdict affirmed = verify(claimed, facts);
    EXPECT_EQ(affirmed.status, affirmed_status) << affirmed;
    Verdict swapped = affirmed;
    swapped.status = affirmed_status == confirmed ? contradicted : confirmed;
    claimed.polarity = Polarity::negated;
    EXPECT_EQ(verify(claimed, facts), swapped);
}

TEST(ClaimVerifierNegation, SwapsForEveryKind) {
    Project p;
    p.facts.add_include(p.main_c, p.core_h);
    const CallId call = p.facts.add_call(p.main_fn, p.init);
    p.facts.add_dependency(p.app, p.core, DependencyVia::call, call.value);
    const std::vector<std::pair<Claim, Claim>> found_and_absent{
        {claim(ClaimKind::calls, function_named("main"), function_named("init")),
         claim(ClaimKind::calls, function_named("init"), function_named("main"))},
        {claim(ClaimKind::includes, file_named("main.c"), file_named("core.h")),
         claim(ClaimKind::includes, file_named("core.c"), file_named("core.h"))},
        {claim(ClaimKind::depends_on, module_named("app"), module_named("core")),
         claim(ClaimKind::depends_on, module_named("core"), module_named("app"))},
        {claim(ClaimKind::defines, file_named("main.c"), function_named("main")),
         claim(ClaimKind::defines, file_named("main.c"), function_named("run"))},
        {claim(ClaimKind::belongs_to, function_named("main"), module_named("app")),
         claim(ClaimKind::belongs_to, function_named("main"), module_named("core"))},
    };
    for (const auto& [found, absent] : found_and_absent) {
        expect_negation_swaps(found, confirmed, p.facts);
        expect_negation_swaps(absent, contradicted, p.facts);
    }
}

// --- Contract -------------------------------------------------------------

TEST(ClaimVerifier, KeepsTheClaimId) {
    const Project p;
    Claim checked = claim(ClaimKind::belongs_to, file_named("main.c"), module_named("app"));
    checked.id = "c-004711";
    EXPECT_EQ(verify(checked, p.facts).claim_id, "c-004711");
}

TEST(ClaimVerifier, EntityTypeNotFittingTheKindIsAnError) {
    const Project p;
    const auto verdict = lcart::verify::verify_claim(
        claim(ClaimKind::calls, file_named("main.c"), function_named("init")), p.facts);
    ASSERT_FALSE(verdict.has_value());
    EXPECT_EQ(verdict.error().code, ErrorCode::invalid_argument);
}

TEST(ClaimVerifier, FunctionWithoutRecordedFileIsAnError) {
    // An inconsistent database: a verdict built on it would be invented.
    Project p;
    p.facts.add_function("orphan", FileId{999});
    for (const Claim& checked :
         {claim(ClaimKind::belongs_to, function_named("orphan"), module_named("core")),
          claim(ClaimKind::calls, function_named("orphan"), function_named("init"))}) {
        const auto verdict = lcart::verify::verify_claim(checked, p.facts);
        ASSERT_FALSE(verdict.has_value()) << verdict.value();
        EXPECT_EQ(verdict.error().code, ErrorCode::not_found);
    }
}

TEST(ClaimVerifier, QueryErrorsArePassedOn) {
    Project p;
    p.facts.fail_queries();
    const auto verdict = lcart::verify::verify_claim(
        claim(ClaimKind::calls, function_named("main"), function_named("init")), p.facts);
    ASSERT_FALSE(verdict.has_value());
    EXPECT_EQ(verdict.error().code, ErrorCode::io_error);
}

} // namespace
