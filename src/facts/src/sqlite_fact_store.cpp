#include "facts/sqlite_fact_store.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lcart::facts {

namespace {

using common::Error;
using common::ErrorCode;
using common::Result;
using common::Status;

// Physical form of docs/architecture/data-model.md, schema_version 3.
// STRICT tables (SQLite >= 3.37) reject values of the wrong type instead of
// converting them. MODULE_DEPENDENCY is materialised by B-11; until then
// module_dependencies() derives it from the edges.
constexpr const char* schema_sql = R"sql(
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) STRICT;

CREATE TABLE module (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
) STRICT;

CREATE TABLE file (
    id          INTEGER PRIMARY KEY,
    path        TEXT NOT NULL UNIQUE,
    sha256      TEXT NOT NULL,
    is_header   INTEGER NOT NULL CHECK (is_header IN (0, 1)),
    is_external INTEGER NOT NULL CHECK (is_external IN (0, 1)),
    module_id   INTEGER REFERENCES module (id),
    CHECK ((is_external = 1) = (module_id IS NULL))
) STRICT;

CREATE TABLE translation_unit (
    id             INTEGER PRIMARY KEY,
    path           TEXT NOT NULL UNIQUE,
    file_id        INTEGER REFERENCES file (id),
    status         TEXT NOT NULL DEFAULT 'pending'
                   CHECK (status IN ('pending', 'parsed', 'failed')),
    command_sha256 TEXT,
    inputs_sha256  TEXT
) STRICT;

CREATE TABLE diagnostic (
    id        INTEGER PRIMARY KEY,
    tu_id     INTEGER NOT NULL REFERENCES translation_unit (id),
    file_path TEXT NOT NULL,
    file_id   INTEGER REFERENCES file (id),
    line      INTEGER NOT NULL,
    severity  TEXT NOT NULL CHECK (severity IN ('warning', 'error', 'fatal')),
    message   TEXT NOT NULL
) STRICT;

CREATE TABLE include_edge (
    id          INTEGER PRIMARY KEY,
    includer_id INTEGER NOT NULL REFERENCES file (id),
    included_id INTEGER REFERENCES file (id),
    spelled     TEXT NOT NULL,
    line        INTEGER NOT NULL,
    is_system   INTEGER NOT NULL CHECK (is_system IN (0, 1))
) STRICT;
CREATE INDEX include_edge_includer ON include_edge (includer_id);

CREATE TABLE function (
    id             INTEGER PRIMARY KEY,
    usr            TEXT NOT NULL UNIQUE,
    name           TEXT NOT NULL,
    qualified_name TEXT NOT NULL,
    signature      TEXT NOT NULL,
    file_id        INTEGER NOT NULL REFERENCES file (id),
    line_begin     INTEGER NOT NULL,
    line_end       INTEGER NOT NULL,
    is_definition  INTEGER NOT NULL CHECK (is_definition IN (0, 1)),
    linkage        TEXT NOT NULL CHECK (linkage IN ('internal', 'external'))
) STRICT;
CREATE INDEX function_name ON function (name);

CREATE TABLE call_edge (
    id        INTEGER PRIMARY KEY,
    caller_id INTEGER NOT NULL REFERENCES function (id),
    callee_id INTEGER REFERENCES function (id),
    kind      TEXT NOT NULL CHECK (kind IN ('direct', 'indirect', 'may_call')),
    via       TEXT NOT NULL,
    file_id   INTEGER NOT NULL REFERENCES file (id),
    line      INTEGER NOT NULL,
    col       INTEGER NOT NULL,
    field     TEXT NOT NULL,
    CHECK ((kind = 'indirect') = (callee_id IS NULL)),
    CHECK (kind <> 'direct' OR field = '')
) STRICT;
CREATE INDEX call_edge_caller ON call_edge (caller_id);
CREATE INDEX call_edge_callee ON call_edge (callee_id);

CREATE TABLE field_assignment (
    field       TEXT NOT NULL,
    function_id INTEGER NOT NULL REFERENCES function (id),
    PRIMARY KEY (field, function_id)
) STRICT;

CREATE TABLE module_dependency (
    from_module_id INTEGER NOT NULL REFERENCES module (id),
    to_module_id   INTEGER NOT NULL REFERENCES module (id),
    via            TEXT NOT NULL CHECK (via IN ('include', 'call')),
    edge_id        INTEGER NOT NULL,
    PRIMARY KEY (from_module_id, to_module_id, via, edge_id)
) STRICT;

CREATE TABLE claim (
    id       TEXT PRIMARY KEY,
    kind     TEXT NOT NULL,
    json     TEXT NOT NULL,
    document TEXT NOT NULL,
    section  TEXT NOT NULL
) STRICT;

CREATE TABLE verdict (
    claim_id TEXT PRIMARY KEY REFERENCES claim (id),
    status   TEXT NOT NULL CHECK (status IN ('confirmed', 'unverified', 'contradicted')),
    reason   TEXT NOT NULL,
    evidence TEXT NOT NULL
) STRICT;
)sql";

