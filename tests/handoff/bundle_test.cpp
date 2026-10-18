// Verifies: b30-facts-only-handoff-bundle-v1 (criterion 2: PROMPT.md,
// CONTRACT.md and claim-schema.json are the texts of the pages; criterion 3:
// absolute spellings show `<outside-root>/` and the file name; criterion 5:
// MANIFEST.json with contract version, run ID and options; criterion 6:
// may_call edges apart in calls.json, prompt rule 5), fr008-generate-documentation-v2
// (criterion 1: PROMPT.md matches the prompt in the contract),
// nfr004-deterministic-output-v1 (the same facts give the same bytes),
// claim-handoff-bundle-layout-v2, claim-handoff-facts-files-v1,
// claim-handoff-bundle-paths-v1, adr0028-paths-outside-root-v1,
// adr0028-texts-from-the-pages-v1, adr0028-run-id-v1, adr0028-project-name-v1,
// adr0028-json-format-v1 and adr0028-command-line-v1 (replacing only an
// earlier bundle).
#include "common/sha256.hpp"
#include "common/version.hpp"
#include "facts/fact_export.hpp"
#include "facts/sqlite_fact_store.hpp"
#include "handoff/handoff.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace lcart::facts;
using lcart::common::ErrorCode;
using lcart::handoff::build_bundle;
using lcart::handoff::Bundle;
using Json = nlohmann::json;

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

/// The facts of the two_modules fixture (tests/golden/two_modules.facts.jsonl),
/// plus a call through a function-pointer table in core/core.c, so that
/// calls.json has every kind of edge. `reversed` adds modules, files and
/// functions in the opposite order, which changes every row id.
std::unique_ptr<SqliteFactStore> facts_of_two_modules(bool reversed = false) {
    auto store = value_of(SqliteFactStore::open(":memory:"));
    std::vector<std::string> module_names{"app", "core"};
    if (reversed) {
        std::ranges::reverse(module_names);
    }
    std::map<std::string, ModuleId> module_ids;
    for (const std::string& name : module_names) {
        module_ids[name] = value_of(store->add_module({.name = name}));
    }
    const ModuleId app = module_ids["app"];
    const ModuleId core = module_ids["core"];
    std::vector<std::pair<std::string, std::optional<ModuleId>>> paths{
        {"core/core.h", core},
        {"core/core.c", core},
        {"app/app.h", app},
        {"app/main.c", app},
        {"app/legacy.c", app},
        {"/usr/lib/llvm-20/lib/clang/20/include/stddef.h", std::nullopt},
        {"/usr/include/string.h", std::nullopt},
        {"/usr/include/stdio.h", std::nullopt}};
    if (reversed) {
        std::ranges::reverse(paths);
    }
    for (const auto& [path, module] : paths) {
        value_of(store->add_file({.path = path,
                                  .content_sha256 = "h",
                                  .is_header = path.ends_with(".h"),
                                  .is_external = !module,
                                  .module = module}));
    }
    const auto file = [&store](const char* path) {
        return value_of(store->find_file(path)).value().id;
    };
    const auto include = [&store, &file](const char* includer, const char* included,
                                         const char* spelled, std::uint32_t line, bool system) {
        expect_ok(store->add_include({.includer = file(includer),
                                      .included = file(included),
                                      .spelled = spelled,
                                      .line = line,
                                      .is_system = system}));
    };
    include("core/core.h", "/usr/lib/llvm-20/lib/clang/20/include/stddef.h", "stddef.h", 4, true);
    include("core/core.c", "core/core.h", "core.h", 1, false);
    include("core/core.c", "/usr/include/string.h", "string.h", 3, true);
    include("app/app.h", "core/core.h", "core/core.h", 4, false);
    include("app/main.c", "app/app.h", "app.h", 1, false);
    include("app/main.c", "/usr/include/stdio.h", "stdio.h", 3, true);
    include("app/legacy.c", "app/app.h", "app.h", 1, false);

    std::vector<FunctionFact> functions{definition("core_length", file("core/core.c"), 5),
                                        definition("core_send", file("core/core.c"), 9),
                                        definition("core_dispatch", file("core/core.c"), 13),
                                        definition("app_run", file("app/main.c"), 5),
                                        definition("main", file("app/main.c"), 9),
                                        definition("legacy_run", file("app/legacy.c"), 3)};
    if (reversed) {
        std::ranges::reverse(functions);
    }
    for (const FunctionFact& function : functions) {
        value_of(store->add_function(function));
    }
    const auto function = [&store](const char* usr) {
        return value_of(store->find_function_by_usr(usr)).value().id;
    };
    const auto call = [&store, &function, &file](const char* caller, const char* callee,
                                                 const char* site, std::uint32_t line,
                                                 std::uint32_t column) {
        expect_ok(store->add_call({.caller = function(caller),
                                   .callee = function(callee),
                                   .site = {.file = file(site), .line = line, .column = column}}));
    };
    call("c:@F@app_run", "c:@F@core_length", "app/main.c", 6, 28);
    call("c:@F@main", "c:@F@app_run", "app/main.c", 10, 12);
    call("c:@F@legacy_run", "c:@F@app_run", "app/legacy.c", 4, 12);
    constexpr std::string_view send_field = "c:@S@backend@FI@send";
    expect_ok(store->add_call({.caller = function("c:@F@core_dispatch"),
                               .callee = std::nullopt,
                               .kind = CallKind::indirect,
                               .via = "backend.send",
                               .site = {.file = file("core/core.c"), .line = 14, .column = 12},
                               .field = std::string{send_field}}));
    expect_ok(store->add_field_assignment(
        {.field = std::string{send_field}, .function = function("c:@F@core_send")}));
    expect_ok(store->derive_may_call_edges());
    expect_ok(store->derive_module_dependencies());
    expect_ok(store->set_coverage({.translation_units_total = 3, .translation_units_parsed = 3}));
    return store;
}

