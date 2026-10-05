#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <span>

#include "file_fact_collector.hpp"
#include "function_fact_collector.hpp"
#include "translation_unit_batch.hpp"

namespace lcart::analysis {

/// Turns the call events of a definition into call edges (FR-004). The
/// caller and the callees are named through FunctionFactCollector, so all
/// function events of the AST must have been replayed first.
///
/// Not thread-safe, for the same reason as FileFactCollector.
class CallFactCollector {
public:
    CallFactCollector(FileFactCollector& files, const FunctionFactCollector& functions,
                      facts::FactWriter& out);

    /// Records the calls in the body of `definition`, in source order, as
    /// calls from `caller` (the id FunctionFactCollector wrote it under).
    /// `files` are the files of its batch.
    void on_definition(const FunctionEvent& definition, std::span<const BatchFile> files,
                       facts::FunctionId caller);

    /// The first error from the fact writer, as FileFactCollector::status().
    [[nodiscard]] common::Status status() const { return status_; }

private:
    FileFactCollector* files_;
    const FunctionFactCollector* functions_;
    facts::FactWriter* out_;
    common::Status status_;
};

} // namespace lcart::analysis