Error sqlite_error(sqlite3* database, ErrorCode code, std::string_view action) {
    return Error{.code = code, .message = std::string{action} + ": " + sqlite3_errmsg(database)};
}

/// Runs constant SQL (schema, pragmas). Never used with analysed data.
Status execute_constant(sqlite3* database, const char* sql) {
    if (sqlite3_exec(database, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return std::unexpected(sqlite_error(database, ErrorCode::io_error, "fact database"));
    }
    return {};
}

/// A prepared statement with parameters bound by position (1-based, as in
/// SQLite). Values are bound, never formatted into the SQL text.
class Statement {
public:
    static Result<Statement> prepare(sqlite3* database, std::string_view sql) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database, sql.data(), static_cast<int>(sql.size()), &statement,
                               nullptr) != SQLITE_OK) {
            return std::unexpected(
                sqlite_error(database, ErrorCode::io_error, "preparing a fact query"));
        }
        return Statement{database, statement};
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept
        : database_{other.database_}, statement_{std::exchange(other.statement_, nullptr)} {}
    Statement& operator=(Statement&&) = delete;
    ~Statement() { sqlite3_finalize(statement_); }

    /// Binds `values` to parameters 1..N. Text is bound without copying
    /// (destructor argument nullptr == SQLITE_STATIC): every caller steps the
    /// statement while the bound strings are still alive.
    template <typename... Values>
    Status bind(const Values&... values) {
        return bind_from(1, values...);
    }

    /// True if a row is available, false when done.
    Result<bool> step() {
        switch (sqlite3_step(statement_)) {
        case SQLITE_ROW:
            return true;
        case SQLITE_DONE:
            return false;
        case SQLITE_CONSTRAINT:
            return std::unexpected(sqlite_error(database_, ErrorCode::invalid_argument,
                                                "fact violates the database constraints"));
        default:
            return std::unexpected(sqlite_error(database_, ErrorCode::io_error, "fact database"));
        }
    }

    /// Makes the statement ready to be bound and run again.
    void reset() {
        sqlite3_reset(statement_);
        sqlite3_clear_bindings(statement_);
    }

    /// Steps a statement that returns no rows.
    Status run() {
        auto row = step();
        if (!row) {
            return std::unexpected(row.error());
        }
        return {};
    }

    [[nodiscard]] std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_, column);
    }
    [[nodiscard]] std::uint32_t uint32(int column) const {
        return static_cast<std::uint32_t>(integer(column));
    }
    [[nodiscard]] bool boolean(int column) const { return integer(column) != 0; }
    [[nodiscard]] bool is_null(int column) const {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }
    [[nodiscard]] std::string text(int column) const {
        const unsigned char* data = sqlite3_column_text(statement_, column);
        if (data == nullptr) {
            return {};
        }
        const auto size = static_cast<std::size_t>(sqlite3_column_bytes(statement_, column));
        // SQLite returns UTF-8 text as unsigned char; std::string wants char.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): SQLite C API
        return {reinterpret_cast<const char*>(data), size};
    }
    template <typename IdType>
    [[nodiscard]] IdType id(int column) const {
        return IdType{integer(column)};
    }
    template <typename IdType>
    [[nodiscard]] std::optional<IdType> optional_id(int column) const {
        if (is_null(column)) {
            return std::nullopt;
        }
        return id<IdType>(column);
    }

private:
    Statement(sqlite3* database, sqlite3_stmt* statement)
        : database_{database}, statement_{statement} {}

    static Status bind_from(int /*index*/) { return {}; }
    template <typename First, typename... Rest>
    Status bind_from(int index, const First& first, const Rest&... rest) {
        if (bind_one(index, first) != SQLITE_OK) {
            return std::unexpected(
                sqlite_error(database_, ErrorCode::io_error, "binding a fact query"));
        }
        return bind_from(index + 1, rest...);
    }

    int bind_one(int index, std::int64_t value) {
        return sqlite3_bind_int64(statement_, index, value);
    }
    int bind_one(int index, std::uint32_t value) {
        return sqlite3_bind_int64(statement_, index, value);
    }
    int bind_one(int index, bool value) {
        return sqlite3_bind_int(statement_, index, value ? 1 : 0);
    }
    int bind_one(int index, std::string_view value) {
        return sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()),
                                 nullptr);
    }
    int bind_one(int index, const std::string& value) {
        return bind_one(index, std::string_view{value});
    }
    int bind_one(int index, const char* value) { return bind_one(index, std::string_view{value}); }
    template <typename Tag>
    int bind_one(int index, Id<Tag> value) {
        return bind_one(index, value.value);
    }
    template <typename T>
    int bind_one(int index, const std::optional<T>& value) {
        return value ? bind_one(index, *value) : sqlite3_bind_null(statement_, index);
    }

    sqlite3* database_;
    sqlite3_stmt* statement_;
};

/// Prepares `sql`, binds `values` and runs it to completion.
template <typename... Values>
Status execute(sqlite3* database, std::string_view sql, const Values&... values) {
    auto statement = Statement::prepare(database, sql);
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto bound = statement->bind(values...); !bound) {
        return bound;
    }
    return statement->run();
}

