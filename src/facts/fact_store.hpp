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
/// ids and returns them. Not thread-safe: one writer per database.
// TODO(B-06): SQLite implementation (SqliteFactStore) implementing both interfaces.
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
};

/// Read side of the fact database, used by verify, report and the CLI.
///
/// Every query returning a sequence returns it in a deterministic order
/// (by path, then line, then id), so outputs built from it are reproducible
/// (NFR-004).
class FactQuery {
public:
    FactQuery() = default;
    FactQuery(const FactQuery&) = delete;
    FactQuery& operator=(const FactQuery&) = delete;
    FactQuery(FactQuery&&) = delete;
    FactQuery& operator=(FactQuery&&) = delete;
    virtual ~FactQuery() = default;

    [[nodiscard]] virtual std::optional<ModuleFact> find_module(std::string_view name) const = 0;
    [[nodiscard]] virtual std::optional<FileFact> find_file(std::string_view path) const = 0;

    /// All functions with this unqualified name. More than one result means
    /// the name is ambiguous (e.g. `static` functions in different files).
    [[nodiscard]] virtual std::vector<FunctionFact> find_functions(std::string_view name) const = 0;

    [[nodiscard]] virtual std::vector<CallFact> calls_from(FunctionId caller) const = 0;
    [[nodiscard]] virtual std::vector<IncludeFact> includes_of(FileId includer) const = 0;

    /// Modules that `module` depends on through include or call edges.
    [[nodiscard]] virtual std::vector<ModuleId> module_dependencies(ModuleId module) const = 0;

    [[nodiscard]] virtual AnalysisCoverage coverage() const = 0;
};

} // namespace lcart::facts
