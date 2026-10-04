#pragma once

#include "common/result.hpp"
#include "facts/fact_model.hpp"

#include <optional>
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
    virtual common::Status add_call(const CallFact& call) = 0;
    virtual common::Status add_diagnostic(const DiagnosticFact& diagnostic) = 0;
    virtual common::Status set_coverage(const AnalysisCoverage& coverage) = 0;
    /// Replaces the module dependencies with those that the include edges
    /// and direct call edges written so far justify (FR-005 criterion 3).
    /// Call once every edge is written.
    virtual common::Status derive_module_dependencies() = 0;
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

    /// All functions with this unqualified name, by file path, then first
    /// line, then id. More than one result means the name is ambiguous (e.g.
    /// `static` functions in different files).
    [[nodiscard]] virtual common::Result<std::vector<FunctionFact>>
    find_functions(std::string_view name) const = 0;

    /// Calls made by `caller`, by call-site file path, then line, then column,
    /// then id.
    [[nodiscard]] virtual common::Result<std::vector<CallFact>>
    calls_from(FunctionId caller) const = 0;

    /// Include directives in `includer`, by line, then id.
    [[nodiscard]] virtual common::Result<std::vector<IncludeFact>>
    includes_of(FileId includer) const = 0;

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

} // namespace lcart::facts