/// Prepares `sql`, binds `values` and returns one int64 from the first row,
/// or nullopt if there is no row.
template <typename... Values>
Result<std::optional<std::int64_t>> select_integer(sqlite3* database, std::string_view sql,
                                                   const Values&... values) {
    auto statement = Statement::prepare(database, sql);
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto bound = statement->bind(values...); !bound) {
        return std::unexpected(bound.error());
    }
    auto row = statement->step();
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!*row) {
        return std::optional<std::int64_t>{};
    }
    return std::optional<std::int64_t>{statement->integer(0)};
}

/// Prepares `sql`, binds `values` and converts every row with `read_row`.
template <typename Row, typename ReadRow, typename... Values>
Result<std::vector<Row>> select_all(sqlite3* database, std::string_view sql, ReadRow read_row,
                                    const Values&... values) {
    auto statement = Statement::prepare(database, sql);
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto bound = statement->bind(values...); !bound) {
        return std::unexpected(bound.error());
    }
    std::vector<Row> rows;
    while (true) {
        auto row = statement->step();
        if (!row) {
            return std::unexpected(row.error());
        }
        if (!*row) {
            return rows;
        }
        auto value = read_row(*statement);
        if (!value) {
            return std::unexpected(value.error());
        }
        rows.push_back(std::move(*value));
    }
}

template <typename Row>
Result<std::optional<Row>> first(Result<std::vector<Row>> rows) {
    if (!rows) {
        return std::unexpected(rows.error());
    }
    if (rows->empty()) {
        return std::optional<Row>{};
    }
    return std::optional<Row>{std::move(rows->front())};
}

/// Inserts a row unless a row with the same unique `key` exists, and returns
/// the id of the row either way.
template <typename... Values>
Result<std::int64_t> insert_or_get(sqlite3* database, std::string_view insert_sql,
                                   std::string_view select_sql, std::string_view key,
                                   const Values&... values) {
    auto existing = select_integer(database, select_sql, key);
    if (!existing) {
        return std::unexpected(existing.error());
    }
    if (*existing) {
        return **existing;
    }
    if (auto inserted = execute(database, insert_sql, key, values...); !inserted) {
        return std::unexpected(inserted.error());
    }
    return sqlite3_last_insert_rowid(database);
}

Error corrupt_value(std::string_view column, std::string_view value) {
    return Error{.code = ErrorCode::incompatible_database,
                 .message = "unexpected " + std::string{column} + " '" + std::string{value} +
                            "' in fact database"};
}

const char* to_text(Linkage linkage) {
    switch (linkage) {
    case Linkage::internal:
        return "internal";
    case Linkage::external:
        return "external";
    }
    std::unreachable();
}

Result<Linkage> linkage_from(std::string_view text) {
    if (text == "internal") {
        return Linkage::internal;
    }
    if (text == "external") {
        return Linkage::external;
    }
    return std::unexpected(corrupt_value("linkage", text));
}

const char* to_text(CallKind kind) {
    switch (kind) {
    case CallKind::direct:
        return "direct";
    case CallKind::indirect:
        return "indirect";
    case CallKind::may_call:
        return "may_call";
    }
    std::unreachable();
}

Result<CallKind> call_kind_from(std::string_view text) {
    if (text == "direct") {
        return CallKind::direct;
    }
    if (text == "indirect") {
        return CallKind::indirect;
    }
    if (text == "may_call") {
        return CallKind::may_call;
    }
    return std::unexpected(corrupt_value("call kind", text));
}

const char* to_text(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    case DiagnosticSeverity::fatal:
        return "fatal";
    }
    std::unreachable();
}

Result<DependencyVia> dependency_via_from(std::string_view text) {
    if (text == "include") {
        return DependencyVia::include;
    }
    if (text == "call") {
        return DependencyVia::call;
    }
    return std::unexpected(corrupt_value("module dependency via", text));
}

Result<DiagnosticSeverity> severity_from(std::string_view text) {
    if (text == "warning") {
        return DiagnosticSeverity::warning;
    }
    if (text == "error") {
        return DiagnosticSeverity::error;
    }
    if (text == "fatal") {
        return DiagnosticSeverity::fatal;
    }
    return std::unexpected(corrupt_value("diagnostic severity", text));
}

constexpr std::string_view coverage_total_key = "translation_units_total";
constexpr std::string_view coverage_parsed_key = "translation_units_parsed";

Result<ModuleFact> read_module(const Statement& row) {
    return ModuleFact{.id = row.id<ModuleId>(0), .name = row.text(1)};
}

Result<FileFact> read_file(const Statement& row) {
    return FileFact{.id = row.id<FileId>(0),
                    .path = row.text(1),
                    .content_sha256 = row.text(2),
                    .is_header = row.boolean(3),
                    .is_external = row.boolean(4),
                    .module = row.optional_id<ModuleId>(5)};
}

