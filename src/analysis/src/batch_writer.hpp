#pragma once

#include "analysis/module_rules.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "call_fact_collector.hpp"
#include "file_fact_collector.hpp"
#include "function_fact_collector.hpp"
#include "translation_unit_batch.hpp"

namespace lcart::analysis {

/// The writer of an analysis (ADR-0019): replays translation unit batches
/// through the collectors, which take every decision that depends on
/// earlier translation units. Replaying the batches in translation unit
/// order writes the facts a sequential analysis writes, whatever the number
/// of workers that recorded them. Not thread-safe: the writer thread only.
class BatchWriter {
public:
    BatchWriter(ModuleRules rules, facts::FactWriter& out);
    BatchWriter(const BatchWriter&) = delete;
    BatchWriter& operator=(const BatchWriter&) = delete;
    BatchWriter(BatchWriter&&) = delete;
    BatchWriter& operator=(BatchWriter&&) = delete;
    ~BatchWriter() = default;

    /// Takes over the state an earlier run of the same analysis left in
    /// `facts`: recorded files, directives and functions.
    [[nodiscard]] common::Status restore(const facts::FactQuery& facts);

    /// Writes the facts of `batch` and appends its warnings to `warnings`.
    /// Returns the number of diagnostics written.
    [[nodiscard]] common::Result<std::uint32_t> apply(const TranslationUnitBatch& batch,
                                                      std::vector<std::string>& warnings);

private:
    void apply(const AstEvent& ast, const TranslationUnitBatch& batch);

    facts::FactWriter* out_;
    FileFactCollector files_;
    FunctionFactCollector functions_;
    CallFactCollector calls_;
};

} // namespace lcart::analysis
