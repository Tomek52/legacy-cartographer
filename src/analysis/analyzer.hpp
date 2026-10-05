#pragma once

#include "analysis/module_rules.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lcart::analysis {

/// Largest AnalysisRequest::jobs (ADR-0019).
inline constexpr unsigned max_jobs = 256;

/// What an analysis reports while it runs (ADR-0019). Never part of the
/// facts (NFR-004).
struct AnalysisProgress {
    std::uint32_t parsed{};              ///< Parsed in this run.
    std::uint32_t failed{};              ///< Failed in this run.
    std::uint32_t already_done{};        ///< Parsed or failed by an earlier, interrupted run.
    std::uint32_t total{};               ///< Translation units of the whole analysis.
    std::chrono::milliseconds elapsed{}; ///< Wall time of this run so far.
    /// elapsed / (parsed + failed) * (translation units left); empty until
    /// a translation unit of this run is done.
    std::optional<std::chrono::milliseconds> remaining{};
    /// The last report of the run: it completed or was interrupted.
    bool finished{};
};

struct AnalysisRequest {
    /// compile_commands.json of the analysed project. Compile commands are
    /// parsed as arguments for the Clang frontend, never executed
    /// (docs/security.md).
    std::filesystem::path compilation_database;
    /// Must be an existing directory. Files under it are stored with paths
    /// relative to it; files outside it with absolute paths.
    std::filesystem::path root;
    /// Translation units whose main file is under the root and matches one
    /// of these are skipped: not analysed and not counted in the coverage.
    /// Headers they match are still recorded when an analysed unit includes
    /// them.
    std::vector<config::PathGlob> exclude_globs;
    /// Files under the root belong to the module of the first rule with a
    /// matching glob, else to default_module() (FR-005).
    ModuleRules module_rules;
    /// Worker threads that parse translation units, 1 to max_jobs. The facts
    /// are the same for any number (NFR-004, ADR-0019).
    unsigned jobs{1};
    /// Continue the interrupted analysis whose facts the AnalysisStore
    /// holds instead of starting a new one. Fails, naming what differs, if
    /// the compilation database, lcart version or configuration (root,
    /// module rules, exclusions, translation units) differ from that run.
    bool resume{};
    /// Once this holds true no new translation unit is started; units that
    /// are being parsed are finished, those next in order committed, and
    /// analyze() returns with AnalysisSummary::interrupted. Meant to be set
    /// from a signal handler. May be null.
    const std::atomic<bool>* stop{};
    /// Called on the thread that called analyze(), at least every
    /// progress_interval while it runs and once at the end. May be empty.
    std::function<void(const AnalysisProgress&)> on_progress{};
    std::chrono::milliseconds progress_interval{std::chrono::seconds{30}};
    /// The time source of the progress reports, and of nothing else;
    /// std::chrono::steady_clock when empty.
    std::function<std::chrono::steady_clock::time_point()> clock{};
};

struct AnalysisSummary {
    std::uint32_t translation_units_total{};
    /// Parsed in the whole analysis, including an earlier, interrupted run.
    std::uint32_t translation_units_parsed{};
    /// Failed in the whole analysis, including an earlier, interrupted run.
    std::uint32_t translation_units_failed{};
    /// Done (parsed or failed) by an earlier, interrupted run and not
    /// parsed again (ADR-0019).
    std::uint32_t translation_units_already_done{};
    /// Diagnostics of the translation units done in this run.
    std::uint32_t diagnostics{};
    /// Messages for the user, e.g. compiler arguments removed for security
    /// reasons (docs/security.md, T5). In translation-unit order; only those
    /// of the translation units done in this run.
    std::vector<std::string> warnings;
    /// AnalysisRequest::stop ended the run before every translation unit
    /// was done; module dependencies and coverage are not written yet.
    bool interrupted{};
};

/// Extracts facts (files, includes, functions, calls) from a C/C++ project.
///
/// A translation unit that fails to parse is recorded as a diagnostic and
/// the analysis continues (FR-001). Deterministic: the same inputs produce
/// the same facts regardless of the number of workers and of thread
/// scheduling (NFR-004).
class Analyzer {
public:
    Analyzer() = default;
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;
    Analyzer(Analyzer&&) = delete;
    Analyzer& operator=(Analyzer&&) = delete;
    virtual ~Analyzer() = default;

    /// A new analysis written to `out`, without per-translation-unit
    /// transactions; `request.resume` is an error here.
    virtual common::Result<AnalysisSummary> analyze(const AnalysisRequest& request,
                                                    facts::FactWriter& out) = 0;
    /// An analysis that commits each translation unit with its status, and
    /// records in META what a resumed run must find unchanged (ADR-0019).
    /// A new analysis expects an empty store.
    virtual common::Result<AnalysisSummary> analyze(const AnalysisRequest& request,
                                                    facts::AnalysisStore& out) = 0;
};

} // namespace lcart::analysis
