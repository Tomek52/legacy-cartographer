#pragma once

#include "common/result.hpp"
#include "facts/fact_model.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::facts {

/// Write side of the fact database, used by the analysis module.
///
/// The `id` field of a record passed to add_*() is ignored; the store assigns
/// ids in insertion order and returns them. Adding a module, file or function
/// that is already recorded (same name, path or USR) returns the existing id,
/// because headers and functions are seen from many translation units.
/// Not thread-safe: one writer per database.
class FactWriter {
public:
    FactWriter() = default;
    FactWriter(const FactWriter&) = delete;
    FactWriter& operator=(const FactWriter&) = delete;
    FactWriter(FactWriter&&) = delete;
    FactWriter& operator=(FactWriter&&) = delete;
    virtual ~FactWriter() = default;

    virtual common::Result<ModuleId> add_module(const ModuleFact& module) = 0;
    virtual common::Result<FileId> add_file(const FileFact& file) = 0;
    virtual common::Status add_include(const IncludeFact& include) = 0;
    virtual common::Result<FunctionId> add_function(const FunctionFact& function) = 0;
    /// Records a direct or indirect call; may_call edges are derived.
    virtual common::Status add_call(const CallFact& call) = 0;
    /// Records that `assignment.function` is assigned to the field; the
    /// same field and function seen again (a header in several translation
    /// units) is recorded once.
    virtual common::Status add_field_assignment(const FieldAssignmentFact& assignment) = 0;
    virtual common::Status add_diagnostic(const DiagnosticFact& diagnostic) = 0;
    virtual common::Status set_coverage(const AnalysisCoverage& coverage) = 0;
    /// Replaces the module dependencies with those that the include edges
    /// and direct call edges written so far justify (FR-005 criterion 3).
    /// Call once every edge is written.
    virtual common::Status derive_module_dependencies() = 0;
    /// Replaces the may_call edges with those that the indirect calls through
    /// a field and the field assignments written so far justify: one per
    /// indirect call and function assigned to its field, at the call's site
    /// and with its `via` (FR-015, ADR-0026). Call once every call and
    /// assignment is written. May_call edges are no module dependencies.
    virtual common::Status derive_may_call_edges() = 0;
};

/// Read side of the fact database, used by verify, report and the CLI.
///
/// Every query returning a sequence returns it in a deterministic order,
/// documented per method, so outputs built from it are reproducible
/// (NFR-004). Queries fail only if the database cannot be read.
class FactQuery {
public:
    FactQuery() = default;
    FactQuery(const FactQuery&) = delete;
    FactQuery& operator=(const FactQuery&) = delete;
    FactQuery(FactQuery&&) = delete;
    FactQuery& operator=(FactQuery&&) = delete;
    virtual ~FactQuery() = default;

    [[nodiscard]] virtual common::Result<std::optional<ModuleFact>>
    find_module(std::string_view name) const = 0;
    [[nodiscard]] virtual common::Result<std::optional<FileFact>>
    find_file(std::string_view path) const = 0;
    [[nodiscard]] virtual common::Result<std::optional<FileFact>> file(FileId id) const = 0;

    /// Every module, by name.
    [[nodiscard]] virtual common::Result<std::vector<ModuleFact>> modules() const = 0;

    /// Every file, by path, then id.
    [[nodiscard]] virtual common::Result<std::vector<FileFact>> files() const = 0;

    /// All functions with this unqualified name, by file path, then first
    /// line, then id. More than one result means the name is ambiguous (e.g.
    /// `static` functions in different files).
    [[nodiscard]] virtual common::Result<std::vector<FunctionFact>>
    find_functions(std::string_view name) const = 0;

    /// The function whose stored USR (FunctionFact::usr, path-prefixed for
    /// internal linkage, ADR-0012) is `usr`.
    [[nodiscard]] virtual common::Result<std::optional<FunctionFact>>
    find_function_by_usr(std::string_view usr) const = 0;
    [[nodiscard]] virtual common::Result<std::optional<FunctionFact>>
    function(FunctionId id) const = 0;

    /// Every function, by file path, then first line, then USR.
    [[nodiscard]] virtual common::Result<std::vector<FunctionFact>> functions() const = 0;