Result<FunctionFact> read_function(const Statement& row) {
    auto linkage = linkage_from(row.text(9));
    if (!linkage) {
        return std::unexpected(linkage.error());
    }
    return FunctionFact{.id = row.id<FunctionId>(0),
                        .usr = row.text(1),
                        .name = row.text(2),
                        .qualified_name = row.text(3),
                        .signature = row.text(4),
                        .file = row.id<FileId>(5),
                        .line_begin = row.uint32(6),
                        .line_end = row.uint32(7),
                        .is_definition = row.boolean(8),
                        .linkage = *linkage};
}

constexpr std::string_view file_columns =
    "SELECT id, path, sha256, is_header, is_external, module_id FROM file";
constexpr std::string_view function_columns =
    "SELECT f.id, f.usr, f.name, f.qualified_name, f.signature, f.file_id, f.line_begin,"
    " f.line_end, f.is_definition, f.linkage FROM function f";
constexpr std::string_view call_columns =
    "SELECT c.caller_id, c.callee_id, c.kind, c.via, c.file_id, c.line, c.col, c.field, c.id"
    " FROM call_edge c JOIN file ON file.id = c.file_id";
constexpr std::string_view include_columns =
    "SELECT e.includer_id, e.included_id, e.spelled, e.line, e.is_system, e.id"
    " FROM include_edge e JOIN file ON file.id = e.includer_id";

Result<CallFact> read_call(const Statement& row) {
    auto kind = call_kind_from(row.text(2));
    if (!kind) {
        return std::unexpected(kind.error());
    }
    return CallFact{
        .id = row.id<CallId>(8),
        .caller = row.id<FunctionId>(0),
        .callee = row.optional_id<FunctionId>(1),
        .kind = *kind,
        .via = row.text(3),
        .site = {.file = row.id<FileId>(4), .line = row.uint32(5), .column = row.uint32(6)},
        .field = row.text(7)};
}

Result<IncludeFact> read_include(const Statement& row) {
    return IncludeFact{.id = row.id<IncludeId>(5),
                       .includer = row.id<FileId>(0),
                       .included = row.optional_id<FileId>(1),
                       .spelled = row.text(2),
                       .line = row.uint32(3),
                       .is_system = row.boolean(4)};
}

/// Reads "meta" value `key` as an unsigned count; 0 if absent.
Result<std::uint32_t> meta_count(sqlite3* database, std::string_view key) {
    auto statement = Statement::prepare(database, "SELECT value FROM meta WHERE key = ?1");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    if (auto bound = statement->bind(key); !bound) {
        return std::unexpected(bound.error());
    }
    auto row = statement->step();
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!*row) {
        return 0U;
    }
    const std::string text = statement->text(0);
    std::uint32_t value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return std::unexpected(corrupt_value(key, text));
        }
        value = value * 10U + static_cast<std::uint32_t>(digit - '0');
    }
    return value;
}

/// Best effort after a failed schema creation: the caller reports the
/// original error, and an unfinished transaction is discarded on close anyway.
void rollback(sqlite3* database) {
    sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
}

constexpr std::string_view schema_version_key = "schema_version";

/// Creates the schema in an empty database, or checks the version of an
/// existing one.
Status initialise_schema(sqlite3* database) {
    auto tables = select_integer(database, "SELECT count(*) FROM sqlite_schema");
    if (!tables) {
        return std::unexpected(tables.error());
    }
    if (tables->value_or(0) == 0) {
        if (auto created = execute_constant(database, "BEGIN"); !created) {
            return created;
        }
        if (auto created = execute_constant(database, schema_sql); !created) {
            rollback(database);
            return created;
        }
        if (auto created = execute(database, "INSERT INTO meta (key, value) VALUES (?1, ?2)",
                                   schema_version_key, std::to_string(schema_version));
            !created) {
            rollback(database);
            return created;
        }
        return execute_constant(database, "COMMIT");
    }

    auto has_meta = select_integer(
        database, "SELECT count(*) FROM sqlite_schema WHERE type = 'table' AND name = 'meta'");
    if (!has_meta) {
        return std::unexpected(has_meta.error());
    }
    if (has_meta->value_or(0) == 0) {
        return std::unexpected(Error{.code = ErrorCode::incompatible_database,
                                     .message = "not an lcart fact database (no meta table)"});
    }
    auto version = meta_count(database, schema_version_key);
    if (!version) {
        return std::unexpected(Error{.code = ErrorCode::incompatible_database,
                                     .message = "fact database: " + version.error().message});
    }
    if (std::cmp_not_equal(*version, schema_version)) {
        return std::unexpected(
            Error{.code = ErrorCode::incompatible_database,
                  .message = "fact database has schema version " + std::to_string(*version) +
                             ", this lcart reads version " + std::to_string(schema_version) +
                             "; run the analysis again into a new database"});
    }
    return {};
}

} // namespace

namespace {

/// SQLITE_OPEN_NOFOLLOW (sqlite3.h): opening fails if the file name is a
/// symbolic link.
int open_link_flags(LinkPolicy links) {
    switch (links) {
    case LinkPolicy::follow:
        return 0;
    case LinkPolicy::refuse:
        return SQLITE_OPEN_NOFOLLOW;
    }
    std::unreachable();
}

} // namespace

