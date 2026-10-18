// Verifies: b26-function-pointer-tables-v1 (criteria 1, 2, 4 and 5) and
// fr015-resolve-function-pointer-tables-v1 (criterion 2), on the
// fn_pointers fixture and a generated project with modules, through the
// SQLite store. Criterion 3 (libmodbus) is checked by
// scripts/libmodbus-baseline.sh, criterion 6 (resume) in
// parallel_analysis_test.cpp.
#include "analysis/clang_analyzer.hpp"
#include "facts/fact_export.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::ClangAnalyzer;
using namespace lcart::facts;

/// One may_call edge with names instead of ids, so expectations read like
/// the fixture's README.
struct MayCall {
    std::string caller;
    std::string callee;
    std::string via;
    std::string file;
    std::uint32_t line{};
    std::uint32_t column{};

    auto operator<=>(const MayCall&) const = default;
};

std::ostream& operator<<(std::ostream& out, const MayCall& edge) {
    return out << edge.caller << " may call " << edge.callee << " via '" << edge.via << "' at "
               << edge.file << ':' << edge.line << ':' << edge.column;
}

template <typename T>
T value_of(lcart::common::Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

class MayCallEdgesTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-may-call-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

public:
    [[nodiscard]] const fs::path& work_dir() const { return work_dir_; }

    /// Analyses `files` of `root` into a new in-memory store, each compiled
    /// with the root on the include path, listed in the compilation
    /// database in the order given.
    [[nodiscard]] std::unique_ptr<SqliteFactStore>
    analyze(const fs::path& root, const std::vector<std::string>& files) const {
        const fs::path database = work_dir_ / "compile_commands.json";
        {
            std::ofstream out{database};
            out << "[\n";
            for (std::size_t i = 0; i < files.size(); ++i) {
                out << R"(  {"directory": ")" << root.string() << R"(", "command": "clang -c -I. )"
                    << files[i] << R"(", "file": ")" << files[i] << "\"}"
                    << (i + 1 < files.size() ? ",\n" : "\n");
            }
            out << "]\n";
        }
        auto store = SqliteFactStore::open(":memory:");
        EXPECT_TRUE(store.has_value()) << (store ? "" : store.error().message);
        if (!store) {
            return nullptr;
        }
        ClangAnalyzer analyzer;
        auto summary = analyzer.analyze({.compilation_database = database,
                                         .root = root,
                                         .exclude_globs = {},
                                         .module_rules = {}},
                                        **store);
        EXPECT_TRUE(summary.has_value()) << (summary ? "" : summary.error().message);
        if (summary) {
            EXPECT_EQ(summary->translation_units_parsed, files.size());
        }
        return std::move(*store);
    }

    [[nodiscard]] static fs::path fn_pointers() {
        return fs::path{LCART_FIXTURES_DIR} / "fn_pointers";
    }

    /// Every may_call edge in `facts` from the only function named `caller`
    /// (an empty name: from every function), in the query's order.
    [[nodiscard]] static std::vector<MayCall> may_calls(const FactQuery& facts,
                                                        const std::string& caller = {}) {
        std::vector<CallFact> found;
        if (caller.empty()) {
            found = value_of(facts.may_calls());
        } else {
            const std::vector<FunctionFact> callers = value_of(facts.find_functions(caller));
            if (callers.size() != 1) {
                ADD_FAILURE() << callers.size() << " functions named " << caller;
                return {};
            }
            found = value_of(facts.may_calls_from(callers[0].id));
        }
        std::vector<MayCall> result;
        for (const CallFact& call : found) {
            EXPECT_EQ(call.kind, CallKind::may_call);
            if (!call.callee) {
                ADD_FAILURE() << "may_call edge without a callee";
                continue;
            }
            const auto from = value_of(facts.function(call.caller));
            const auto to = value_of(facts.function(*call.callee));
            const auto file = value_of(facts.file(call.site.file));
            result.push_back({.caller = from ? from->name : "?",
                              .callee = to ? to->name : "?",
                              .via = call.via,
                              .file = file ? file->path : "?",
                              .line = call.site.line,
                              .column = call.site.column});
        }
        return result;
    }

    /// Writes the project of ModuleBoundary below under `root`: the table's
    /// struct in module `inc`, a caller through it in `a`, the functions
    /// assigned to it in `b`.
    static void write_module_project(const fs::path& root) {
        fs::create_directories(root / "inc");
        fs::create_directories(root / "a");
        fs::create_directories(root / "b");
        std::ofstream{root / "inc/ops.h"} << "struct ops {\n"
                                             "    int (*run)(int value);\n"
                                             "};\n";
        std::ofstream{root / "a/use.c"} << "#include \"inc/ops.h\"\n"
                                           "\n"
                                           "int use(const struct ops *ops) {\n"
                                           "    return ops->run(1);\n"
                                           "}\n";
        std::ofstream{root / "b/impl.c"} << "#include \"inc/ops.h\"\n"
                                            "\n"
                                            "static int op_one(int value) { return value; }\n"
                                            "static int op_two(int value) { return -value; }\n"
                                            "static int op_three(int value) { return 3; }\n"
                                            "static int op_four(int value) { return 4; }\n"
                                            "\n"
                                            "const struct ops ops_table[2] = {{op_one}, "
                                            "{.run = &op_two}};\n"
                                            "\n"
                                            "int pick(void) {\n"
                                            "    static const struct ops chosen = {op_three};\n"
                                            "    return chosen.run(3);\n"
                                            "}\n"
                                            "\n"
                                            "int automatic(void) {\n"
                                            "    const struct ops temporary = {op_four};\n"
                                            "    return temporary.run(4);\n"
                                            "}\n";
    }

