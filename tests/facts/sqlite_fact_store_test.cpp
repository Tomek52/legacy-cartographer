// Verifies: b06-sqlite-fact-store-v1 (criteria 1-4: round trip in the
// documented order, schema version check, bound parameters, in-memory only)
// and the storage side of fr002-record-files-and-includes-v1,
// fr003-record-functions-v1 (one row per USR, definition wins) and
// fr004-record-call-edges-v1 (indirect edges keep their member name).
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using lcart::common::ErrorCode;
using lcart::facts::SqliteFactStore;
using namespace lcart::facts;

std::unique_ptr<SqliteFactStore> open_in_memory() {
    auto store = SqliteFactStore::open(":memory:");
    EXPECT_TRUE(store.has_value()) << (store ? "" : store.error().message);
    return store ? std::move(*store) : nullptr;
}

template <typename T>
T value_of(lcart::common::Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

void expect_ok(const lcart::common::Status& status) {
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

/// Two modules, `core` (core.h, core.c, util.c) and `app` (main.c), plus one
/// external header. Records are added out of path order on purpose.
struct Fixture {
    std::unique_ptr<SqliteFactStore> store = open_in_memory();
    ModuleId core, app;
    FileId core_c, util_c, core_h, main_c, stdio_h;
    FunctionId init, helper_core, helper_util, run, main;

    Fixture() {
        core = value_of(store->add_module({.name = "core"}));
        app = value_of(store->add_module({.name = "app"}));
        util_c = add_file("src/util.c", core);
        core_c = add_file("src/core.c", core);
        core_h = add_file("src/core.h", core, /*is_header=*/true);
        main_c = add_file("app/main.c", app);
        stdio_h = value_of(store->add_file(
            {.path = "/usr/include/stdio.h", .is_header = true, .is_external = true}));

        // Declared in the header first, then defined in core.c: one function.
        init = add_function("usr:init", "init", core_h, 3, 3, false);
        EXPECT_EQ(add_function("usr:init", "init", core_c, 10, 14, true), init);
        helper_util = add_function("usr:util.c@helper", "helper", util_c, 2, 4, true);
        helper_core = add_function("usr:core.c@helper", "helper", core_c, 5, 8, true);
        run = add_function("usr:run", "run", core_c, 20, 30, true);
        main = add_function("usr:main", "main", main_c, 1, 6, true);

        expect_ok(store->add_include({.includer = main_c,
                                      .included = stdio_h,
                                      .spelled = "stdio.h",
                                      .line = 2,
                                      .is_system = true}));
        expect_ok(store->add_include(
            {.includer = main_c, .included = core_h, .spelled = "core.h", .line = 1}));
        expect_ok(store->add_include(
            {.includer = main_c, .included = std::nullopt, .spelled = "missing.h", .line = 3}));

        add_call(run, helper_core, core_c, 25, 9);
        add_call(run, init, core_c, 22, 5);
        expect_ok(store->add_call({.caller = run,
                                   .callee = std::nullopt,
                                   .kind = CallKind::indirect,
                                   .via = "recv",
                                   .site = {.file = core_c, .line = 22, .column = 2}}));
        add_call(main, init, main_c, 3, 5);
    }

    [[nodiscard]] FileId add_file(const std::string& path, ModuleId module,
                                  bool is_header = false) const {
        return value_of(store->add_file({.path = path,
                                         .content_sha256 = "sha-" + path,
                                         .is_header = is_header,
                                         .module = module}));
    }

    [[nodiscard]] FunctionId add_function(const std::string& usr, const std::string& name,
                                          FileId file, std::uint32_t begin, std::uint32_t end,
                                          bool is_definition) const {
        return value_of(store->add_function({.usr = usr,
                                             .name = name,
                                             .qualified_name = name,
                                             .signature = "void " + name + "(void)",
                                             .file = file,
                                             .line_begin = begin,
                                             .line_end = end,
                                             .is_definition = is_definition,
                                             .linkage = Linkage::external}));
    }

    void add_call(FunctionId caller, FunctionId callee, FileId file, std::uint32_t line,
                  std::uint32_t column) const {
        expect_ok(store->add_call({.caller = caller,
                                   .callee = callee,
                                   .kind = CallKind::direct,
                                   .site = {.file = file, .line = line, .column = column}}));
    }
};

TEST(SqliteFactStore, FindsModulesAndFiles) {
    const Fixture f;
    const ModuleFact core{.id = f.core, .name = "core"};
    const FileFact core_h{.id = f.core_h,
                          .path = "src/core.h",
                          .content_sha256 = "sha-src/core.h",
                          .is_header = true,
                          .is_external = false,
                          .module = f.core};
    const FileFact stdio_h{.id = f.stdio_h,
                           .path = "/usr/include/stdio.h",
                           .is_header = true,
                           .is_external = true,
                           .module = std::nullopt};

    EXPECT_EQ(value_of(f.store->find_module("core")), core);
    EXPECT_EQ(value_of(f.store->find_module("nope")), std::nullopt);
    EXPECT_EQ(value_of(f.store->find_file("src/core.h")), core_h);
    EXPECT_EQ(value_of(f.store->find_file("/usr/include/stdio.h")), stdio_h);
}

TEST(SqliteFactStore, AddingKnownModuleOrFileReturnsExistingId) {
    Fixture f;
    EXPECT_EQ(value_of(f.store->add_module({.name = "core"})), f.core);
    EXPECT_EQ(f.add_file("src/core.h", f.core, true), f.core_h);
}

TEST(SqliteFactStore, FunctionsByNameInPathThenLineOrder) {
    Fixture f;
    auto helpers = value_of(f.store->find_functions("helper"));
    ASSERT_EQ(helpers.size(), 2U);
    EXPECT_EQ(helpers[0].id, f.helper_core); // src/core.c before src/util.c
    EXPECT_EQ(helpers[1].id, f.helper_util);
    EXPECT_NE(helpers[0].usr, helpers[1].usr);
}

TEST(SqliteFactStore, DefinitionReplacesDeclarationLocation) {
    Fixture f;
    auto init = value_of(f.store->find_functions("init"));
    ASSERT_EQ(init.size(), 1U);
    EXPECT_EQ(init[0].file, f.core_c);
    EXPECT_EQ(init[0].line_begin, 10U);
    EXPECT_EQ(init[0].line_end, 14U);
    EXPECT_TRUE(init[0].is_definition);

    // A later declaration does not move it back to the header.
    EXPECT_EQ(f.add_function("usr:init", "init", f.core_h, 3, 3, false), f.init);
    EXPECT_EQ(value_of(f.store->find_functions("init"))[0].file, f.core_c);
}

TEST(SqliteFactStore, CallsFromInSiteOrderWithIndirectEdges) {
    Fixture f;
    auto calls = value_of(f.store->calls_from(f.run));
    ASSERT_EQ(calls.size(), 3U);
    EXPECT_EQ(calls[0].kind, CallKind::indirect); // line 22, column 2
    EXPECT_FALSE(calls[0].callee.has_value());
    EXPECT_EQ(calls[0].via, "recv");
    EXPECT_EQ(calls[1].callee, f.init); // line 22, column 5
    EXPECT_EQ(calls[1].site.column, 5U);
    EXPECT_EQ(calls[2].callee, f.helper_core); // line 25
    EXPECT_EQ(calls[2].site.file, f.core_c);
    EXPECT_EQ(calls[2].site.line, 25U);
}

TEST(SqliteFactStore, IncludesInLineOrderWithUnresolvedTarget) {
    Fixture f;
    auto includes = value_of(f.store->includes_of(f.main_c));
    ASSERT_EQ(includes.size(), 3U);
    EXPECT_EQ(includes[0].included, f.core_h);
    EXPECT_FALSE(includes[0].is_system);
    EXPECT_EQ(includes[1].included, f.stdio_h);
    EXPECT_TRUE(includes[1].is_system);
    EXPECT_FALSE(includes[2].included.has_value());
    EXPECT_EQ(includes[2].spelled, "missing.h");
    EXPECT_EQ(includes[2].line, 3U);
}

TEST(SqliteFactStore, ModuleDependenciesFromIncludesAndCalls) {
    Fixture f;
    EXPECT_TRUE(value_of(f.store->module_dependencies(f.app)).empty()); // not derived yet
    expect_ok(f.store->derive_module_dependencies());

    auto app_deps = value_of(f.store->module_dependencies(f.app));
    ASSERT_EQ(app_deps.size(), 1U); // external stdio.h is in no module
    EXPECT_EQ(app_deps[0].id, f.core);
    EXPECT_TRUE(value_of(f.store->module_dependencies(f.core)).empty());
}

// Verifies: fr005-map-files-to-modules-v1 (criterion 3: dependencies are
// stored with the edges that justify them).
TEST(SqliteFactStore, ModuleDependenciesKeepTheirEdges) {
    Fixture f;
    expect_ok(f.store->derive_module_dependencies());

    // Ids follow insertion order: the second include, the fourth call.
    const std::vector<ModuleDependencyFact> expected{
        {.from = f.app,
         .to = f.core,
         .via = DependencyVia::include,
         .edge_id = 2,
         .site = {.file = f.main_c, .line = 1, .column = 0}},
        {.from = f.app,
         .to = f.core,
         .via = DependencyVia::call,
         .edge_id = 4,
         .site = {.file = f.main_c, .line = 3, .column = 5}},
    };
    EXPECT_EQ(value_of(f.store->module_dependency_edges(f.app)), expected);
    // Calls and includes within core justify nothing.
    EXPECT_TRUE(value_of(f.store->module_dependency_edges(f.core)).empty());
}

TEST(SqliteFactStore, DerivingModuleDependenciesAgainReplacesThem) {
    Fixture f;
    expect_ok(f.store->derive_module_dependencies());
    const ModuleId tools = value_of(f.store->add_module({.name = "tools"}));
    const FileId tool_c = f.add_file("tools/tool.c", tools);
    expect_ok(f.store->add_include(
        {.includer = tool_c, .included = f.core_h, .spelled = "core.h", .line = 1}));
    expect_ok(f.store->derive_module_dependencies());

    EXPECT_EQ(value_of(f.store->module_dependency_edges(f.app)).size(), 2U);
    auto tool_deps = value_of(f.store->module_dependencies(tools));
    ASSERT_EQ(tool_deps.size(), 1U);
    EXPECT_EQ(tool_deps[0].name, "core");
}

TEST(SqliteFactStore, DiagnosticsAndCoverageRoundTrip) {
    Fixture f;
    EXPECT_EQ(value_of(f.store->coverage()).translation_units_total, 0U);

    expect_ok(f.store->add_diagnostic({.translation_unit = "src/util.c",
                                       .file = "src/util.c",
                                       .line = 7,
                                       .severity = DiagnosticSeverity::error,
                                       .message = "expected ';'"}));
    expect_ok(f.store->add_diagnostic({.translation_unit = "src/core.c",
                                       .file = "",
                                       .line = 0,
                                       .severity = DiagnosticSeverity::fatal,
                                       .message = "no input"}));
    expect_ok(f.store->add_diagnostic({.translation_unit = "src/core.c",
                                       .file = "src/core.h",
                                       .line = 1,
                                       .severity = DiagnosticSeverity::warning,
                                       .message = "unused"}));
    expect_ok(f.store->set_coverage({.translation_units_total = 3, .translation_units_parsed = 1}));

    auto diagnostics = value_of(f.store->diagnostics());
    ASSERT_EQ(diagnostics.size(), 3U);
    EXPECT_EQ(diagnostics[0].message, "no input"); // src/core.c, no file
    EXPECT_EQ(diagnostics[0].severity, DiagnosticSeverity::fatal);
    EXPECT_EQ(diagnostics[1].file, "src/core.h");
    EXPECT_EQ(diagnostics[2].translation_unit, "src/util.c");
    EXPECT_EQ(diagnostics[2].line, 7U);

    auto coverage = value_of(f.store->coverage());
    EXPECT_EQ(coverage.translation_units_total, 3U);
    EXPECT_EQ(coverage.translation_units_parsed, 1U);
}

TEST(SqliteFactStore, RejectsEdgeToUnknownFile) {
    Fixture f;
    auto status = f.store->add_include({.includer = FileId{999}, .spelled = "x.h", .line = 1});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::invalid_argument);
}

TEST(SqliteFactStore, NamesAreBoundNotConcatenated) {
    Fixture f;
    const std::string hostile = "x'); DROP TABLE file;--";
    auto module = value_of(f.store->add_module({.name = hostile}));
    const FileId file = f.add_file(hostile, module);
    const FunctionId function = f.add_function("usr:" + hostile, hostile, file, 1, 1, true);

    EXPECT_EQ(value_of(f.store->find_file(hostile)).value_or(FileFact{}).id, file);
    EXPECT_EQ(value_of(f.store->find_module(hostile)), (ModuleFact{.id = module, .name = hostile}));
    const auto functions = value_of(f.store->find_functions(hostile));
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].id, function);
    EXPECT_TRUE(value_of(f.store->find_file("src/core.c")).has_value()); // table still there
}

