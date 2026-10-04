#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>

#include "file_fact_collector.hpp"
#include "function_fact_collector.hpp"

namespace lcart::analysis {

/// Turns the call expressions in a function body into call edges (FR-004).
/// The caller and the callees are named through FunctionFactCollector, so
/// a translation unit's functions must all have been seen first.
///
/// Not thread-safe, for the same reason as FileFactCollector.
class CallFactCollector {
public:
    CallFactCollector(FileFactCollector& files, const FunctionFactCollector& functions,
                      facts::FactWriter& out);

    /// Records the calls in the body of `definition`, in source order.
    void on_definition(clang::ASTContext& context, const clang::FunctionDecl& definition);

    /// The first error from the fact writer, as FileFactCollector::status().
    [[nodiscard]] common::Status status() const { return status_; }

private:
    FileFactCollector* files_;
    const FunctionFactCollector* functions_;
    facts::FactWriter* out_;
    common::Status status_;
};

} // namespace lcart::analysis