Bundle bundle_of(const FactQuery& facts, std::vector<std::string> modules = {}) {
    return value_of(build_bundle(
        facts,
        {.project_name = "two_modules", .modules = std::move(modules), .include_source = false}));
}

std::string file_of(const Bundle& bundle, const std::string& path) {
    const auto found = bundle.files.find(path);
    EXPECT_NE(found, bundle.files.end()) << path;
    return found == bundle.files.end() ? std::string{} : found->second;
}

Json json_of(const Bundle& bundle, const std::string& path) {
    return Json::parse(file_of(bundle, path));
}

// ---- the texts of the pages, extracted here on their own ------------------

std::string page(std::string_view name) {
    return read_file(fs::path{LCART_DOCS_DIR} / "specs" / name);
}

/// The fenced block opened by "```<language>" after the anchor whose ID
/// starts with `anchor_prefix`, without its fences.
std::string fenced_block(const std::string& text, std::string_view anchor_prefix,
                         std::string_view language) {
    const std::size_t anchor = text.find("<a id=\"" + std::string{anchor_prefix});
    if (anchor == std::string::npos) {
        ADD_FAILURE() << "no anchor " << anchor_prefix;
        return {};
    }
    const std::string opening = "```" + std::string{language} + "\n";
    const std::size_t begin = text.find(opening, anchor) + opening.size();
    const std::size_t end = text.find("\n```\n", begin);
    return text.substr(begin, end + 1 - begin);
}

/// The section headed by the anchor `anchor_prefix`, as a document with a
/// `#` heading.
std::string section_document(const std::string& text, std::string_view anchor_prefix) {
    const std::size_t heading = text.find("## <a id=\"" + std::string{anchor_prefix});
    if (heading == std::string::npos) {
        ADD_FAILURE() << "no section " << anchor_prefix;
        return {};
    }
    const std::size_t title = text.find("</a>", heading) + 4;
    const std::size_t end_of_heading = text.find('\n', heading);
    std::string body =
        text.substr(end_of_heading, text.find("\n## ", end_of_heading) - end_of_heading);
    while (body.ends_with('\n')) {
        body.pop_back();
    }
    return "# " + text.substr(title, end_of_heading - title) + body + "\n";
}