Result<std::unique_ptr<SqliteFactStore>> SqliteFactStore::open(const std::string& path,
                                                               LinkPolicy links) {
    sqlite3* database = nullptr;
    const int flags =
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI | open_link_flags(links);
    const int opened = sqlite3_open_v2(path.c_str(), &database, flags, nullptr);
    // Owned from here on, also on failure (sqlite3_open_v2 may allocate).
    std::unique_ptr<SqliteFactStore> store{new SqliteFactStore{database}};
    if (opened != SQLITE_OK) {
        return std::unexpected(
            sqlite_error(database, ErrorCode::io_error, "cannot open fact database " + path));
    }
    // SQLite leaves foreign keys off by default for compatibility.
    if (auto enabled = execute_constant(database, "PRAGMA foreign_keys = ON"); !enabled) {
        return std::unexpected(enabled.error());
    }
    if (auto ready = initialise_schema(database); !ready) {
        return std::unexpected(ready.error());
    }
    return store;
}

bool SqliteFactStore::is_fact_database(const std::string& path) {
    // Every SQLite 3 file starts with this header (sqlite.org/fileformat.html);
    // checking it first keeps SQLite away from other files.
    static constexpr std::string_view sqlite_header{"SQLite format 3\0", 16};
    std::string header(sqlite_header.size(), '\0');
    try {
        std::ifstream in{path, std::ios::binary};
        in.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (!in || header != sqlite_header) {
            return false;
        }
    } catch (const std::ios_base::failure&) {
        // libstdc++'s filebuf throws on a failed read (a directory).
        return false;
    }
    sqlite3* database = nullptr;
    // No SQLITE_OPEN_URI: `path` is a file name, never a URI.
    const int opened = sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, int (*)(sqlite3*)> owned{database, sqlite3_close};
    if (opened != SQLITE_OK) {
        return false;
    }
    auto has_meta =
        select_integer(database, "SELECT count(*) FROM sqlite_schema WHERE type = 'table'"
                                 " AND name = 'meta'");
    if (!has_meta || has_meta->value_or(0) == 0) {
        return false;
    }
    auto has_version =
        select_integer(database, "SELECT count(*) FROM meta WHERE key = ?1", schema_version_key);
    return has_version && has_version->value_or(0) > 0;
}

SqliteFactStore::SqliteFactStore(sqlite3* database) : database_{database} {}

SqliteFactStore::~SqliteFactStore() {
    sqlite3_close(database_);
}

Result<ModuleId> SqliteFactStore::add_module(const ModuleFact& module) {
    auto id = insert_or_get(database_, "INSERT INTO module (name) VALUES (?1)",
                            "SELECT id FROM module WHERE name = ?1", module.name);
    if (!id) {
        return std::unexpected(id.error());
    }
    return ModuleId{*id};
}

Result<FileId> SqliteFactStore::add_file(const FileFact& file) {
    auto id = insert_or_get(database_,
                            "INSERT INTO file (path, sha256, is_header, is_external, module_id)"
                            " VALUES (?1, ?2, ?3, ?4, ?5)",
                            "SELECT id FROM file WHERE path = ?1", file.path, file.content_sha256,
                            file.is_header, file.is_external, file.module);
    if (!id) {
        return std::unexpected(id.error());
    }
    return FileId{*id};
}

Status SqliteFactStore::add_include(const IncludeFact& include) {
    return execute(database_,
                   "INSERT INTO include_edge (includer_id, included_id, spelled, line, is_system)"
                   " VALUES (?1, ?2, ?3, ?4, ?5)",
                   include.includer, include.included, include.spelled, include.line,
                   include.is_system);
}

Result<FunctionId> SqliteFactStore::add_function(const FunctionFact& function) {
    auto id = insert_or_get(
        database_,
        "INSERT INTO function (usr, name, qualified_name, signature, file_id, line_begin,"
        " line_end, is_definition, linkage) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
        "SELECT id FROM function WHERE usr = ?1", function.usr, function.name,
        function.qualified_name, function.signature, function.file, function.line_begin,
        function.line_end, function.is_definition, to_text(function.linkage));
    if (!id) {
        return std::unexpected(id.error());
    }
    // A function seen first as a declaration takes the location of its
    // definition (FR-003 criterion 2); later declarations change nothing.
    if (function.is_definition) {
        if (auto updated =
                execute(database_,
                        "UPDATE function SET signature = ?2, file_id = ?3, line_begin = ?4,"
                        " line_end = ?5, is_definition = 1, linkage = ?6"
                        " WHERE id = ?1 AND is_definition = 0",
                        *id, function.signature, function.file, function.line_begin,
                        function.line_end, to_text(function.linkage));
            !updated) {
            return std::unexpected(updated.error());
        }
    }
    return FunctionId{*id};
}

Status SqliteFactStore::add_call(const CallFact& call) {
    return execute(database_,
                   "INSERT INTO call_edge (caller_id, callee_id, kind, via, file_id, line, col,"
                   " field) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
                   call.caller, call.callee, to_text(call.kind), call.via, call.site.file,
                   call.site.line, call.site.column, call.field);
}