/// Opens a raw connection to a named shared in-memory database, so a test
/// can prepare its content and then open it through the store.
class SharedMemoryDatabase {
public:
    static constexpr const char* uri = "file:lcart_test_db?mode=memory&cache=shared";

    SharedMemoryDatabase() {
        EXPECT_EQ(sqlite3_open_v2(uri, &database_,
                                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI,
                                  nullptr),
                  SQLITE_OK);
    }
    SharedMemoryDatabase(const SharedMemoryDatabase&) = delete;
    SharedMemoryDatabase& operator=(const SharedMemoryDatabase&) = delete;
    SharedMemoryDatabase(SharedMemoryDatabase&&) = delete;
    SharedMemoryDatabase& operator=(SharedMemoryDatabase&&) = delete;
    ~SharedMemoryDatabase() { sqlite3_close(database_); }

    void exec(const char* sql) {
        EXPECT_EQ(sqlite3_exec(database_, sql, nullptr, nullptr, nullptr), SQLITE_OK)
            << sqlite3_errmsg(database_);
    }

    /// The values of the first column of `sql`'s rows, as text ("NULL" for null).
    [[nodiscard]] std::vector<std::string> column(const char* sql) {
        std::vector<std::string> values;
        sqlite3_stmt* statement = nullptr;
        EXPECT_EQ(sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr), SQLITE_OK)
            << sqlite3_errmsg(database_);
        while (statement != nullptr && sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(statement, 0);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): SQLite C API
            values.emplace_back(text == nullptr ? "NULL" : reinterpret_cast<const char*>(text));
        }
        sqlite3_finalize(statement);
        return values;
    }

