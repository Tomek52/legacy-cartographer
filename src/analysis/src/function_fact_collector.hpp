#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "file_fact_collector.hpp"

namespace lcart::analysis {

/// Turns function declarations and definitions into function facts (FR-003).
/// Like FileFactCollector, one collector serves every translation unit, so a
/// function declared in a header is written once, and once more when its
/// definition is first seen. Only functions in files under the root are
/// recorded (docs/architecture/data-model.md). A second definition of a
/// function with external linkage is a function of its own and a warning
/// (ADR-0013).
///
/// Not thread-safe, for the same reason as FileFactCollector.
class FunctionFactCollector {
public:
    FunctionFactCollector(FileFactCollector& files, facts::FactWriter& out);

    /// Forgets the declarations of the previous translation unit; call before
    /// the first on_function() of each one.
    void begin_translation_unit();

    /// Call for every function declaration of the translation unit, in
    /// source order. Returns true if `function` is a definition written now,
    /// so the calls in its body are to be recorded (once per function).
    [[nodiscard]] bool on_function(clang::ASTContext& context, const clang::FunctionDecl& function);

    /// The recorded function that a declaration of the current translation
    /// unit names, if any: its definition in this translation unit, else the
    /// function its USR names.
    [[nodiscard]] std::optional<facts::FunctionId> id_of(const clang::FunctionDecl& function) const;

    /// Warnings since the last call, in the order they arose.
    [[nodiscard]] std::vector<std::string> take_warnings() { return std::move(warnings_); }

    /// The first error from the fact writer, as FileFactCollector::status().
    [[nodiscard]] common::Status status() const { return status_; }

private:
    struct Recorded {
        facts::FunctionId id;
        bool has_definition;
        /// Where the definition is, once written: root-relative path, line.
        std::string definition_file;
        std::uint32_t definition_line;
    };

    FileFactCollector* files_;
    facts::FactWriter* out_;
    common::Status status_;
    std::vector<std::string> warnings_;
    /// USR as stored -> the function.
    std::map<std::string, Recorded> recorded_;
    /// Canonical declaration of the current translation unit -> its function.
    /// Looked up only, never iterated, so pointer order cannot reach the facts.
    std::map<const clang::FunctionDecl*, facts::FunctionId> in_translation_unit_;
};

} // namespace lcart::analysis