Status SqliteFactStore::add_field_assignment(const FieldAssignmentFact& assignment) {
    // A table in a header is seen by every translation unit including it.
    return execute(database_,
                   "INSERT INTO field_assignment (field, function_id) VALUES (?1, ?2)"
                   " ON CONFLICT DO NOTHING",
                   assignment.field, assignment.function);
}

Status SqliteFactStore::add_diagnostic(const DiagnosticFact& diagnostic) {
    auto tu = insert_or_get(database_, "INSERT INTO translation_unit (path) VALUES (?1)",
                            "SELECT id FROM translation_unit WHERE path = ?1",
                            diagnostic.translation_unit);
    if (!tu) {
        return std::unexpected(tu.error());
    }
    // file_path is spelled like FILE.path, so a diagnostic in a recorded file
    // finds its row; one in a file that is not recorded keeps a null file_id.
    return execute(database_,
                   "INSERT INTO diagnostic (tu_id, file_path, file_id, line, severity, message)"
                   " VALUES (?1, ?2, (SELECT id FROM file WHERE path = ?2), ?3, ?4, ?5)",
                   *tu, diagnostic.file, diagnostic.line, to_text(diagnostic.severity),
                   diagnostic.message);
}

Status SqliteFactStore::set_coverage(const AnalysisCoverage& coverage) {
    constexpr std::string_view upsert = "INSERT INTO meta (key, value) VALUES (?1, ?2)"
                                        " ON CONFLICT (key) DO UPDATE SET value = excluded.value";
    if (auto total = execute(database_, upsert, coverage_total_key,
                             std::to_string(coverage.translation_units_total));
        !total) {
        return total;
    }
    return execute(database_, upsert, coverage_parsed_key,
                   std::to_string(coverage.translation_units_parsed));
}

Result<std::optional<ModuleFact>> SqliteFactStore::find_module(std::string_view name) const {
    return first(select_all<ModuleFact>(database_, "SELECT id, name FROM module WHERE name = ?1",
                                        read_module, name));
}

Result<std::optional<FileFact>> SqliteFactStore::find_file(std::string_view path) const {
    return first(select_all<FileFact>(database_, std::string{file_columns} + " WHERE path = ?1",
                                      read_file, path));
}

Result<std::optional<FileFact>> SqliteFactStore::file(FileId id) const {
    return first(select_all<FileFact>(database_, std::string{file_columns} + " WHERE id = ?1",
                                      read_file, id));
}

Result<std::vector<ModuleFact>> SqliteFactStore::modules() const {
    return select_all<ModuleFact>(database_, "SELECT id, name FROM module ORDER BY name",
                                  read_module);
}

Result<std::vector<FileFact>> SqliteFactStore::files() const {
    return select_all<FileFact>(database_, std::string{file_columns} + " ORDER BY path, id",
                                read_file);
}

Result<std::vector<FunctionFact>> SqliteFactStore::find_functions(std::string_view name) const {
    return select_all<FunctionFact>(database_,
                                    std::string{function_columns} +
                                        " JOIN file ON file.id = f.file_id"
                                        " WHERE f.name = ?1 ORDER BY file.path, f.line_begin, f.id",
                                    read_function, name);
}

Result<std::optional<FunctionFact>>
SqliteFactStore::find_function_by_usr(std::string_view usr) const {
    return first(select_all<FunctionFact>(
        database_, std::string{function_columns} + " WHERE f.usr = ?1", read_function, usr));
}

Result<std::optional<FunctionFact>> SqliteFactStore::function(FunctionId id) const {
    return first(select_all<FunctionFact>(
        database_, std::string{function_columns} + " WHERE f.id = ?1", read_function, id));
}

Result<std::vector<FunctionFact>> SqliteFactStore::functions() const {
    return select_all<FunctionFact>(database_,
                                    std::string{function_columns} +
                                        " JOIN file ON file.id = f.file_id"
                                        " ORDER BY file.path, f.line_begin, f.usr",
                                    read_function);
}

// may_call edges (FR-015) are separate queries, never mixed with calls.

Result<std::vector<CallFact>> SqliteFactStore::calls_from(FunctionId caller) const {
    return select_all<CallFact>(database_,
                                std::string{call_columns} +
                                    " WHERE c.caller_id = ?1 AND c.kind IN ('direct', 'indirect')"
                                    " ORDER BY file.path, c.line, c.col, c.id",
                                read_call, caller);
}

Result<std::vector<CallFact>> SqliteFactStore::calls_to(FunctionId callee) const {
    return select_all<CallFact>(database_,
                                std::string{call_columns} +
                                    " WHERE c.callee_id = ?1 AND c.kind = 'direct'"
                                    " ORDER BY file.path, c.line, c.col, c.id",
                                read_call, callee);
}

Result<std::vector<CallFact>> SqliteFactStore::calls() const {
    return select_all<CallFact>(database_,
                                std::string{call_columns} +
                                    " WHERE c.kind IN ('direct', 'indirect')"
                                    " ORDER BY file.path, c.line, c.col, c.id",
                                read_call);
}

