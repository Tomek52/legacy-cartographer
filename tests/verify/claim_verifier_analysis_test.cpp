// Verifies: b27-claim-verifier-v1 (criterion 4: failed translation units
// turn entity_not_found into unverified / coverage_incomplete, on the
// broken_tu fixture's database; criterion 2 on fn_pointers), through the
// real SqliteFactStore after analysis; claim-entity-resolution-v2 and
// claim-relation-rules-v2 ("was parsed").
#include "analysis/clang_analyzer.hpp"
#include "facts/sqlite_fact_store.hpp"
#include "verify/claim_verifier.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::ClangAnalyzer;
using lcart::claims::Claim;
using lcart::claims::ClaimKind;
using lcart::claims::EntityType;
using lcart::claims::Polarity;
using lcart::verify::FactTable;
using lcart::verify::VerdictReason;
using lcart::verify::VerificationStatus;
using namespace lcart::facts;

template <typename T>
T value_of(lcart::common::Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

class ClaimVerifierAnalysisTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-verify-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    /// Analyses `files` of fixture `name` into a new in-memory store, each
    /// compiled with `include` on the include path.
    [[nodiscard]] std::unique_ptr<SqliteFactStore> analyze(const std::string& name,
                                                           const std::vector<std::string>& files,
                                                           const std::string& include) const {
        const fs::path root = fs::path{LCART_FIXTURES_DIR} / name;
        const fs::path database = work_dir_ / "compile_commands.json";
        {
            std::ofstream out{database};
            out << "[\n";
            for (std::size_t i = 0; i < files.size(); ++i) {
                out << R"(  {"directory": ")" << root.string() << R"(", "command": "clang -c )"
                    << include << ' ' << files[i] << R"(", "file": ")" << files[i] << "\"}"
                    << (i + 1 < files.size() ? ",\n" : "\n");
            }
            out << "]\n";
        }
        auto store = SqliteFactStore::open(":memory:");
        EXPECT_TRUE(store.has_value()) << (store ? "" : store.error().message);
        if (!store) {
            return nullptr;
        }
        ClangAnalyzer analyzer;
        auto summary = analyzer.analyze({.compilation_database = database,
                                         .root = root,
                                         .exclude_globs = {},
                                         .module_rules = {}},
                                        **store);
        EXPECT_TRUE(summary.has_value()) << (summary ? "" : summary.error().message);
        return std::move(*store);
    }

private:
    fs::path work_dir_;
};

Claim calls(const std::string& caller, const std::string& callee) {
    return {.id = "c-0001",
            .kind = ClaimKind::calls,
            .subject = {.type = EntityType::function, .name = caller, .file = std::nullopt},
            .object = {.type = EntityType::function, .name = callee, .file = std::nullopt},
            .polarity = Polarity::affirmed,
            .source = {},
            .text = caller + " calls " + callee};
}

TEST_F(ClaimVerifierAnalysisTest, FailedTranslationUnitTurnsNotFoundIntoUnverified) {
    const auto store = analyze("broken_tu", {"ok.c", "broken.c", "also_ok.c"}, "-Iinclude");
    ASSERT_NE(store, nullptr);
    const AnalysisCoverage coverage = value_of(store->coverage());
    ASSERT_EQ(coverage.translation_units_total, 3U);
    ASSERT_EQ(coverage.translation_units_parsed, 2U); // broken.c failed

    // The name may be in the part that was not parsed.
    const auto missing = value_of(lcart::verify::verify_claim(calls("main", "imagined"), *store));
    EXPECT_EQ(missing.status, VerificationStatus::unverified);
    EXPECT_EQ(missing.reason, VerdictReason::coverage_incomplete);
    EXPECT_TRUE(missing.evidence.empty());

    // A definition in the failed unit may have lost its calls.
    const auto in_broken = value_of(lcart::verify::verify_claim(calls("broken", "main"), *store));
    EXPECT_EQ(in_broken.status, VerificationStatus::unverified);
    EXPECT_EQ(in_broken.reason, VerdictReason::coverage_incomplete);

    // Definitions that were parsed are still checked.
    const auto found = value_of(lcart::verify::verify_claim(calls("main", "util_length"), *store));
    EXPECT_EQ(found.status, VerificationStatus::confirmed);
    ASSERT_EQ(found.evidence.size(), 1U);
    EXPECT_EQ(found.evidence[0].table, FactTable::call_edge);
    const std::vector<CallFact> from_main =
        value_of(store->calls_from(value_of(store->find_functions("main")).at(0).id));
    ASSERT_EQ(from_main.size(), 1U);
    EXPECT_EQ(found.evidence[0].id, from_main[0].id.value);

    const auto absent = value_of(lcart::verify::verify_claim(calls("also_ok", "main"), *store));
    EXPECT_EQ(absent.status, VerificationStatus::contradicted);
    EXPECT_EQ(absent.reason, VerdictReason::relation_absent);
}