private:
    fs::path work_dir_;
};

std::string export_of(const FactQuery& facts) {
    std::ostringstream out;
    const auto written = write_jsonl_export(facts, out);
    EXPECT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    return std::move(out).str();
}

std::vector<std::string> fn_pointers_units() {
    return {"core.c", "rtu.c", "tcp.c"};
}

/// The id of the only function named `name`; a failure (and id 0) if there
/// is not exactly one.
FunctionId only_function(const FactQuery& facts, const std::string& name) {
    const std::vector<FunctionFact> found = value_of(facts.find_functions(name));
    if (found.size() != 1) {
        ADD_FAILURE() << found.size() << " functions named " << name;
        return {};
    }
    return found[0].id;
}

std::vector<CallKind> kinds_of(const std::vector<CallFact>& calls) {
    std::vector<CallKind> kinds;
    kinds.reserve(calls.size());
    for (const CallFact& call : calls) {
        kinds.push_back(call.kind);
    }
    return kinds;
}

/// The names of the modules that the module `name` depends on.
std::vector<std::string> dependencies_of(const FactQuery& facts, const std::string& name) {
    const std::optional<ModuleFact> module = value_of(facts.find_module(name));
    if (!module) {
        ADD_FAILURE() << "no module " << name;
        return {};
    }
    std::vector<std::string> names;
    for (const ModuleFact& depended_on : value_of(facts.module_dependencies(module->id))) {
        names.push_back(depended_on.name);
    }
    return names;
}

// Verifies: b26-function-pointer-tables-v1 (criterion 1: a positional
// table, `rtu_backend`, and a designated one, `tcp_backend` with only
// `.send`; calls through `ctx->backend->send` and through a local struct
// `local.recv`).
TEST_F(MayCallEdgesTest, CallThroughTableFieldMayCallEachFunctionAssignedToIt) {
    const auto store = analyze(fn_pointers(), fn_pointers_units());
    ASSERT_NE(store, nullptr);

    EXPECT_EQ(may_calls(*store, "transfer"),
              (std::vector<MayCall>{
                  {"transfer", "rtu_send", "backend.send", "core.c", 10, 16},
                  {"transfer", "tcp_send", "backend.send", "core.c", 10, 16},
                  {"transfer", "rtu_recv", "backend.recv", "core.c", 11, 19},
              }));
    EXPECT_EQ(may_calls(*store, "run"),
              (std::vector<MayCall>{{"run", "rtu_recv", "local.recv", "core.c", 20, 48}}));
}