    /// Calls made by `caller`, by call-site file path, then line, then column,
    /// then id.
    [[nodiscard]] virtual common::Result<std::vector<CallFact>>
    calls_from(FunctionId caller) const = 0;

    /// Direct calls naming `callee`, by call-site file path, then line, then
    /// column, then id.
    [[nodiscard]] virtual common::Result<std::vector<CallFact>>
    calls_to(FunctionId callee) const = 0;

    /// Every direct and indirect call, by call-site file path, then line,
    /// then column, then id.
    [[nodiscard]] virtual common::Result<std::vector<CallFact>> calls() const = 0;

    /// The may_call edges from `caller` (FR-015), by call-site file path,
    /// then line, then column, then callee USR, then id: empty until
    /// FactWriter::derive_may_call_edges() has run. Never returned by the
    /// call queries above.
    [[nodiscard]] virtual common::Result<std::vector<CallFact>>
    may_calls_from(FunctionId caller) const = 0;

    /// Every may_call edge, in the order of may_calls_from().
    [[nodiscard]] virtual common::Result<std::vector<CallFact>> may_calls() const = 0;

    /// Include directives in `includer`, by line, then id.
    [[nodiscard]] virtual common::Result<std::vector<IncludeFact>>
    includes_of(FileId includer) const = 0;

    /// Include directives naming `included`, by includer path, then line,
    /// then id.
    [[nodiscard]] virtual common::Result<std::vector<IncludeFact>>
    included_by(FileId included) const = 0;

    /// Every include directive, by includer path, then line, then id.
    [[nodiscard]] virtual common::Result<std::vector<IncludeFact>> includes() const = 0;

    /// Other modules that `module` depends on, by module name: empty until
    /// FactWriter::derive_module_dependencies() has run.
    [[nodiscard]] virtual common::Result<std::vector<ModuleFact>>
    module_dependencies(ModuleId module) const = 0;

    /// The edges that justify the dependencies of `module`, by the name of
    /// the module depended on, then include edges before call edges, then
    /// edge id.
    [[nodiscard]] virtual common::Result<std::vector<ModuleDependencyFact>>
    module_dependency_edges(ModuleId module) const = 0;

    /// All diagnostics, by translation unit, then file, then line, then id.
    [[nodiscard]] virtual common::Result<std::vector<DiagnosticFact>> diagnostics() const = 0;

    /// Zero counts when no analysis has recorded coverage yet.
    [[nodiscard]] virtual common::Result<AnalysisCoverage> coverage() const = 0;
};

/// The fact database of an analysis that commits each translation unit on
/// its own, so that an interrupted analysis can be resumed (ADR-0019).
///
/// Writes between begin_transaction() and commit_transaction() become
/// durable together or not at all; outside a transaction each write is its
/// own. derive_module_dependencies() and derive_may_call_edges() run their
/// own transactions and must be called outside one. Not thread-safe: one writer per database.
class AnalysisStore : public FactWriter, public FactQuery {
public:
    /// The META value of `key`; empty if there is none.
    [[nodiscard]] virtual common::Result<std::optional<std::string>>
    meta(std::string_view key) const = 0;
    /// Sets the META value of `key`, replacing an earlier one.
    virtual common::Status set_meta(std::string_view key, std::string_view value) = 0;

    /// Records `paths` (spelled like FileFact::path) as `pending`
    /// translation units, in this order; a path already recorded keeps its
    /// status.
    virtual common::Status add_translation_units(std::span<const std::string> paths) = 0;
    /// Every translation unit, in the order they were added.
    [[nodiscard]] virtual common::Result<std::vector<TranslationUnitFact>>
    translation_units() const = 0;
    /// Sets the status of the translation unit at `path` (added before) and
    /// links it to its main file if that is a recorded file.
    virtual common::Status set_translation_unit_status(std::string_view path,
                                                       TranslationUnitStatus status) = 0;

    virtual common::Status begin_transaction() = 0;
    virtual common::Status commit_transaction() = 0;
    /// Discards the writes since begin_transaction(); nothing if none is open.
    virtual void rollback_transaction() = 0;
};

} // namespace lcart::facts