private:
    sqlite3* database_ = nullptr;
};

TEST(SqliteFactStore, ReopensDatabaseWithSameSchemaVersion) {
    const SharedMemoryDatabase keep_alive;
    {
        auto store = SqliteFactStore::open(SharedMemoryDatabase::uri);
        ASSERT_TRUE(store.has_value()) << store.error().message;
        ASSERT_TRUE((*store)->add_module({.name = "core"}).has_value());
    }
    auto reopened = SqliteFactStore::open(SharedMemoryDatabase::uri);
    ASSERT_TRUE(reopened.has_value()) << reopened.error().message;
    EXPECT_TRUE(value_of((*reopened)->find_module("core")).has_value());
}

TEST(SqliteFactStore, RejectsOtherSchemaVersion) {
    SharedMemoryDatabase database;
    {
        auto store = SqliteFactStore::open(SharedMemoryDatabase::uri);
        ASSERT_TRUE(store.has_value()) << store.error().message;
    }
    const std::string other_version = std::to_string(schema_version + 1);
    database.exec(
        ("UPDATE meta SET value = '" + other_version + "' WHERE key = 'schema_version'").c_str());

    auto store = SqliteFactStore::open(SharedMemoryDatabase::uri);
    ASSERT_FALSE(store.has_value());
    EXPECT_EQ(store.error().code, ErrorCode::incompatible_database);
    EXPECT_NE(store.error().message.find("schema version " + other_version), std::string::npos)
        << store.error().message;
}

