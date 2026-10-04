#pragma once

#include "common/result.hpp"
#include "facts/fact_model.hpp"
#include "facts/fact_store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace lcart::facts {

/// The fact database as one SQLite file (docs/architecture/data-model.md).
///
/// All SQL uses bound parameters: names from analysed code are never part of
/// a statement's text (docs/security.md, T7). Not thread-safe.
class SqliteFactStore final : public FactWriter, public FactQuery {
public:
    /// Opens the database at `path`, creating the schema if the database is
    /// empty. `path` is a file name, `:memory:` or a SQLite `file:` URI.
    /// Fails with ErrorCode::incompatible_database if the database has another
    /// schema_version or is not a fact database, and with io_error if it
    /// cannot be opened.
    [[nodiscard]] static common::Result<std::unique_ptr<SqliteFactStore>>
    open(const std::string& path);

    SqliteFactStore(const SqliteFactStore&) = delete;
    SqliteFactStore& operator=(const SqliteFactStore&) = delete;
    SqliteFactStore(SqliteFactStore&&) = delete;
    SqliteFactStore& operator=(SqliteFactStore&&) = delete;
    ~SqliteFactStore() override;

    common::Result<ModuleId> add_module(const ModuleFact& module) override;
    common::Result<FileId> add_file(const FileFact& file) override;
    common::Status add_include(const IncludeFact& include) override;
    common::Result<FunctionId> add_function(const FunctionFact& function) override;
    common::Status add_call(const CallFact& call) override;
    common::Status add_diagnostic(const DiagnosticFact& diagnostic) override;
    common::Status set_coverage(const AnalysisCoverage& coverage) override;

    [[nodiscard]] common::Result<std::optional<ModuleFact>>
    find_module(std::string_view name) const override;
    [[nodiscard]] common::Result<std::optional<FileFact>>
    find_file(std::string_view path) const override;
    [[nodiscard]] common::Result<std::vector<FunctionFact>>
    find_functions(std::string_view name) const override;
    [[nodiscard]] common::Result<std::vector<CallFact>>
    calls_from(FunctionId caller) const override;
    [[nodiscard]] common::Result<std::vector<IncludeFact>>
    includes_of(FileId includer) const override;
    [[nodiscard]] common::Result<std::vector<ModuleFact>>
    module_dependencies(ModuleId module) const override;
    [[nodiscard]] common::Result<std::vector<DiagnosticFact>> diagnostics() const override;
    [[nodiscard]] common::Result<AnalysisCoverage> coverage() const override;

private:
    explicit SqliteFactStore(sqlite3* database);

    sqlite3* database_;
};

} // namespace lcart::facts
