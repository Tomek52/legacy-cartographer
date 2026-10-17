#pragma once

#include "facts/fact_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lcart::analysis {

// What a worker recorded while parsing one translation unit: the events the
// collectors receive, in the order Clang produces them (ADR-0019). A batch
// holds no database ids and depends on no other translation unit; the
// writer replays it and takes every decision that does (file ids, the
// first-definition rule, ADR-0012/0013 identities, callees).
//
// The worker leaves out only events the writer would ignore without any
// effect on the facts: directives in files outside the root, functions and
// call sites located outside the root, calls to functions that are not
// recorded, calls in bodies of functions without a USR, and field
// assignments located outside the root or naming a function that is not
// recorded.

/// Index of a BatchFile in TranslationUnitBatch::files.
using BatchFileIndex = std::uint32_t;

/// A file named by an event.
struct BatchFile {
    std::string path; ///< As in the facts: root-relative, or absolute if external.
    bool is_external{};
    /// SHA-256 of the content as Clang read it, lowercase hex; empty if
    /// Clang had no content for it (an error only if the file is recorded).
    std::optional<std::string> content_sha256;
};

/// The main file of the translation unit was entered.
struct MainFileEvent {
    BatchFileIndex file{};
};

/// An `#include` directive in a file under the root, or, without an
/// includer, a file forced in with `-include` (Clang's predefines buffer).
struct IncludeEvent {
    std::optional<BatchFileIndex> includer;
    std::optional<BatchFileIndex> included; ///< Empty when not found.
    std::string spelled;
    std::uint32_t line{}; ///< Of the `#`, in the includer.
    bool is_angled{};
};

/// A call expression in the body of a definition, located in a file under
/// the root (a call written by a macro: at the macro's use).
struct CallEvent {
    /// Direct call: the callee as a batch-local reference to a function of
    /// the same AstEvent (FunctionEvent::canonical). Empty: indirect call.
    std::optional<std::uint32_t> callee;
    std::string via; ///< Indirect call: the member and its object, as written.
    /// Indirect call through a struct field of function-pointer type:
    /// Clang's USR of the field (ADR-0026); empty otherwise.
    std::string field;
    BatchFileIndex file{};
    std::uint32_t line{};
    std::uint32_t column{};
};

/// A function declaration located in a file under the root.
struct FunctionEvent {
    BatchFileIndex file{};
    /// Clang's USR; empty if Clang could not generate one.
    std::optional<std::string> clang_usr;
    bool is_internal{}; ///< Not visible outside its translation unit.
    /// Internal linkage: the fact path of the file of this translation
    /// unit's definition, else of the first declaration; empty if none
    /// (ADR-0012).
    std::string internal_prefix;
    bool is_definition{};
    std::uint32_t line_begin{};
    std::uint32_t line_end{};
    std::string name;
    std::string qualified_name;
    std::string signature;
    /// Batch-local reference to the function: equal for all declarations
    /// of one function within the AstEvent (Clang's canonical declaration).
    std::uint32_t canonical{};
    /// For a definition: the calls in its body, in source order.
    std::vector<CallEvent> calls;
};

/// A function assigned to a struct field of function-pointer type in the
/// initializer of a variable with static storage duration, located under
/// the root (FR-015, ADR-0026).
struct FieldAssignmentEvent {
    std::string field; ///< Clang's USR of the field.
    /// The function as a batch-local reference (FunctionEvent::canonical).
    std::uint32_t function{};
};

/// The parsed AST of one compile command: its function declarations and
/// field assignments in traversal order. A translation unit with several
/// compile commands has one per command.
struct AstEvent {
    std::vector<FunctionEvent> functions;
    std::vector<FieldAssignmentEvent> field_assignments;
};

using BatchEvent = std::variant<MainFileEvent, IncludeEvent, AstEvent>;

/// Everything one translation unit contributes to the facts.
struct TranslationUnitBatch {
    std::size_t index{}; ///< Position in the sorted list of translation units.
    std::vector<BatchFile> files;
    std::vector<BatchEvent> events;
    /// Diagnostics, written after the unit's events.
    std::vector<facts::DiagnosticFact> diagnostics;
    /// Compiler arguments removed for security reasons (docs/security.md,
    /// T5), reported before the warnings of the events.
    std::vector<std::string> warnings;
    bool parsed{}; ///< Clang parsed every compile command without errors.
};

} // namespace lcart::analysis