TEST(SqliteFactStore, RejectsDatabaseThatIsNotAFactDatabase) {
    SharedMemoryDatabase database;
    database.exec("CREATE TABLE something_else(x)");

    auto store = SqliteFactStore::open(SharedMemoryDatabase::uri);
    ASSERT_FALSE(store.has_value());
    EXPECT_EQ(store.error().code, ErrorCode::incompatible_database);
}

TEST(SqliteFactStore, UnopenablePathIsAnIoError) {
    auto store = SqliteFactStore::open("/nonexistent-dir/facts.db");
    ASSERT_FALSE(store.has_value());
    EXPECT_EQ(store.error().code, ErrorCode::io_error);
}

// Verifies: fr006-query-and-export-facts-v1 (the queries behind `lcart query`
// and the export, each in its documented order).
TEST(SqliteFactStore, ListsEveryModuleFileAndFunction) {
    const Fixture f;
    const auto modules = value_of(f.store->modules());
    ASSERT_EQ(modules.size(), 2U);
    EXPECT_EQ(modules[0].name, "app");
    EXPECT_EQ(modules[1].name, "core");

    std::vector<std::string> paths;
    for (const FileFact& file : value_of(f.store->files())) {
        paths.push_back(file.path);
    }
    EXPECT_EQ(paths, (std::vector<std::string>{"/usr/include/stdio.h", "app/main.c", "src/core.c",
                                               "src/core.h", "src/util.c"}));

    std::vector<std::string> usrs;
    for (const FunctionFact& function : value_of(f.store->functions())) {
        usrs.push_back(function.usr);
    }
    // By file path, then first line: main.c; core.c 5, 10, 20; util.c.
    EXPECT_EQ(usrs, (std::vector<std::string>{"usr:main", "usr:core.c@helper", "usr:init",
                                              "usr:run", "usr:util.c@helper"}));
}

