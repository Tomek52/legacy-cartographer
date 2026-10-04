#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>

#include <map>
#include <string>

#include "file_fact_collector.hpp"

namespace lcart::analysis {

/// Turns function declarations and definitions into function facts (FR-003).
/// Like FileFactCollector, one collector serves every translation unit, so a
/// function declared in a header is written once, and once more when its
/// definition is first seen. Only functions in files under the root are
/// recorded (docs/architecture/data-model.md).
///
/// Not thread-safe, for the same reason as FileFactCollector.
class FunctionFactCollector {
public:
    FunctionFactCollector(FileFactCollector& files, facts::FactWriter& out);

    void on_function(clang::ASTContext& context, const clang::FunctionDecl& function);

    /// The first error from the fact writer, as FileFactCollector::status().
    [[nodiscard]] common::Status status() const { return status_; }

private:
    FileFactCollector* files_;
    facts::FactWriter* out_;
    common::Status status_;
    /// USR as stored -> whether its definition has been written.
    std::map<std::string, bool> has_definition_;
};

} // namespace lcart::analysis