// Verifies: b26-function-pointer-tables-v1 (criterion 2: a parameter,
// `apply`'s `handler`, and a local function pointer, `local_handler`).
TEST_F(MayCallEdgesTest, IndirectCallNotThroughTableFieldHasNoMayCall) {
    const auto store = analyze(fn_pointers(), fn_pointers_units());
    ASSERT_NE(store, nullptr);

    EXPECT_TRUE(may_calls(*store, "apply").empty());
    EXPECT_TRUE(may_calls(*store, "local_pointer").empty());
    EXPECT_EQ(may_calls(*store).size(), 4U); // only those of transfer and run
}

// Verifies: b26-function-pointer-tables-v1 (criterion 4: a separate query;
// calls_from, calls_to and calls do not return may_call edges) and
// fr015-resolve-function-pointer-tables-v1 (criterion 2).
TEST_F(MayCallEdgesTest, MayCallEdgesAreNotCalls) {
    const auto store = analyze(fn_pointers(), fn_pointers_units());
    ASSERT_NE(store, nullptr);
    const FunctionId transfer = only_function(*store, "transfer");
    const FunctionId tcp_send = only_function(*store, "tcp_send");

    EXPECT_EQ(kinds_of(value_of(store->calls_from(transfer))),
              (std::vector{CallKind::indirect, CallKind::indirect}));
    EXPECT_TRUE(value_of(store->calls_to(tcp_send)).empty());
    EXPECT_EQ(std::ranges::count(kinds_of(value_of(store->calls())), CallKind::may_call), 0);
}

