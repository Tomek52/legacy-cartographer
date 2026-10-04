#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lcart::analysis {

struct AnalysisRequest {
    /// compile_commands.json of the analysed project. Compile commands are
    /// parsed as arguments for the Clang frontend, never executed
    /// (docs/security.md).
    std::filesystem::path compilation_database;
    /// Must be an existing directory. Files under it are stored with paths
    /// relative to it; files outside it with absolute paths.
    std::filesystem::path root;
    std::vector<std::string> exclude_globs;
};

struct AnalysisSummary {
    std::uint32_t translation_units_total{};
    std::uint32_t translation_units_parsed{};
    std::uint32_t diagnostics{};
    /// Messages for the user, e.g. compiler arguments removed for security
    /// reasons (docs/security.md, T5). In translation-unit order.
    std::vector<std::string> warnings;
};

/// Extracts facts (files, includes, functions, calls) from a C/C++ project.
///
/// A translation unit that fails to parse is recorded as a diagnostic and
/// the analysis continues (FR-001). Deterministic: the same inputs produce
/// the same facts regardless of thread scheduling (NFR-004).
class Analyzer {
public:
    Analyzer() = default;
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;
    Analyzer(Analyzer&&) = delete;
    Analyzer& operator=(Analyzer&&) = delete;
    virtual ~Analyzer() = default;

    virtual common::Result<AnalysisSummary> analyze(const AnalysisRequest& request,
                                                    facts::FactWriter& out) = 0;
};

} // namespace lcart::analysis
