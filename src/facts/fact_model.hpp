#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace lcart::facts {

/// Version of the fact database schema (docs/architecture/data-model.md).
/// Any change to tables or column meaning increments it.
/// 2: translation unit and diagnostic paths are spelled like file paths
/// (B-12).
inline constexpr int schema_version = 2;

/// Row identifier in the fact database. Distinct types per table, so a
/// FileId cannot be passed where a FunctionId is expected.
template <typename Tag>
struct Id {
    std::int64_t value{};
    auto operator<=>(const Id&) const = default;
};

using FileId = Id<struct FileTag>;
using FunctionId = Id<struct FunctionTag>;
using ModuleId = Id<struct ModuleTag>;

/// 1-based line and column, as reported by Clang.
struct SourceLocation {
    FileId file{};
    std::uint32_t line{};
    std::uint32_t column{};

    bool operator==(const SourceLocation&) const = default;
};

enum class Linkage : std::uint8_t { internal, external };

/// `indirect`: a call through a function pointer or similar; the callee is
/// not known statically, so verification of "A calls B" cannot be negative
/// for a caller that has indirect calls (docs/specs/claim-schema.md).
enum class CallKind : std::uint8_t { direct, indirect };

// Fact records. Every field has a default member initializer, so a record
// can be built with designated initializers naming only the fields that matter.

struct ModuleFact {
    ModuleId id{};
    std::string name{};

    bool operator==(const ModuleFact&) const = default;
};

struct FileFact {
    FileId id{};
    std::string path{}; ///< Relative to the analysis root, '/' separators.
    std::string content_sha256{};
    bool is_header{};
    /// Outside the analysis root (e.g. a system header): recorded only as an
    /// include target and never assigned to a module (FR-002).
    bool is_external{};
    std::optional<ModuleId> module{}; ///< Empty if and only if is_external.

    bool operator==(const FileFact&) const = default;
};

struct IncludeFact {
    FileId includer{};
    std::optional<FileId> included{}; ///< Empty when the header was not found.
    std::string spelled{};            ///< As written in the directive.
    std::uint32_t line{};
    bool is_system{}; ///< Angle-bracket include.

    bool operator==(const IncludeFact&) const = default;
};

struct FunctionFact {
    FunctionId id{};
    std::string usr{}; ///< Clang Unified Symbol Resolution: stable identity across TUs.
    std::string name{};
    std::string qualified_name{};
    std::string signature{};
    FileId file{};
    std::uint32_t line_begin{};
    std::uint32_t line_end{};
    bool is_definition{};
    Linkage linkage{Linkage::external};

    bool operator==(const FunctionFact&) const = default;
};

struct CallFact {
    FunctionId caller{};
    std::optional<FunctionId> callee{}; ///< Empty if and only if CallKind::indirect.
    CallKind kind{CallKind::direct};
    /// Indirect calls: the called member and what it is read from, as written
    /// (`backend.recv` for `ctx->backend->recv(...)`); empty otherwise.
    std::string via{};
    SourceLocation site{};

    bool operator==(const CallFact&) const = default;
};

/// What makes one module depend on another (FR-005 criterion 3).
enum class DependencyVia : std::uint8_t { include, call };

/// One edge that makes module `from` depend on module `to`: an include
/// directive in a file of `from` naming a file of `to`, or a direct call from
/// a function of `from` to a function of `to` (a function belongs to the
/// module of the file it is located in: its definition, else its first
/// declaration).
struct ModuleDependencyFact {
    ModuleId from{};
    ModuleId to{};
    DependencyVia via{DependencyVia::include};
    std::int64_t edge_id{}; ///< Row id of the include or call edge.
    /// The include directive (column 0: not recorded) or the call site.
    SourceLocation site{};

    bool operator==(const ModuleDependencyFact&) const = default;
};

enum class DiagnosticSeverity : std::uint8_t { warning, error, fatal };

/// A compiler diagnostic from analysing one translation unit (FR-001). Both
/// paths are spelled like FileFact::path: relative to the analysis root, or
/// absolute outside it. A diagnostic in a recorded file is linked to it.
struct DiagnosticFact {
    std::string translation_unit{}; ///< The main file of the unit.
    std::string file{};             ///< Empty when the diagnostic has no location.
    std::uint32_t line{};
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string message{};

    bool operator==(const DiagnosticFact&) const = default;
};

/// How much of the compilation database was analysed. The verifier only
/// reports `contradicted` for an absent fact when coverage is complete.
struct AnalysisCoverage {
    std::uint32_t translation_units_total{};
    std::uint32_t translation_units_parsed{};

    bool operator==(const AnalysisCoverage&) const = default;
};

} // namespace lcart::facts