// Verifies: b26-function-pointer-tables-v1 (criterion 4: a kind of its own
// in the export, never a `call` record).
TEST_F(MayCallEdgesTest, ExportListsMayCallEdgesAsTheirOwnKind) {
    const auto store = analyze(fn_pointers(), fn_pointers_units());
    ASSERT_NE(store, nullptr);

    const std::string exported = export_of(*store);
    EXPECT_NE(
        exported.find(
            R"json({"record":"may_call","caller":"c:@F@transfer","file":"core.c","line":10,"column":16,"callee":"tcp.c#c:tcp.c@F@tcp_send","via":"backend.send"})json"),
        std::string::npos)
        << exported;
    EXPECT_EQ(exported.find(R"("kind":"may_call")"), std::string::npos);
}

// Verifies: b26-function-pointer-tables-v1 (criterion 5) and
// nfr004-deterministic-output-v1: the compilation database in every order
// gives the same export. Units are analysed in sorted path order (ADR-0019),
// so this only guards the input order of the database; the order of tables
// and calls across units is EdgesDoNotDependOnTheOrderOfTranslationUnits.
TEST_F(MayCallEdgesTest, ExportDoesNotDependOnTheOrderOfTheCompilationDatabase) {
    std::vector<std::string> order = fn_pointers_units();
    std::ranges::sort(order);
    const auto reference = analyze(fn_pointers(), order);
    ASSERT_NE(reference, nullptr);
    const std::string reference_export = export_of(*reference);
    while (std::ranges::next_permutation(order).found) {
        const auto shuffled = analyze(fn_pointers(), order);
        ASSERT_NE(shuffled, nullptr);
        EXPECT_EQ(export_of(*shuffled), reference_export) << order[0] << ' ' << order[1];
    }
}

/// The may_call edges of the fn_pointers fixture with its source files
/// renamed by `renaming`, sorted and without their file, which must be the
/// renamed core.c.
std::vector<MayCall> edges_after_renaming(const MayCallEdgesTest& test, const fs::path& root,
                                          const std::map<std::string, std::string>& renaming);

// Verifies: b26-function-pointer-tables-v1 (criterion 5) and
// nfr004-deterministic-output-v1: translation units renamed so that the
// tables are analysed before, between or after the calls give the same
// edges (sorted file names give the translation unit order, ADR-0019).
TEST_F(MayCallEdgesTest, EdgesDoNotDependOnTheOrderOfTranslationUnits) {
    const auto edge = [](const char* caller, const char* callee, const char* via,
                         std::uint32_t line, std::uint32_t column) {
        return MayCall{.caller = caller,
                       .callee = callee,
                       .via = via,
                       .file = {},
                       .line = line,
                       .column = column};
    };
    const std::vector<MayCall> expected{
        edge("run", "rtu_recv", "local.recv", 20, 48),
        edge("transfer", "rtu_recv", "backend.recv", 11, 19),
        edge("transfer", "rtu_send", "backend.send", 10, 16),
        edge("transfer", "tcp_send", "backend.send", 10, 16),
    };
    const std::vector<std::map<std::string, std::string>> renamings{
        {{"core.c", "core.c"}, {"rtu.c", "rtu.c"}, {"tcp.c", "tcp.c"}},
        {{"core.c", "c.c"}, {"rtu.c", "a.c"}, {"tcp.c", "b.c"}},
        {{"core.c", "b.c"}, {"rtu.c", "c.c"}, {"tcp.c", "a.c"}},
        {{"core.c", "a.c"}, {"rtu.c", "c.c"}, {"tcp.c", "b.c"}},
    };
    for (std::size_t i = 0; i < renamings.size(); ++i) {
        EXPECT_EQ(
            edges_after_renaming(*this, work_dir() / ("renamed" + std::to_string(i)), renamings[i]),
            expected)
            << "renaming " << i;
    }
}

std::vector<MayCall> edges_after_renaming(const MayCallEdgesTest& test, const fs::path& root,
                                          const std::map<std::string, std::string>& renaming) {
    const fs::path fixture = fs::path{LCART_FIXTURES_DIR} / "fn_pointers";
    fs::create_directories(root);
    fs::copy_file(fixture / "backend.h", root / "backend.h");
    std::vector<std::string> files;
    for (const auto& [original, renamed] : renaming) {
        fs::copy_file(fixture / original, root / renamed);
        files.push_back(renamed);
    }
    const auto store = test.analyze(root, files);
    if (store == nullptr) {
        return {};
    }
    std::vector<MayCall> edges = MayCallEdgesTest::may_calls(*store);
    for (MayCall& edge : edges) {
        EXPECT_EQ(edge.file, renaming.at("core.c")) << edge;
        edge.file.clear();
    }
    std::ranges::sort(edges);
    return edges;
}

// Verifies: b26-function-pointer-tables-v1 (criteria 1 and 4: an array of
// tables, `&function`, a function-local `static` table; a table with
// automatic storage is no static initializer; a may_call edge into another
// module is no module dependency).
TEST_F(MayCallEdgesTest, MayCallEdgesAreNoModuleDependencies) {
    const fs::path root = work_dir() / "project";
    write_module_project(root);
    const auto store = analyze(root, {"a/use.c", "b/impl.c"});
    ASSERT_NE(store, nullptr);

    EXPECT_EQ(may_calls(*store, "use"), (std::vector<MayCall>{
                                            {"use", "op_one", "ops.run", "a/use.c", 4, 12},
                                            {"use", "op_three", "ops.run", "a/use.c", 4, 12},
                                            {"use", "op_two", "ops.run", "a/use.c", 4, 12},
                                        }));
    EXPECT_EQ(may_calls(*store, "pick"), (std::vector<MayCall>{
                                             {"pick", "op_one", "chosen.run", "b/impl.c", 12, 12},
                                             {"pick", "op_three", "chosen.run", "b/impl.c", 12, 12},
                                             {"pick", "op_two", "chosen.run", "b/impl.c", 12, 12},
                                         }));
    EXPECT_EQ(may_calls(*store, "automatic"),
              (std::vector<MayCall>{
                  {"automatic", "op_one", "temporary.run", "b/impl.c", 17, 12},
                  {"automatic", "op_three", "temporary.run", "b/impl.c", 17, 12},
                  {"automatic", "op_two", "temporary.run", "b/impl.c", 17, 12},
              }));
    // The include of inc/ops.h; not `b`, whose functions `use` may call.
    EXPECT_EQ(dependencies_of(*store, "a"), (std::vector<std::string>{"inc"}));
}

} // namespace
