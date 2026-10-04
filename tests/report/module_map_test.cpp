// Verifies: fr007-module-map-without-llm-v1 (criterion 3: the same database
// gives the same map; the module list and Mermaid graph from facts only),
// nfr004-deterministic-output-v1 (criterion 1: golden map) and
// b13-module-map-v1 (golden file for the two_modules fixture), in the
// format of ADR-0017.
#include "facts/sqlite_fact_store.hpp"
#include "report/report_writer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace lcart::facts;
using lcart::report::write_module_map;

template <typename T>
T value_of(lcart::common::Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

void expect_ok(const lcart::common::Status& status) {
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

std::string read_file(const fs::path& path) {
    // Not istreambuf_iterator: GCC 14 at -O3 reports -Wnull-dereference in it.
    const std::ifstream in{path, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return std::move(text).str();
}

std::string map_of(const FactQuery& facts) {
    std::ostringstream out;
    expect_ok(write_module_map(facts, out));
    return std::move(out).str();
}

std::unique_ptr<SqliteFactStore> empty_store() {
    return value_of(SqliteFactStore::open(":memory:"));
}

FunctionFact definition(const char* name, FileId file, std::uint32_t line) {
    return {.usr = std::string{"c:@F@"} + name,
            .name = name,
            .qualified_name = name,
            .signature = "int (void)",
            .file = file,
            .line_begin = line,
            .line_end = line + 2,
            .is_definition = true};
}

/// The facts `lcart analyze` records for the two_modules fixture (its
/// README and tests/golden/two_modules.facts.jsonl), external headers
/// with this machine's kind of absolute path.
std::unique_ptr<SqliteFactStore> two_modules_facts() {
    auto store = empty_store();
    const ModuleId app = value_of(store->add_module({.name = "app"}));
    const ModuleId core = value_of(store->add_module({.name = "core"}));
    const auto add_file = [&store](const char* path, std::optional<ModuleId> module) {
        const std::string_view text{path};
        return value_of(store->add_file({.path = path,
                                         .content_sha256 = "h",
                                         .is_header = text.ends_with(".h"),
                                         .is_external = !module,
                                         .module = module}));
    };
    const FileId core_h = add_file("core/core.h", core);
    const FileId core_c = add_file("core/core.c", core);
    const FileId app_h = add_file("app/app.h", app);
    const FileId main_c = add_file("app/main.c", app);
    const FileId legacy_c = add_file("app/legacy.c", app);
    const FileId stddef_h =
        add_file("/usr/lib/llvm-20/lib/clang/20/include/stddef.h", std::nullopt);
    const FileId string_h = add_file("/usr/include/string.h", std::nullopt);
    const FileId stdio_h = add_file("/usr/include/stdio.h", std::nullopt);

    const auto include = [&store](FileId includer, FileId included, const char* spelled,
                                  std::uint32_t line, bool system) {
        expect_ok(store->add_include({.includer = includer,
                                      .included = included,
                                      .spelled = spelled,
                                      .line = line,
                                      .is_system = system}));
    };
    include(core_h, stddef_h, "stddef.h", 4, true);
    include(core_c, core_h, "core.h", 1, false);
    include(core_c, string_h, "string.h", 3, true);
    include(app_h, core_h, "core/core.h", 4, false);
    include(main_c, app_h, "app.h", 1, false);
    include(main_c, stdio_h, "stdio.h", 3, true);
    include(legacy_c, app_h, "app.h", 1, false);

    const FunctionId core_length =
        value_of(store->add_function(definition("core_length", core_c, 5)));
    const FunctionId app_run = value_of(store->add_function(definition("app_run", main_c, 5)));
    const FunctionId main = value_of(store->add_function(definition("main", main_c, 9)));
    const FunctionId legacy_run =
        value_of(store->add_function(definition("legacy_run", legacy_c, 3)));
    const auto call = [&store](FunctionId caller, FunctionId callee, FileId site_file,
                               std::uint32_t line, std::uint32_t column) {
        expect_ok(store->add_call({.caller = caller,
                                   .callee = callee,
                                   .site = {.file = site_file, .line = line, .column = column}}));
    };
    call(app_run, core_length, main_c, 6, 28);
    call(main, app_run, main_c, 10, 12);
    call(legacy_run, app_run, legacy_c, 4, 12);
    expect_ok(store->derive_module_dependencies());
    expect_ok(store->set_coverage({.translation_units_total = 3, .translation_units_parsed = 3}));
    return store;
}

// B-13: golden file for the two_modules fixture. External headers are left
// out; the CLI test checks the same file against a real analysis.
TEST(ModuleMap, TwoModulesEqualsTheGoldenFile) {
    const std::string golden = read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.map.md");
    ASSERT_FALSE(golden.empty());
    const std::string map = map_of(*two_modules_facts());
    EXPECT_EQ(map, golden);
    EXPECT_EQ(map.find("stddef"), std::string::npos);
    EXPECT_EQ(map.find("/usr"), std::string::npos);
}

// FR-007 criterion 3 (NFR-004): the same database twice, and the same facts
// inserted in another order, give the same bytes.
TEST(ModuleMap, SameFactsGiveTheSameBytes) {
    const auto store = two_modules_facts();
    const std::string first = map_of(*store);
    EXPECT_EQ(map_of(*store), first);

    auto reversed = empty_store();
    const ModuleId zeta = value_of(reversed->add_module({.name = "zeta"}));
    const ModuleId alpha = value_of(reversed->add_module({.name = "alpha"}));
    const FileId z2 = value_of(reversed->add_file({.path = "z/2.c", .module = zeta}));
    const FileId z1 = value_of(reversed->add_file({.path = "z/1.c", .module = zeta}));
    const FileId a1 = value_of(reversed->add_file({.path = "a/1.c", .module = alpha}));
    static_cast<void>(value_of(reversed->add_function(definition("f", z2, 1))));
    static_cast<void>(value_of(reversed->add_function(definition("g", z1, 1))));
    expect_ok(reversed->add_include({.includer = z1, .included = a1, .spelled = "1.c", .line = 1}));
    expect_ok(reversed->derive_module_dependencies());

    auto in_order = empty_store();
    const ModuleId alpha2 = value_of(in_order->add_module({.name = "alpha"}));
    const ModuleId zeta2 = value_of(in_order->add_module({.name = "zeta"}));
    const FileId a1b = value_of(in_order->add_file({.path = "a/1.c", .module = alpha2}));
    const FileId z1b = value_of(in_order->add_file({.path = "z/1.c", .module = zeta2}));
    const FileId z2b = value_of(in_order->add_file({.path = "z/2.c", .module = zeta2}));
    static_cast<void>(value_of(in_order->add_function(definition("g", z1b, 1))));
    static_cast<void>(value_of(in_order->add_function(definition("f", z2b, 1))));
    expect_ok(
        in_order->add_include({.includer = z1b, .included = a1b, .spelled = "1.c", .line = 1}));
    expect_ok(in_order->derive_module_dependencies());

    EXPECT_EQ(map_of(*reversed), map_of(*in_order));
}

// ADR-0017: function counts count definitions, not declarations.
TEST(ModuleMap, FunctionCountsCountDefinitionsOnly) {
    auto store = empty_store();
    const ModuleId lib = value_of(store->add_module({.name = "lib"}));
    const FileId header =
        value_of(store->add_file({.path = "lib/x.h", .is_header = true, .module = lib}));
    FunctionFact declared = definition("only_declared", header, 3);
    declared.is_definition = false;
    static_cast<void>(value_of(store->add_function(declared)));
    static_cast<void>(value_of(store->add_function(definition("inline_one", header, 5))));

    const std::string map = map_of(*store);
    EXPECT_NE(map.find("- `lib`: 1 file, 1 function definition\n"), std::string::npos) << map;
    EXPECT_NE(map.find("- `lib/x.h`: 1 function definition\n"), std::string::npos) << map;
}

// ADR-0017: each edge is labelled with its include and call sites;
// singular and plural, zero parts left out.
TEST(ModuleMap, EdgeLabelsCountIncludeAndCallSites) {
    auto store = empty_store();
    const ModuleId a = value_of(store->add_module({.name = "a"}));
    const ModuleId b = value_of(store->add_module({.name = "b"}));
    const ModuleId c = value_of(store->add_module({.name = "c"}));
    const FileId a_c = value_of(store->add_file({.path = "a/a.c", .module = a}));
    const FileId b_h = value_of(store->add_file({.path = "b/b.h", .is_header = true, .module = b}));
    const FileId b_c = value_of(store->add_file({.path = "b/b.c", .module = b}));
    const FileId c_c = value_of(store->add_file({.path = "c/c.c", .module = c}));
    expect_ok(store->add_include({.includer = a_c, .included = b_h, .spelled = "b.h", .line = 1}));
    expect_ok(store->add_include({.includer = a_c, .included = b_h, .spelled = "b.h", .line = 2}));
    const FunctionId in_b = value_of(store->add_function(definition("in_b", b_c, 1)));
    const FunctionId in_c = value_of(store->add_function(definition("in_c", c_c, 1)));
    for (const std::uint32_t line : {2U, 3U, 4U}) {
        expect_ok(store->add_call(
            {.caller = in_b, .callee = in_c, .site = {.file = b_c, .line = line, .column = 1}}));
    }
    // An indirect call justifies no dependency (FR-005).
    expect_ok(store->add_call({.caller = in_b,
                               .callee = std::nullopt,
                               .kind = CallKind::indirect,
                               .via = "ops.run",
                               .site = {.file = b_c, .line = 5, .column = 1}}));
    expect_ok(store->derive_module_dependencies());

    const std::string map = map_of(*store);
    EXPECT_NE(map.find("    m0 -->|\"2 includes\"| m1\n"
                       "    m1 -->|\"3 calls\"| m2\n"
                       "```\n"),
              std::string::npos)
        << map;
}

// ADR-0017: Mermaid labels are generated ids with escaped names; Markdown
// names are code spans that no character can break.
TEST(ModuleMap, AwkwardNamesAreEscaped) {
    auto store = empty_store();
    const std::vector<std::string> names{"say \"hi\"", "<b>x</b>", "back`tick",   "a|b",
                                         "`edge`",     "#35;",     "caf\xC3\xA9", "two\nlines",
                                         "bad\xFF",    "a&b:%%c"};
    std::vector<FileId> files;
    for (const std::string& name : names) {
        const ModuleId module = value_of(store->add_module({.name = name}));
        files.push_back(
            value_of(store->add_file({.path = "dir/" + name + ".c", .module = module})));
    }
    // a|b depends on say "hi".
    expect_ok(store->add_include(
        {.includer = files[3], .included = files[0], .spelled = "x", .line = 1}));
    expect_ok(store->derive_module_dependencies());

    const std::string map = map_of(*store);
    const std::string replacement = "\xEF\xBF\xBD";
    // Byte order: # < ` a b c s t ("a&" before "a|", "bad" after "back").
    const std::string graph = "```mermaid\n"
                              "flowchart LR\n"
                              "    m0[\"#35;35#59;\"]\n"
                              "    m1[\"#60;b#62;x#60;/b#62;\"]\n"
                              "    m2[\"#96;edge#96;\"]\n"
                              "    m3[\"a#38;b#58;#37;#37;c\"]\n"
                              "    m4[\"a#124;b\"]\n"
                              "    m5[\"back#96;tick\"]\n"
                              "    m6[\"bad#65533;\"]\n"
                              "    m7[\"caf#233;\"]\n"
                              "    m8[\"say #34;hi#34;\"]\n"
                              "    m9[\"two#65533;lines\"]\n"
                              "    m4 -->|\"1 include\"| m8\n"
                              "```\n";
    EXPECT_NE(map.find(graph), std::string::npos) << map;
    for (const std::string& line :
         {std::string{"- `#35;`: 1 file, 0 function definitions\n"},
          std::string{"- `<b>x</b>`: 1 file"}, std::string{"- ``back`tick``: 1 file"},
          std::string{"- `a|b`: 1 file"}, std::string{"- `a&b:%%c`: 1 file"},
          std::string{"- `` `edge` ``: 1 file"}, std::string{"- `caf\xC3\xA9`: 1 file"},
          std::string{"- `say \"hi\"`: 1 file"}, "- `two" + replacement + "lines`: 1 file",
          "- `bad" + replacement + "`: 1 file", std::string{"### Module `` `edge` ``\n"},
          std::string{"- ``dir/`edge`.c``: 0 function"},
          "- `dir/two" + replacement + "lines.c`: 0 function"}) {
        EXPECT_NE(map.find(line), std::string::npos) << line << "\n" << map;
    }
    // Nothing raw that would end a label, a line or a code span.
    EXPECT_EQ(map.find("two\nlines"), std::string::npos);
    EXPECT_EQ(map.find('\xFF'), std::string::npos);
}

/// The facts of `inner`, except that the file at `path` belongs to no
/// module. lcart's own database cannot hold such a file (FR-005 assigns
/// every file under the root), but FactQuery does not promise that.
class FileWithoutModule final : public FactQuery {
public:
    FileWithoutModule(const FactQuery& inner, std::string path)
        : inner_(&inner), path_(std::move(path)) {}

    [[nodiscard]] lcart::common::Result<std::optional<ModuleFact>>
    find_module(std::string_view name) const override {
        return inner_->find_module(name);
    }
    [[nodiscard]] lcart::common::Result<std::optional<FileFact>>
    find_file(std::string_view path) const override {
        return stripped(inner_->find_file(path));
    }
    [[nodiscard]] lcart::common::Result<std::optional<FileFact>> file(FileId id) const override {
        return stripped(inner_->file(id));
    }
    [[nodiscard]] lcart::common::Result<std::vector<ModuleFact>> modules() const override {
        return inner_->modules();
    }
    [[nodiscard]] lcart::common::Result<std::vector<FileFact>> files() const override {
        auto files = inner_->files();
        if (files) {
            for (FileFact& file : *files) {
                strip(file);
            }
        }
        return files;
    }
    [[nodiscard]] lcart::common::Result<std::vector<FunctionFact>>
    find_functions(std::string_view name) const override {
        return inner_->find_functions(name);
    }
    [[nodiscard]] lcart::common::Result<std::optional<FunctionFact>>
    find_function_by_usr(std::string_view usr) const override {
        return inner_->find_function_by_usr(usr);
    }
    [[nodiscard]] lcart::common::Result<std::optional<FunctionFact>>
    function(FunctionId id) const override {
        return inner_->function(id);
    }
    [[nodiscard]] lcart::common::Result<std::vector<FunctionFact>> functions() const override {
        return inner_->functions();
    }
    [[nodiscard]] lcart::common::Result<std::vector<CallFact>>
    calls_from(FunctionId caller) const override {
        return inner_->calls_from(caller);
    }
    [[nodiscard]] lcart::common::Result<std::vector<CallFact>>
    calls_to(FunctionId callee) const override {
        return inner_->calls_to(callee);
    }
    [[nodiscard]] lcart::common::Result<std::vector<CallFact>> calls() const override {
        return inner_->calls();
    }
    [[nodiscard]] lcart::common::Result<std::vector<IncludeFact>>
    includes_of(FileId includer) const override {
        return inner_->includes_of(includer);
    }
    [[nodiscard]] lcart::common::Result<std::vector<IncludeFact>>
    included_by(FileId included) const override {
        return inner_->included_by(included);
    }
    [[nodiscard]] lcart::common::Result<std::vector<IncludeFact>> includes() const override {
        return inner_->includes();
    }
    [[nodiscard]] lcart::common::Result<std::vector<ModuleFact>>
    module_dependencies(ModuleId module) const override {
        return inner_->module_dependencies(module);
    }
    [[nodiscard]] lcart::common::Result<std::vector<ModuleDependencyFact>>
    module_dependency_edges(ModuleId module) const override {
        return inner_->module_dependency_edges(module);
    }
    [[nodiscard]] lcart::common::Result<std::vector<DiagnosticFact>> diagnostics() const override {
        return inner_->diagnostics();
    }
    [[nodiscard]] lcart::common::Result<AnalysisCoverage> coverage() const override {
        return inner_->coverage();
    }

private:
    void strip(FileFact& file) const {
        if (file.path == path_) {
            file.module = std::nullopt;
        }
    }
    [[nodiscard]] lcart::common::Result<std::optional<FileFact>>
    stripped(lcart::common::Result<std::optional<FileFact>> file) const {
        if (file && *file) {
            strip(**file);
        }
        return file;
    }

    const FactQuery* inner_;
    std::string path_;
};

// ADR-0017: files under the root that belong to no module are listed under
// "(unassigned)", after the modules; external files are never listed.
TEST(ModuleMap, FilesInNoModuleAreListedAsUnassigned) {
    const auto store = two_modules_facts();
    const FileWithoutModule facts{*store, "app/main.c"};
    const std::string map = map_of(facts);
    EXPECT_NE(map.find("- `app`: 2 files, 1 function definition\n"
                       "- `core`: 2 files, 1 function definition\n"
                       "- (unassigned): 1 file, 2 function definitions\n"),
              std::string::npos)
        << map;
    EXPECT_TRUE(map.ends_with("### (unassigned)\n"
                              "\n"
                              "- `app/main.c`: 2 function definitions\n"))
        << map;
    EXPECT_EQ(map.find("stdio"), std::string::npos) << map;
}

TEST(ModuleMap, NoModulesAndModulesWithoutFiles) {
    const auto empty = empty_store();
    const std::string none = map_of(*empty);
    EXPECT_NE(none.find("Translation units parsed: 0 of 0.\n"), std::string::npos) << none;
    EXPECT_EQ(none.find("```mermaid"), std::string::npos) << none;
    EXPECT_NE(none.find("## Modules\n\nNo modules.\n"), std::string::npos) << none;

    auto lonely = empty_store();
    static_cast<void>(value_of(lonely->add_module({.name = "lonely"})));
    const std::string map = map_of(*lonely);
    EXPECT_NE(map.find("    m0[\"lonely\"]\n```\n"), std::string::npos) << map;
    EXPECT_NE(map.find("- `lonely`: 0 files, 0 function definitions\n"), std::string::npos) << map;
    EXPECT_TRUE(map.ends_with("### Module `lonely`\n\nNo files.\n")) << map;
}

TEST(ModuleMap, FailedStreamIsAnError) {
    const auto store = two_modules_facts();
    std::ostringstream out;
    out.setstate(std::ios::badbit);
    EXPECT_FALSE(write_module_map(*store, out).has_value());
}

} // namespace