TEST(SqliteFactStore, LooksUpFilesAndFunctionsById) {
    const Fixture f;
    EXPECT_EQ(value_of(f.store->file(f.core_h)).value_or(FileFact{}).path, "src/core.h");
    EXPECT_EQ(value_of(f.store->file(FileId{999})), std::nullopt);
    EXPECT_EQ(value_of(f.store->function(f.run)).value_or(FunctionFact{}).usr, "usr:run");
    EXPECT_EQ(value_of(f.store->function(FunctionId{999})), std::nullopt);
    EXPECT_EQ(
        value_of(f.store->find_function_by_usr("usr:util.c@helper")).value_or(FunctionFact{}).id,
        f.helper_util);
    EXPECT_EQ(value_of(f.store->find_function_by_usr("helper")), std::nullopt);
}

TEST(SqliteFactStore, CallsToListsDirectCallersInSiteOrder) {
    Fixture f;
    const FunctionId late = f.add_function("usr:late", "late", f.util_c, 40, 41, true);
    f.add_call(late, f.init, f.util_c, 41, 3);

    const auto calls = value_of(f.store->calls_to(f.init));
    ASSERT_EQ(calls.size(), 3U);
    EXPECT_EQ(calls[0].caller, f.main); // app/main.c
    EXPECT_EQ(calls[1].caller, f.run);  // src/core.c
    EXPECT_EQ(calls[2].caller, late);   // src/util.c
    EXPECT_TRUE(value_of(f.store->calls_to(f.main)).empty());
}

TEST(SqliteFactStore, ListsEveryCallInSiteOrder) {
    const Fixture f;
    const auto calls = value_of(f.store->calls());
    ASSERT_EQ(calls.size(), 4U);
    EXPECT_EQ(calls[0].site.file, f.main_c);      // app/main.c 3:5
    EXPECT_EQ(calls[1].kind, CallKind::indirect); // src/core.c 22:2
    EXPECT_EQ(calls[2].callee, f.init);           // src/core.c 22:5
    EXPECT_EQ(calls[3].callee, f.helper_core);    // src/core.c 25:9
}

