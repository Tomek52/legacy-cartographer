#pragma once

#include "common/result.hpp"
#include "facts/fact_model.hpp"
#include "facts/fact_store.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace lcart::facts {

/// Whether SqliteFactStore::open opens a database file through a symbolic
/// link.
enum class LinkPolicy : std::uint8_t {
    follow, ///< A link to a database is that database.
    /// The file name must not be a symbolic link, or opening fails (for a
    /// file lcart writes as its own, ADR-0019).
    refuse,
};

/// The fact database as one SQLite file (docs/architecture/data-model.md).
///
/// All SQL uses bound parameters: names from analysed code are never part of
/// a statement's text (docs/security.md, T7). Not thread-safe.
class SqliteFactStore final : public AnalysisStore {
public:
    /// Opens the database at `path`, creating the schema if the database is
    /// empty. `path` is a file name, `:memory:` or a SQLite `file:` URI.
    /// Fails with ErrorCode::incompatible_database if the database has another
    /// schema_version or is not a fact database, and with io_error if it
    /// cannot be opened, including a symbolic link with LinkPolicy::refuse.
    [[nodiscard]] static common::Result<std::unique_ptr<SqliteFactStore>>
    open(const std::string& path, LinkPolicy links = LinkPolicy::follow);

    /// Whether the file at `path` is a fact database of any schema version:
    /// a SQLite file with lcart's `meta` table and a `schema_version`. Opens
    /// it read-only and changes nothing; false for anything else, including
    /// a file that cannot be read.
    [[nodiscard]] static bool is_fact_database(const std::string& path);

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
    common::Status add_field_assignment(const FieldAssignmentFact& assignment) override;
    common::Status add_diagnostic(const DiagnosticFact& diagnostic) override;
    common::Status set_coverage(const AnalysisCoverage& coverage) override;
    common::Status derive_module_dependencies() override;
    common::Status derive_may_call_edges() override;

    [[nodiscard]] common::Result<std::optional<ModuleFact>>
    find_module(std::string_view name) const override;
    [[nodiscard]] common::Result<std::optional<FileFact>>
    find_file(std::string_view path) const override;
    [[nodiscard]] common::Result<std::optional<FileFact>> file(FileId id) const override;
    [[nodiscard]] common::Result<std::vector<ModuleFact>> modules() const override;
    [[nodiscard]] common::Result<std::vector<FileFact>> files() const override;
    [[nodiscard]] common::Result<std::vector<FunctionFact>>
    find_functions(std::string_view name) const override;
    [[nodiscard]] common::Result<std::optional<FunctionFact>>
    find_function_by_usr(std::string_view usr) const override;
    [[nodiscard]] common::Result<std::optional<FunctionFact>>
    function(FunctionId id) const override;
    [[nodiscard]] common::Result<std::vector<FunctionFact>> functions() const override;
    [[nodiscard]] common::Result<std::vector<CallFact>>
    calls_from(FunctionId caller) const override;
    [[nodiscard]] common::Result<std::vector<CallFact>> calls_to(FunctionId callee) const override;
    [[nodiscard]] common::Result<std::vector<CallFact>> calls() const override;
    [[nodiscard]] common::Result<std::vector<CallFact>>
    may_calls_from(FunctionId caller) const override;
    [[nodiscard]] common::Result<std::vector<CallFact>> may_calls() const override;
    [[nodiscard]] common::Result<std::vector<IncludeFact>>
    includes_of(FileId includer) const override;
    [[nodiscard]] common::Result<std::vector<IncludeFact>>
    included_by(FileId included) const override;
    [[nodiscard]] common::Result<std::vector<IncludeFact>> includes() const override;
    [[nodiscard]] common::Result<std::vector<ModuleFact>>
    module_dependencies(ModuleId module) const override;
    [[nodiscard]] common::Result<std::vector<ModuleDependencyFact>>
    module_dependency_edges(ModuleId module) const override;
    [[nodiscard]] common::Result<std::vector<DiagnosticFact>> diagnostics() const override;
    [[nodiscard]] common::Result<AnalysisCoverage> coverage() const override;

    [[nodiscard]] common::Result<std::optional<std::string>>
    meta(std::string_view key) const override;
    common::Status set_meta(std::string_view key, std::string_view value) override;
    common::Status add_translation_units(std::span<const std::string> paths) override;
    [[nodiscard]] common::Result<std::vector<TranslationUnitFact>>
    translation_units() const override;
    common::Status set_translation_unit_status(std::string_view path,
                                               TranslationUnitStatus status) override;
    common::Status begin_transaction() override;
    common::Status commit_transaction() override;
    void rollback_transaction() override;

private:
    explicit SqliteFactStore(sqlite3* database);

    sqlite3* database_;
};

} // namespace lcart::facts