void replace_all(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

/// The prompt of the page with its values filled in, keeping the
/// facts-only paragraph without its mark and dropping the with-source one.
std::string facts_only_prompt(std::string prompt, const std::string& run_id) {
    replace_all(prompt, "{{project.name}}", "two_modules");
    replace_all(prompt, "{{coverage.parsed}}", "3");
    replace_all(prompt, "{{coverage.total}}", "3");
    replace_all(prompt, "{{output_dir}}", "../../agent/" + run_id + "/");
    std::istringstream lines{prompt};
    std::string result;
    bool in_with_source = false;
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("- *with-source:* ")) {
            in_with_source = true;
            continue;
        }
        if (in_with_source && line.starts_with("  ")) {
            continue;
        }
        in_with_source = false;
        replace_all(line, "- *facts-only:* ", "- ");
        result += line + "\n";
    }
    return result;
}

// ---- criterion 2 -----------------------------------------------------------

// The page still has what the bundle replaces, so the next test compares
// more than a copy.
TEST(HandoffBundle, ThePromptOfThePageHasValuesAndAlternativeParagraphs) {
    const std::string prompt_template =
        fenced_block(page("handoff-contract.md"), "claim-handoff-prompt-v", "markdown");
    for (const char* part : {"{{project.name}}", "{{coverage.parsed}}", "{{coverage.total}}",
                             "{{output_dir}}", "- *facts-only:* ", "- *with-source:* "}) {
        EXPECT_NE(prompt_template.find(part), std::string::npos) << part;
    }
}

TEST(HandoffBundle, PromptIsThePromptOfTheContractPageFilledIn) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    const std::string prompt_template =
        fenced_block(page("handoff-contract.md"), "claim-handoff-prompt-v", "markdown");
    const std::string prompt = file_of(bundle, "PROMPT.md");
    EXPECT_EQ(prompt, facts_only_prompt(prompt_template, bundle.run_id));
    EXPECT_EQ(prompt.find("{{"), std::string::npos);
    EXPECT_EQ(prompt.find("with-source"), std::string::npos);
    EXPECT_NE(prompt.find("\n- There is no source code in this bundle."), std::string::npos);
    EXPECT_NE(prompt.find("`../../agent/" + bundle.run_id + "/`"), std::string::npos);
}

TEST(HandoffBundle, ContractIsTheAgentOutputSectionOfThePage) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    const std::string contract = file_of(bundle, "CONTRACT.md");
    EXPECT_EQ(contract, section_document(page("handoff-contract.md"), "claim-agent-output-v"));
    EXPECT_TRUE(contract.starts_with("# Agent output (contract v1)\n\nThe agent writes"))
        << contract;
    // No link that leads nowhere inside the bundle.
    EXPECT_EQ(contract.find("]("), std::string::npos);
}

TEST(HandoffBundle, ClaimSchemaIsTheSchemaBlockOfThePage) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    const std::string schema = file_of(bundle, "claim-schema.json");
    EXPECT_EQ(schema, fenced_block(page("claim-schema.md"), "claim-json-schema-v", "json"));
    EXPECT_EQ(Json::parse(schema)["$id"], "lcart:claim:v1");
}

// ---- criterion 5 -----------------------------------------------------------

TEST(HandoffBundle, ManifestRecordsContractVersionRunIdAndOptions) {
    const Bundle bundle = bundle_of(*facts_of_two_modules(), {"core", "app"});
    const Json manifest = json_of(bundle, "MANIFEST.json");
    EXPECT_EQ(manifest["contract_version"], 1);
    EXPECT_EQ(manifest["format"], "lcart-handoff");
    EXPECT_EQ(manifest["run_id"], bundle.run_id);
    EXPECT_EQ(manifest["options"],
              Json::parse(R"({"include_source":false,"modules":["app","core"]})"));
    EXPECT_EQ(manifest["source_excerpts"], Json::array());
}

/// The paths MANIFEST.json lists, each checked against its SHA-256.
std::vector<std::string> listed_files(const Bundle& bundle) {
    std::vector<std::string> listed;
    const Json manifest = json_of(bundle, "MANIFEST.json");
    for (const Json& file : manifest["files"]) {
        const std::string path = file["path"];
        listed.push_back(path);
        EXPECT_EQ(file["sha256"], lcart::common::sha256_hex(file_of(bundle, path))) << path;
    }
    return listed;
}