TEST(SqliteFactStore, IncludedByAndAllIncludesInIncluderOrder) {
    Fixture f;
    expect_ok(f.store->add_include(
        {.includer = f.core_c, .included = f.core_h, .spelled = "core.h", .line = 1}));

    const auto included_by = value_of(f.store->included_by(f.core_h));
    ASSERT_EQ(included_by.size(), 2U);
    EXPECT_EQ(included_by[0].includer, f.main_c); // app/main.c before src/core.c
    EXPECT_EQ(included_by[1].includer, f.core_c);

    const auto includes = value_of(f.store->includes());
    ASSERT_EQ(includes.size(), 4U);
    EXPECT_EQ(includes[0].spelled, "core.h");    // app/main.c:1
    EXPECT_EQ(includes[1].spelled, "stdio.h");   // app/main.c:2
    EXPECT_EQ(includes[2].spelled, "missing.h"); // app/main.c:3
    EXPECT_EQ(includes[3].includer, f.core_c);
}

// A diagnostic in a recorded file names it (the open question
// "DIAGNOSTIC.file_id stays null", resolved in B-12).
TEST(SqliteFactStore, DiagnosticInARecordedFileGetsItsFileId) {
    SharedMemoryDatabase database;
    {
        auto store = SqliteFactStore::open(SharedMemoryDatabase::uri);
        ASSERT_TRUE(store.has_value()) << store.error().message;
        const ModuleId module = value_of((*store)->add_module({.name = "src"}));
        const FileId file = value_of((*store)->add_file({.path = "src/core.c", .module = module}));
        ASSERT_EQ(file.value, 1);
        for (const char* path : {"src/core.c", "/usr/include/stdio.h", ""}) {
            expect_ok((*store)->add_diagnostic({.translation_unit = "src/core.c",
                                                .file = path,
                                                .line = 1,
                                                .severity = DiagnosticSeverity::error,
                                                .message = "x"}));
        }
    }
    EXPECT_EQ(database.column("SELECT file_id FROM diagnostic ORDER BY id"),
              (std::vector<std::string>{"1", "NULL", "NULL"}));
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: lcart writes its own
// partial database, never a file a symbolic link names): with
// LinkPolicy::refuse a link is not opened and its target is untouched;
// the default still follows links (a link to a database is that database).
TEST(SqliteFactStore, RefusesASymbolicLinkWhenAsked) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("lcart-no-follow-" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path target = dir / "target.lcart.db";
    ASSERT_TRUE(SqliteFactStore::open(target.string()).has_value());
    const std::filesystem::path link = dir / "link.lcart.db";
    std::filesystem::create_symlink(target, link);
    const std::filesystem::path dangling = dir / "dangling.lcart.db";
    std::filesystem::create_symlink(dir / "missing.lcart.db", dangling);

    EXPECT_FALSE(SqliteFactStore::open(link.string(), LinkPolicy::refuse).has_value());
    EXPECT_FALSE(SqliteFactStore::open(dangling.string(), LinkPolicy::refuse).has_value());
    EXPECT_FALSE(std::filesystem::exists(dir / "missing.lcart.db"));
    EXPECT_TRUE(SqliteFactStore::open(target.string(), LinkPolicy::refuse).has_value());
    EXPECT_TRUE(SqliteFactStore::open(link.string()).has_value());
    std::filesystem::remove_all(dir);
}

TEST(SqliteFactStore, RecognisesOnlyFactDatabases) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("lcart-is-fact-database-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const std::string facts_path = (dir / "facts.lcart.db").string();
    ASSERT_TRUE(SqliteFactStore::open(facts_path).has_value());
    EXPECT_TRUE(SqliteFactStore::is_fact_database(facts_path));

    const std::string other_path = (dir / "other.sqlite").string();
    sqlite3* other = nullptr;
    ASSERT_EQ(sqlite3_open(other_path.c_str(), &other), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(other, "CREATE TABLE meta(k)", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(other);
    EXPECT_FALSE(SqliteFactStore::is_fact_database(other_path)); // no schema_version

    std::ofstream{dir / "main.c"} << "int main(void) { return 0; }\n";
    EXPECT_FALSE(SqliteFactStore::is_fact_database((dir / "main.c").string()));
    EXPECT_FALSE(SqliteFactStore::is_fact_database((dir / "absent.db").string()));
    EXPECT_FALSE(SqliteFactStore::is_fact_database(dir.string()));
    std::filesystem::remove_all(dir);
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019, commit per TU: a
// translation unit's facts are committed together with its status; an
// uncommitted unit leaves nothing behind and stays `pending`).
TEST(SqliteFactStore, CommitsEachTranslationUnitWithItsStatus) {
    SharedMemoryDatabase database;
    {
        auto opened = SqliteFactStore::open(SharedMemoryDatabase::uri);
        ASSERT_TRUE(opened.has_value()) << opened.error().message;
        SqliteFactStore& store = **opened;
        const std::vector<std::string> units{"src/b.c", "src/a.c", "src/c.c"};
        expect_ok(store.add_translation_units(units));
        expect_ok(store.add_translation_units(std::vector<std::string>{"src/a.c"})); // kept
        EXPECT_EQ(value_of(store.translation_units()),
                  (std::vector<TranslationUnitFact>{
                      {.path = "src/b.c", .status = TranslationUnitStatus::pending},
                      {.path = "src/a.c", .status = TranslationUnitStatus::pending},
                      {.path = "src/c.c", .status = TranslationUnitStatus::pending}}));

        expect_ok(store.begin_transaction());
        const ModuleId module = value_of(store.add_module({.name = "src"}));
        ASSERT_EQ(value_of(store.add_file({.path = "src/b.c", .module = module})).value, 1);
        expect_ok(store.set_translation_unit_status("src/b.c", TranslationUnitStatus::parsed));
        expect_ok(store.commit_transaction());

        expect_ok(store.begin_transaction());
        expect_ok(store.add_diagnostic({.translation_unit = "src/a.c",
                                        .file = "src/a.c",
                                        .line = 2,
                                        .severity = DiagnosticSeverity::error,
                                        .message = "x"}));
        expect_ok(store.set_translation_unit_status("src/a.c", TranslationUnitStatus::failed));
        expect_ok(store.commit_transaction());

        // Interrupted in the middle of a unit: nothing of it stays.
        expect_ok(store.begin_transaction());
        ASSERT_TRUE(store.add_file({.path = "src/c.c", .module = module}).has_value());
        expect_ok(store.set_translation_unit_status("src/c.c", TranslationUnitStatus::parsed));
        store.rollback_transaction();
        store.rollback_transaction(); // none open: nothing happens
        EXPECT_FALSE(store.set_translation_unit_status("src/x.c", TranslationUnitStatus::parsed)
                         .has_value());

        EXPECT_EQ(value_of(store.translation_units()),
                  (std::vector<TranslationUnitFact>{
                      {.path = "src/b.c", .status = TranslationUnitStatus::parsed},
                      {.path = "src/a.c", .status = TranslationUnitStatus::failed},
                      {.path = "src/c.c", .status = TranslationUnitStatus::pending}}));
        EXPECT_FALSE(value_of(store.find_file("src/c.c")).has_value());
        EXPECT_EQ(value_of(store.diagnostics()).size(), 1U);
    }
    // The main file of a unit is linked once it is a recorded file.
    EXPECT_EQ(database.column("SELECT file_id FROM translation_unit ORDER BY id"),
              (std::vector<std::string>{"1", "NULL", "NULL"}));
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: META records what a
// resumed run must find unchanged).
TEST(SqliteFactStore, StoresMetaValues) {
    const auto store = open_in_memory();
    EXPECT_EQ(value_of(store->meta("tool_version")), std::nullopt);
    EXPECT_EQ(value_of(store->meta("schema_version")), std::to_string(schema_version));
    expect_ok(store->set_meta("tool_version", "1.2.3"));
    expect_ok(store->set_meta("tool_version", "1.2.4"));
    EXPECT_EQ(value_of(store->meta("tool_version")), "1.2.4");
}

} // namespace