// The callee's USR, not its id, orders the edges of one call, so the order
// does not depend on the order in which translation units were analysed.
constexpr std::string_view may_call_order = " ORDER BY file.path, c.line, c.col, callee.usr, c.id";

Result<std::vector<CallFact>> SqliteFactStore::may_calls_from(FunctionId caller) const {
    return select_all<CallFact>(database_,
                                std::string{call_columns} +
                                    " JOIN function callee ON callee.id = c.callee_id"
                                    " WHERE c.caller_id = ?1 AND c.kind = 'may_call'" +
                                    std::string{may_call_order},
                                read_call, caller);
}

Result<std::vector<CallFact>> SqliteFactStore::may_calls() const {
    return select_all<CallFact>(database_,
                                std::string{call_columns} +
                                    " JOIN function callee ON callee.id = c.callee_id"
                                    " WHERE c.kind = 'may_call'" +
                                    std::string{may_call_order},
                                read_call);
}

Result<std::vector<IncludeFact>> SqliteFactStore::includes_of(FileId includer) const {
    return select_all<IncludeFact>(
        database_, std::string{include_columns} + " WHERE e.includer_id = ?1 ORDER BY e.line, e.id",
        read_include, includer);
}

Result<std::vector<IncludeFact>> SqliteFactStore::included_by(FileId included) const {
    return select_all<IncludeFact>(database_,
                                   std::string{include_columns} +
                                       " WHERE e.included_id = ?1 ORDER BY file.path, e.line, e.id",
                                   read_include, included);
}

Result<std::vector<IncludeFact>> SqliteFactStore::includes() const {
    return select_all<IncludeFact>(
        database_, std::string{include_columns} + " ORDER BY file.path, e.line, e.id",
        read_include);
}

Result<std::vector<ModuleFact>> SqliteFactStore::module_dependencies(ModuleId module) const {
    return select_all<ModuleFact>(database_,
                                  "SELECT m.id, m.name FROM module m WHERE m.id IN ("
                                  " SELECT to_module_id FROM module_dependency"
                                  " WHERE from_module_id = ?1)"
                                  " ORDER BY m.name",
                                  read_module, module);
}

Status SqliteFactStore::derive_module_dependencies() {
    // A function belongs to the module of the file it is located in (its
    // definition, once seen); indirect calls have no callee. An edge to an
    // external file has a NULL module, so the `<>` excludes it.
    constexpr const char* derive_sql = R"sql(
BEGIN;
DELETE FROM module_dependency;
INSERT INTO module_dependency (from_module_id, to_module_id, via, edge_id)
    SELECT source.module_id, target.module_id, 'include', e.id FROM include_edge e
    JOIN file source ON source.id = e.includer_id
    JOIN file target ON target.id = e.included_id
    WHERE source.module_id <> target.module_id;
INSERT INTO module_dependency (from_module_id, to_module_id, via, edge_id)
    SELECT caller_file.module_id, callee_file.module_id, 'call', c.id FROM call_edge c
    JOIN function caller ON caller.id = c.caller_id
    JOIN file caller_file ON caller_file.id = caller.file_id
    JOIN function callee ON callee.id = c.callee_id
    JOIN file callee_file ON callee_file.id = callee.file_id
    WHERE c.kind = 'direct' AND caller_file.module_id <> callee_file.module_id;
COMMIT;
)sql";
    if (auto derived = execute_constant(database_, derive_sql); !derived) {
        rollback(database_);
        return derived;
    }
    return {};
}

Status SqliteFactStore::derive_may_call_edges() {
    // One edge per indirect call through a field and function assigned to
    // that field, whichever translation unit assigned it (FR-015,
    // ADR-0026). Inserted in call, then callee order, so row ids follow the
    // order of the analysis, like those of the other edges.
    constexpr const char* derive_sql = R"sql(
BEGIN;
DELETE FROM call_edge WHERE kind = 'may_call';
INSERT INTO call_edge (caller_id, callee_id, kind, via, file_id, line, col, field)
    SELECT c.caller_id, a.function_id, 'may_call', c.via, c.file_id, c.line, c.col, c.field
    FROM call_edge c JOIN field_assignment a ON a.field = c.field
    WHERE c.kind = 'indirect' AND c.field <> ''
    ORDER BY c.id, a.function_id;
COMMIT;
)sql";
    if (auto derived = execute_constant(database_, derive_sql); !derived) {
        rollback(database_);
        return derived;
    }
    return {};
}

Result<std::vector<ModuleDependencyFact>>
SqliteFactStore::module_dependency_edges(ModuleId module) const {
    auto read_edge = [](const Statement& row) -> Result<ModuleDependencyFact> {
        auto via = dependency_via_from(row.text(2));
        if (!via) {
            return std::unexpected(via.error());
        }
        return ModuleDependencyFact{
            .from = row.id<ModuleId>(0),
            .to = row.id<ModuleId>(1),
            .via = *via,
            .edge_id = row.integer(3),
            .site = {.file = row.id<FileId>(4), .line = row.uint32(5), .column = row.uint32(6)}};
    };
    return select_all<ModuleDependencyFact>(
        database_,
        "SELECT d.from_module_id, d.to_module_id, d.via, d.edge_id,"
        " coalesce(i.includer_id, c.file_id), coalesce(i.line, c.line), coalesce(c.col, 0)"
        " FROM module_dependency d JOIN module m ON m.id = d.to_module_id"
        " LEFT JOIN include_edge i ON d.via = 'include' AND i.id = d.edge_id"
        " LEFT JOIN call_edge c ON d.via = 'call' AND c.id = d.edge_id"
        " WHERE d.from_module_id = ?1"
        " ORDER BY m.name, d.via = 'call', d.edge_id",
        read_edge, module);
}

