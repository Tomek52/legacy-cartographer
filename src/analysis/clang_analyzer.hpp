#pragma once

#include "analysis/analyzer.hpp"

namespace lcart::analysis {

/// Analyzer backed by Clang LibTooling (ADR-0002, ADR-0007). All Clang types
/// stay in the implementation file, so including this header does not pull
/// in Clang.
///
/// Translation units are analysed one at a time in path order (B-07);
/// parallel workers come with B-16 (docs/architecture/components.md).
/// Response files are expanded and code-loading arguments removed before
/// Clang sees a command (docs/security.md, T5).
class ClangAnalyzer final : public Analyzer {
public:
    common::Result<AnalysisSummary> analyze(const AnalysisRequest& request,
                                            facts::FactWriter& out) override;
};

} // namespace lcart::analysis