// The facts of the fatal_nested README, which the next test relies on: k is
// defined in h3.h, its call to g was dropped, and the only diagnostic is
// the fatal one in h2.h.
TEST_F(ClaimVerifierAnalysisTest, FatalNestedFixtureHidesADroppedCall) {
    const auto store = analyze("fatal_nested", {"main.c", "other.c"}, "-I.");
    ASSERT_NE(store, nullptr);
    const std::vector<FunctionFact> k = value_of(store->find_functions("k"));
    ASSERT_EQ(k.size(), 1U);
    EXPECT_TRUE(k[0].is_definition);
    EXPECT_TRUE(value_of(store->calls_from(k[0].id)).empty());
    const std::vector<DiagnosticFact> diagnostics = value_of(store->diagnostics());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].severity, DiagnosticSeverity::fatal);
    EXPECT_EQ(diagnostics[0].file, "h2.h");
}

void expect_coverage_incomplete(const Claim& claim, const FactQuery& facts) {
    const auto verdict = value_of(lcart::verify::verify_claim(claim, facts));
    EXPECT_EQ(verdict.status, VerificationStatus::unverified) << claim.text;
    EXPECT_EQ(verdict.reason, VerdictReason::coverage_incomplete) << claim.text;
}

// Verifies: claim-relation-rules-v2 (a fatal error anywhere in the
// analysis: no file counts as parsed), on the fatal_nested fixture.
TEST_F(ClaimVerifierAnalysisTest, NoFalseVerdictAfterAFatalError) {
    const auto store = analyze("fatal_nested", {"main.c", "other.c"}, "-I.");
    ASSERT_NE(store, nullptr);
    Claim does_not_call = calls("k", "g");
    does_not_call.polarity = Polarity::negated;
    for (const Claim& claim : {calls("k", "g"), does_not_call, calls("unrelated", "g")}) {
        expect_coverage_incomplete(claim, *store);
    }
}

// The facts of the recovery_expr README, which the next test relies on: k
// is defined in h2.h with no calls, and the only diagnostic is a non-fatal
// error in h1.h.
TEST_F(ClaimVerifierAnalysisTest, RecoveryExprFixtureHidesADroppedCall) {
    const auto store = analyze("recovery_expr", {"main.c", "other.c"}, "-I.");
    ASSERT_NE(store, nullptr);
    const std::vector<FunctionFact> k = value_of(store->find_functions("k"));
    ASSERT_EQ(k.size(), 1U);
    EXPECT_TRUE(k[0].is_definition);
    EXPECT_TRUE(value_of(store->calls_from(k[0].id)).empty());
    const std::vector<DiagnosticFact> diagnostics = value_of(store->diagnostics());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].severity, DiagnosticSeverity::error);
    EXPECT_EQ(diagnostics[0].file, "h1.h");
    EXPECT_EQ(diagnostics[0].translation_unit, "main.c");
}

// Verifies: claim-relation-rules-v2 (a unit with an error reaches the file
// through its include edges: not parsed), on the recovery_expr fixture.
TEST_F(ClaimVerifierAnalysisTest, NoFalseVerdictAfterAnErrorInAnIncludedHeader) {
    const auto store = analyze("recovery_expr", {"main.c", "other.c"}, "-I.");
    ASSERT_NE(store, nullptr);
    Claim does_not_call = calls("k", "g");
    does_not_call.polarity = Polarity::negated;
    for (const Claim& claim : {calls("k", "g"), does_not_call}) {
        expect_coverage_incomplete(claim, *store);
    }
    // other.c is not reached by main.c: still checked.
    const auto clean = value_of(lcart::verify::verify_claim(calls("other", "g"), *store));
    EXPECT_EQ(clean.status, VerificationStatus::contradicted);
}

TEST_F(ClaimVerifierAnalysisTest, NotFoundUnderCompleteCoverageIsContradicted) {
    const auto store = analyze("broken_tu", {"ok.c", "also_ok.c"}, "-Iinclude");
    ASSERT_NE(store, nullptr);
    const auto missing = value_of(lcart::verify::verify_claim(calls("main", "imagined"), *store));
    EXPECT_EQ(missing.status, VerificationStatus::contradicted);
    EXPECT_EQ(missing.reason, VerdictReason::entity_not_found);
    EXPECT_TRUE(missing.evidence.empty());
}

TEST_F(ClaimVerifierAnalysisTest, MayCallEdgesMakeAnAbsentCallUnverified) {
    const auto store = analyze("fn_pointers", {"core.c", "rtu.c", "tcp.c"}, "-I.");
    ASSERT_NE(store, nullptr);
    // transfer reaches rtu_send only through backend.send.
    const auto through_table =
        value_of(lcart::verify::verify_claim(calls("transfer", "rtu_send"), *store));
    EXPECT_EQ(through_table.status, VerificationStatus::unverified);
    EXPECT_EQ(through_table.reason, VerdictReason::indirect_calls_present);
    // Two indirect calls, then the may_call edge to rtu_send.
    EXPECT_EQ(through_table.evidence.size(), 3U);

    const auto direct =
        value_of(lcart::verify::verify_claim(calls("rtu_send", "checksum"), *store));
    EXPECT_EQ(direct.status, VerificationStatus::confirmed);
    const auto none = value_of(lcart::verify::verify_claim(calls("checksum", "rtu_send"), *store));
    EXPECT_EQ(none.status, VerificationStatus::contradicted);
}

} // namespace