Result<std::vector<DiagnosticFact>> SqliteFactStore::diagnostics() const {
    auto read_diagnostic = [](const Statement& row) -> Result<DiagnosticFact> {
        auto severity = severity_from(row.text(3));
        if (!severity) {
            return std::unexpected(severity.error());
        }
        return DiagnosticFact{.translation_unit = row.text(0),
                              .file = row.text(1),
                              .line = row.uint32(2),
                              .severity = *severity,
                              .message = row.text(4)};
    };
    return select_all<DiagnosticFact>(
        database_,
        "SELECT tu.path, d.file_path, d.line, d.severity, d.message"
        " FROM diagnostic d JOIN translation_unit tu ON tu.id = d.tu_id"
        " ORDER BY tu.path, d.file_path, d.line, d.id",
        read_diagnostic);
}

Result<AnalysisCoverage> SqliteFactStore::coverage() const {
    auto total = meta_count(database_, coverage_total_key);
    if (!total) {
        return std::unexpected(total.error());
    }
    auto parsed = meta_count(database_, coverage_parsed_key);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return AnalysisCoverage{.translation_units_total = *total, .translation_units_parsed = *parsed};
}

namespace {

const char* to_text(TranslationUnitStatus status) {
    switch (status) {
    case TranslationUnitStatus::pending:
        return "pending";
    case TranslationUnitStatus::parsed:
        return "parsed";
    case TranslationUnitStatus::failed:
        return "failed";
    }
    std::unreachable();
}

Result<TranslationUnitStatus> translation_unit_status_from(std::string_view text) {
    if (text == "pending") {
        return TranslationUnitStatus::pending;
    }
    if (text == "parsed") {
        return TranslationUnitStatus::parsed;
    }
    if (text == "failed") {
        return TranslationUnitStatus::failed;
    }
    return std::unexpected(corrupt_value("translation unit status", text));
}

} // namespace

Result<std::optional<std::string>> SqliteFactStore::meta(std::string_view key) const {
    return first(select_all<std::string>(
        database_, "SELECT value FROM meta WHERE key = ?1",
        [](const Statement& row) -> Result<std::string> { return row.text(0); }, key));
}

Status SqliteFactStore::set_meta(std::string_view key, std::string_view value) {
    return execute(database_,
                   "INSERT INTO meta (key, value) VALUES (?1, ?2)"
                   " ON CONFLICT (key) DO UPDATE SET value = excluded.value",
                   key, value);
}

Status SqliteFactStore::add_translation_units(std::span<const std::string> paths) {
    auto statement = Statement::prepare(
        database_, "INSERT INTO translation_unit (path) VALUES (?1) ON CONFLICT (path) DO NOTHING");
    if (!statement) {
        return std::unexpected(statement.error());
    }
    for (const std::string& path : paths) {
        if (auto bound = statement->bind(path); !bound) {
            return bound;
        }
        if (auto inserted = statement->run(); !inserted) {
            return inserted;
        }
        statement->reset();
    }
    return {};
}

Result<std::vector<TranslationUnitFact>> SqliteFactStore::translation_units() const {
    auto read_unit = [](const Statement& row) -> Result<TranslationUnitFact> {
        auto status = translation_unit_status_from(row.text(1));
        if (!status) {
            return std::unexpected(status.error());
        }
        return TranslationUnitFact{.path = row.text(0), .status = *status};
    };
    return select_all<TranslationUnitFact>(
        database_, "SELECT path, status FROM translation_unit ORDER BY id", read_unit);
}

Status SqliteFactStore::set_translation_unit_status(std::string_view path,
                                                    TranslationUnitStatus status) {
    // The path is spelled like FILE.path, so the main file finds its row.
    if (auto updated = execute(database_,
                               "UPDATE translation_unit SET status = ?2,"
                               " file_id = (SELECT id FROM file WHERE path = ?1) WHERE path = ?1",
                               path, to_text(status));
        !updated) {
        return updated;
    }
    if (sqlite3_changes(database_) != 1) {
        return std::unexpected(
            Error{.code = ErrorCode::invalid_argument,
                  .message = "no translation unit " + std::string{path} + " in the fact database"});
    }
    return {};
}

Status SqliteFactStore::begin_transaction() {
    return execute_constant(database_, "BEGIN");
}

Status SqliteFactStore::commit_transaction() {
    return execute_constant(database_, "COMMIT");
}

void SqliteFactStore::rollback_transaction() {
    if (sqlite3_get_autocommit(database_) == 0) {
        rollback(database_);
    }
}

} // namespace lcart::facts
