#pragma once

#include "analysis/analyzer.hpp"

namespace lcart::analysis {

/// Analyzer backed by Clang LibTooling (ADR-0002, ADR-0007). All Clang types
/// stay in the implementation file, so including this header does not pull
/// in Clang.
///
/// Workers parse translation units in parallel; one writer, the calling
/// thread, applies their facts in path order (ADR-0019). Response files are
/// expanded and code-loading arguments removed before Clang sees a command
/// (docs/security.md, T5).
class ClangAnalyzer final : public Analyzer {
public:
    common::Result<AnalysisSummary> analyze(const AnalysisRequest& request,
                                            facts::FactWriter& out) override;
    common::Result<AnalysisSummary> analyze(const AnalysisRequest& request,
                                            facts::AnalysisStore& out) override;
};

} // namespace lcart::analysis