TEST(HandoffBundle, ManifestListsEveryOtherFileWithItsHash) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    const std::vector<std::string> listed = listed_files(bundle);
    EXPECT_EQ(listed, (std::vector<std::string>{"CONTRACT.md", "PROMPT.md", "claim-schema.json",
                                                "facts/calls.json", "facts/functions.json",
                                                "facts/includes.json", "facts/modules.json",
                                                "facts/project.json"}));
    EXPECT_EQ(bundle.files.size(), listed.size() + 1);
}

TEST(HandoffBundle, RunIdIsTheHashOfTheExportAndTheOptions) {
    const auto facts = facts_of_two_modules();
    std::ostringstream export_text;
    expect_ok(write_jsonl_export(*facts, export_text));
    const auto run_id_of = [&export_text](std::string_view options) {
        return lcart::common::sha256_hex(export_text.str() + std::string{options} + "\n")
            .substr(0, 12);
    };
    const Bundle all = bundle_of(*facts);
    EXPECT_EQ(
        all.run_id,
        run_id_of(R"({"contract_version":1,"options":{"include_source":false,"modules":[]}})"));
    const Bundle core = bundle_of(*facts, {"core"});
    EXPECT_EQ(
        core.run_id,
        run_id_of(
            R"({"contract_version":1,"options":{"include_source":false,"modules":["core"]}})"));
    EXPECT_NE(all.run_id, core.run_id);
}

// ---- NFR-004, JSON format --------------------------------------------------

TEST(HandoffBundle, SameFactsGiveTheSameBytes) {
    const Bundle first = bundle_of(*facts_of_two_modules());
    EXPECT_EQ(first.files, bundle_of(*facts_of_two_modules()).files);
    EXPECT_EQ(first.files, bundle_of(*facts_of_two_modules(true)).files);
    EXPECT_EQ(first.run_id, bundle_of(*facts_of_two_modules(true)).run_id);
}

TEST(HandoffBundle, JsonIsSortedWithTwoSpacesOfIndentation) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    for (const auto& [path, content] : bundle.files) {
        if (!path.ends_with(".json") || path == "claim-schema.json") {
            continue;
        }
        // nlohmann_json keeps object keys sorted, so a sorted, indented
        // document is the one it writes back.
        const Json parsed = Json::parse(content);
        EXPECT_EQ(content, parsed.dump(2) + "\n") << path;
    }
}

TEST(HandoffBundle, ProjectRecordsNameCoverageAndVersions) {
    const Json project = json_of(bundle_of(*facts_of_two_modules()), "facts/project.json");
    EXPECT_EQ(project["name"], "two_modules");
    EXPECT_EQ(project["coverage"], Json::parse(R"({"parsed":3,"total":3})"));
    EXPECT_EQ(project["fact_schema_version"], lcart::facts::schema_version);
    EXPECT_EQ(project["tool_version"], std::string{lcart::common::version});
}

TEST(HandoffBundle, ModulesListFilesAndDependencyEdges) {
    const Json modules = json_of(bundle_of(*facts_of_two_modules()), "facts/modules.json");
    EXPECT_EQ(modules, Json::parse(R"({"modules": [
        {"name": "app", "files": ["app/app.h", "app/legacy.c", "app/main.c"],
         "depends_on": [{"module": "core", "edges": [
            {"via": "include", "file": "app/app.h", "line": 4, "column": null},
            {"via": "call", "file": "app/main.c", "line": 6, "column": 28}]}]},
        {"name": "core", "files": ["core/core.c", "core/core.h"], "depends_on": []}]})"));
}

TEST(HandoffBundle, FunctionsPerModule) {
    const Json functions = json_of(bundle_of(*facts_of_two_modules()), "facts/functions.json");
    const auto function = [](const char* name, const char* file, int line) {
        return Json{{"name", name},       {"qualified_name", name}, {"signature", "int (void)"},
                    {"file", file},       {"line_begin", line},     {"line_end", line + 2},
                    {"definition", true}, {"linkage", "external"}};
    };
    EXPECT_EQ(functions,
              (Json{{"modules",
                     {{{"name", "app"},
                       {"functions",
                        {function("legacy_run", "app/legacy.c", 3),
                         function("app_run", "app/main.c", 5), function("main", "app/main.c", 9)}}},
                      {{"name", "core"},
                       {"functions",
                        {function("core_length", "core/core.c", 5),
                         function("core_send", "core/core.c", 9),
                         function("core_dispatch", "core/core.c", 13)}}}}}}));
}

// ---- criterion 6 -----------------------------------------------------------

TEST(HandoffBundle, CallsListMayCallEdgesApartFromDirectAndIndirectCalls) {
    const Json calls = json_of(bundle_of(*facts_of_two_modules()), "facts/calls.json");
    EXPECT_EQ(calls, Json::parse(R"({
        "direct": [
            {"caller": "legacy_run", "caller_file": "app/legacy.c", "callee": "app_run",
             "callee_file": "app/main.c", "file": "app/legacy.c", "line": 4, "column": 12},
            {"caller": "app_run", "caller_file": "app/main.c", "callee": "core_length",
             "callee_file": "core/core.c", "file": "app/main.c", "line": 6, "column": 28},
            {"caller": "main", "caller_file": "app/main.c", "callee": "app_run",
             "callee_file": "app/main.c", "file": "app/main.c", "line": 10, "column": 12}],
        "indirect": [
            {"caller": "core_dispatch", "caller_file": "core/core.c", "via": "backend.send",
             "file": "core/core.c", "line": 14, "column": 12}],
        "may_call": [
            {"caller": "core_dispatch", "caller_file": "core/core.c", "callee": "core_send",
             "callee_file": "core/core.c", "via": "backend.send", "file": "core/core.c",
             "line": 14, "column": 12}]})"));
}

TEST(HandoffBundle, PromptRuleFiveSaysMayCallEdgesAreCandidatesNotCalls) {
    const std::string prompt = file_of(bundle_of(*facts_of_two_modules()), "PROMPT.md");
    const std::size_t rule = prompt.find("\n5. ");
    ASSERT_NE(rule, std::string::npos);
    const std::string rule_five = prompt.substr(rule, prompt.find("\n6. ") - rule);
    EXPECT_NE(rule_five.find("`may_call` edge is a candidate, not a\n   call"), std::string::npos)
        << rule_five;
    EXPECT_NE(rule_five.find("never back that with\n   a `calls` claim"), std::string::npos)
        << rule_five;
}

TEST(HandoffBundle, IncludesKeepTheExportsNamesOfExternalFiles) {
    const Json includes = json_of(bundle_of(*facts_of_two_modules()), "facts/includes.json");
    ASSERT_EQ(includes["includes"].size(), 7U);
    EXPECT_EQ(includes["includes"][3], Json::parse(R"({"file": "app/main.c", "line": 3,
        "spelled": "stdio.h", "system": true, "target": "stdio.h", "target_external": true})"));
}

// ---- --module ---------------------------------------------------------------

std::vector<std::string> values_of(const Json& array, const char* key) {
    std::vector<std::string> values;
    for (const Json& item : array) {
        values.push_back(item[key]);
    }
    return values;
}

TEST(HandoffBundle, ModuleSelectionKeepsTheModulesAndTheirEdges) {
    const Bundle bundle = bundle_of(*facts_of_two_modules(), {"core"});
    const Json modules = json_of(bundle, "facts/modules.json");
    ASSERT_EQ(modules["modules"].size(), 1U);
    EXPECT_EQ(modules["modules"][0]["name"], "core");
    const Json functions = json_of(bundle, "facts/functions.json");
    ASSERT_EQ(functions["modules"].size(), 1U);
    EXPECT_EQ(functions["modules"][0]["name"], "core");
    const Json calls = json_of(bundle, "facts/calls.json");
    EXPECT_EQ(calls["direct"], Json::array());
    EXPECT_EQ(calls["indirect"].size(), 1U);
    EXPECT_EQ(calls["may_call"].size(), 1U);
    EXPECT_EQ(values_of(json_of(bundle, "facts/includes.json")["includes"], "file"),
              (std::vector<std::string>{"core/core.c", "core/core.c", "core/core.h"}));
}

// Calls into a selected module from another stay out; the other end of a
// kept edge is still named.
TEST(HandoffBundle, ModuleSelectionNamesTheOtherEndOfAnEdge) {
    const Json app_calls = json_of(bundle_of(*facts_of_two_modules(), {"app"}), "facts/calls.json");
    ASSERT_EQ(app_calls["direct"].size(), 3U);
    EXPECT_EQ(app_calls["direct"][1]["callee"], "core_length");
    EXPECT_EQ(app_calls["may_call"], Json::array());
}

TEST(HandoffBundle, UnknownModuleIsNotFound) {
    const auto facts = facts_of_two_modules();
    const auto bundle = build_bundle(*facts, {.project_name = "p", .modules = {"core", "nope"}});
    ASSERT_FALSE(bundle.has_value());
    EXPECT_EQ(bundle.error().code, ErrorCode::not_found);
    EXPECT_NE(bundle.error().message.find("nope"), std::string::npos) << bundle.error().message;
}

TEST(HandoffBundle, ModuleNamedTwiceAndIncludeSourceAreRefused) {
    const auto facts = facts_of_two_modules();
    const auto twice = build_bundle(*facts, {.project_name = "p", .modules = {"core", "core"}});
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().code, ErrorCode::invalid_argument);
    EXPECT_NE(twice.error().message.find("module core given twice"), std::string::npos)
        << twice.error().message;
    const auto with_source = build_bundle(*facts, {.project_name = "p", .include_source = true});
    ASSERT_FALSE(with_source.has_value());
    EXPECT_EQ(with_source.error().code, ErrorCode::invalid_argument);
    EXPECT_NE(with_source.error().message.find("--include-source is not available yet"),
              std::string::npos)
        << with_source.error().message;
}

// ---- criterion 3 -----------------------------------------------------------

/// `spelled`, `target` and `target_external` of each include record.
std::vector<Json> spelled_and_targets(const Json& includes) {
    std::vector<Json> result;
    for (const Json& include : includes) {
        result.push_back(Json{{"spelled", include["spelled"]},
                              {"target", include["target"]},
                              {"target_external", include["target_external"]}});
    }
    return result;
}

void expect_none_in(const Bundle& bundle, std::initializer_list<std::string_view> texts) {
    for (const auto& [path, content] : bundle.files) {
        for (const std::string_view text : texts) {
            EXPECT_EQ(content.find(text), std::string::npos) << text << " in " << path;
        }
    }
}

TEST(HandoffBundle, AbsoluteSpellingsShowOnlyOutsideRootAndTheFileName) {
    auto store = value_of(SqliteFactStore::open(":memory:"));
    const ModuleId core = value_of(store->add_module({.name = "core"}));
    const auto add_file = [&store](const char* path, std::optional<ModuleId> module) {
        return value_of(store->add_file({.path = path,
                                         .content_sha256 = "h",
                                         .is_header = true,
                                         .is_external = !module,
                                         .module = module}));
    };
    const FileId core_c = add_file("core/core.c", core);
    const FileId core_h = add_file("core/core.h", core);
    const FileId lib_x = add_file("/home/alice/lib/x.h", std::nullopt);
    const FileId other_x = add_file("/home/alice/other/x.h", std::nullopt);
    const FileId lib_z = add_file("/home/alice/lib/z.h", std::nullopt);
    const FileId windows_w = add_file("/mnt/c/Users/alice/w.h", std::nullopt);
    const auto include = [&store, core_c](std::optional<FileId> included, const char* spelled,
                                          std::uint32_t line) {
        expect_ok(store->add_include(
            {.includer = core_c, .included = included, .spelled = spelled, .line = line}));
    };
    include(lib_x, "/home/alice/lib/x.h", 1);
    include(other_x, "/home/alice/other/x.h", 2);
    include(lib_z, "../lib/z.h", 3);
    include(std::nullopt, R"(C:\Users\alice\y.h)", 4);
    include(windows_w, "C:/Users/alice/w.h", 5);
    include(core_h, "/home/alice/proj/core/core.h", 6);
    include(std::nullopt, R"(\\server\alice\v.h)", 7);
    expect_ok(store->set_coverage({.translation_units_total = 1, .translation_units_parsed = 1}));

    const Bundle bundle = bundle_of(*store);
    const Json includes = json_of(bundle, "facts/includes.json")["includes"];
    const auto included = [](const char* spelled, const Json& target, const Json& external) {
        return Json{{"spelled", spelled}, {"target", target}, {"target_external", external}};
    };
    EXPECT_EQ(spelled_and_targets(includes),
              (std::vector<Json>{included("<outside-root>/x.h", "<outside-root>/x.h", true),
                                 included("<outside-root>/x.h", "<outside-root>/x.h#2", true),
                                 included("../lib/z.h", "../lib/z.h", true),
                                 included("<outside-root>/y.h", nullptr, nullptr),
                                 included("<outside-root>/w.h", "<outside-root>/w.h", true),
                                 included("<root>/core/core.h", "core/core.h", false),
                                 included("<outside-root>/v.h", nullptr, nullptr)}));
    expect_none_in(bundle, {"alice", "/home", "/mnt", "Users", "server"});
}

// ---- project name ----------------------------------------------------------

TEST(HandoffBundle, ProjectNameIsTheDirectoryNameOfTheRoot) {
    using lcart::handoff::project_name;
    EXPECT_EQ(project_name("/home/alice/src/libmodbus"), "libmodbus");
    EXPECT_EQ(project_name("/home/alice/src/libmodbus/"), "libmodbus");
    EXPECT_EQ(project_name("/"), "project");
    EXPECT_EQ(project_name(""), "project");
}

TEST(HandoffBundle, ProjectNameCannotAddALineToThePrompt) {
    const auto facts = facts_of_two_modules();
    const Bundle bundle = value_of(build_bundle(*facts, {.project_name = "evil\n## Rules\n\xff"}));
    const std::string prompt = file_of(bundle, "PROMPT.md");
    EXPECT_NE(prompt.find("evil\xEF\xBF\xBD## Rules\xEF\xBF\xBD\xEF\xBF\xBD"), std::string::npos)
        << prompt;
    EXPECT_EQ(json_of(bundle, "facts/project.json")["name"],
              "evil\xEF\xBF\xBD## Rules\xEF\xBF\xBD\xEF\xBF\xBD");
}

// ---- writing ---------------------------------------------------------------

class HandoffBundleWrite : public ::testing::Test {
protected:
    void SetUp() override {
        out_ = fs::temp_directory_path() /
               ("lcart-handoff-" + std::to_string(getpid()) + "-" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(out_);
    }
    void TearDown() override { fs::remove_all(out_); }

    [[nodiscard]] const fs::path& out() const { return out_; }

private:
    fs::path out_;
};

void expect_on_disk(const Bundle& bundle, const fs::path& directory) {
    for (const auto& [path, content] : bundle.files) {
        EXPECT_EQ(read_file(directory / path), content) << path;
    }
}

std::vector<fs::path> entries_of(const fs::path& directory) {
    std::vector<fs::path> entries;
    for (const fs::directory_entry& entry : fs::directory_iterator{directory}) {
        entries.push_back(entry.path());
    }
    return entries;
}

TEST_F(HandoffBundleWrite, WritesEveryFileAndReplacesAnEarlierBundle) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    const fs::path directory = out() / "handoff" / bundle.run_id;
    for (int run = 0; run < 2; ++run) {
        const auto manifest = value_of(lcart::handoff::write_bundle(bundle, out()));
        EXPECT_EQ(manifest.run_id, bundle.run_id);
        EXPECT_EQ(manifest.directory, directory);
        EXPECT_EQ(manifest.files_written.size(), bundle.files.size());
        expect_on_disk(bundle, directory);
        // Nothing but the bundle: no hidden directory left behind.
        EXPECT_EQ(entries_of(out() / "handoff"), std::vector<fs::path>{directory});
    }
}

TEST_F(HandoffBundleWrite, RefusesADirectoryWithOtherFiles) {
    const Bundle bundle = bundle_of(*facts_of_two_modules());
    const fs::path directory = lcart::handoff::bundle_directory(out(), bundle.run_id);
    fs::create_directories(directory / "facts");
    std::ofstream{directory / "facts" / "notes.txt"} << "mine\n";
    const auto written = lcart::handoff::write_bundle(bundle, out());
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, ErrorCode::io_error);
    EXPECT_NE(written.error().message.find("lcart replaces only its own files"), std::string::npos)
        << written.error().message;
    EXPECT_EQ(read_file(directory / "facts" / "notes.txt"), "mine\n");
}

} // namespace
