// Verifies: fr006-query-and-export-facts-v1 (the JSON Lines export, format
// of ADR-0016) and nfr004-deterministic-output-v1 (no ids, timestamps or
// absolute paths in the export; a fixed order).
#include "facts/fact_export.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using lcart::facts::json_string;
using namespace lcart::facts;

template <typename T>
T value_of(lcart::common::Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

void expect_ok(const lcart::common::Status& status) {
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

TEST(JsonString, CopiesPlainText) {
    EXPECT_EQ(json_string(""), R"("")");
    EXPECT_EQ(json_string("core/core.h"), R"("core/core.h")");
}

TEST(JsonString, EscapesQuotesBackslashesAndControlCharacters) {
    EXPECT_EQ(json_string("a\"b\\c"), R"("a\"b\\c")");
    EXPECT_EQ(json_string("\n\r\t\b\f"), R"("\n\r\t\b\f")");
    EXPECT_EQ(json_string(std::string{"\x01\x1f", 2}), R"("\u0001\u001f")");
    EXPECT_EQ(json_string(std::string{"a\0b", 3}), R"("a\u0000b")");
    EXPECT_EQ(json_string("\x7f"), "\"\x7f\"");
}

TEST(JsonString, CopiesValidUtf8) {
    EXPECT_EQ(json_string("\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80"),
              "\"\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80\"");
}

TEST(JsonString, ReplacesEachInvalidByteWithReplacementCharacter) {
    const std::string replacement = "\xEF\xBF\xBD";
    EXPECT_EQ(json_string("a\x80z"), "\"a" + replacement + "z\""); // lone continuation
    EXPECT_EQ(json_string("\xC0\xAF"), "\"" + replacement + replacement + "\""); // overlong
    EXPECT_EQ(json_string("\xED\xA0\x80"),
              "\"" + replacement + replacement + replacement + "\""); // surrogate
    EXPECT_EQ(json_string("\xF4\x90\x80\x80"), "\"" + replacement + replacement + replacement +
                                                   replacement + "\"");          // above U+10FFFF
    EXPECT_EQ(json_string("\xE2\x82"), "\"" + replacement + replacement + "\""); // truncated
    EXPECT_EQ(json_string("\xFF"), "\"" + replacement + "\"");
}

/// Two modules, two externals spelled alike, an unresolved include, an
/// indirect call and diagnostics; added out of order on purpose.
std::unique_ptr<SqliteFactStore> sample_facts() {
    auto store = value_of(SqliteFactStore::open(":memory:"));
    const ModuleId src = value_of(store->add_module({.name = "src"}));
    const ModuleId lib = value_of(store->add_module({.name = "lib"}));
    const FileId b_c =
        value_of(store->add_file({.path = "src/b.c", .content_sha256 = "h-b", .module = src}));
    const FileId a_c =
        value_of(store->add_file({.path = "src/a.c", .content_sha256 = "h-a", .module = src}));
    static_cast<void>(value_of(store->add_file(
        {.path = "src/a.h", .content_sha256 = "h-ah", .is_header = true, .module = src})));
    const FileId x_c =
        value_of(store->add_file({.path = "lib/x.c", .content_sha256 = "h-x", .module = lib}));
    const auto external = [&store](const char* path) {
        return value_of(store->add_file({.path = path,
                                         .content_sha256 = "differs per machine",
                                         .is_header = true,
                                         .is_external = true}));
    };
    const FileId two_cfg = external("/opt/two/cfg.h");
    const FileId one_cfg = external("/opt/one/cfg.h");
    const FileId stdio = external("/usr/include/stdio.h");
    static_cast<void>(external("/usr/lib/unreferenced.h"));

    expect_ok(
        store->add_include({.includer = b_c, .included = two_cfg, .spelled = "cfg.h", .line = 1}));
    expect_ok(
        store->add_include({.includer = a_c, .included = one_cfg, .spelled = "cfg.h", .line = 2}));
    expect_ok(store->add_include(
        {.includer = a_c, .included = std::nullopt, .spelled = "gone.h", .line = 3}));
    expect_ok(store->add_include(
        {.includer = a_c, .included = stdio, .spelled = "stdio.h", .line = 1, .is_system = true}));

    const FunctionId g = value_of(store->add_function({.usr = "c:@F@g",
                                                       .name = "g",
                                                       .qualified_name = "g",
                                                       .signature = "void (void)",
                                                       .file = b_c,
                                                       .line_begin = 3,
                                                       .line_end = 9,
                                                       .is_definition = true}));
    const FunctionId f = value_of(store->add_function({.usr = "src/a.c#c:a.c@F@f",
                                                       .name = "f",
                                                       .qualified_name = "f",
                                                       .signature = "int (int)",
                                                       .file = a_c,
                                                       .line_begin = 4,
                                                       .line_end = 8,
                                                       .is_definition = true,
                                                       .linkage = Linkage::internal}));
    const FunctionId h = value_of(store->add_function({.usr = "c:@F@h",
                                                       .name = "h",
                                                       .qualified_name = "lib::h",
                                                       .signature = "void (void)",
                                                       .file = x_c,
                                                       .line_begin = 1,
                                                       .line_end = 2,
                                                       .is_definition = true}));
    expect_ok(store->add_call({.caller = g,
                               .callee = std::nullopt,
                               .kind = CallKind::indirect,
                               .via = "ops.run",
                               .site = {.file = b_c, .line = 6, .column = 3},
                               .field = "c:@S@ops@FI@run"}));
    expect_ok(
        store->add_call({.caller = g, .callee = f, .site = {.file = b_c, .line = 5, .column = 3}}));
    expect_ok(
        store->add_call({.caller = f, .callee = h, .site = {.file = a_c, .line = 5, .column = 3}}));
    // g's indirect call may call f and h; g -> h (src -> lib) is no module
    // dependency (B-26).
    expect_ok(store->add_field_assignment({.field = "c:@S@ops@FI@run", .function = h}));
    expect_ok(store->add_field_assignment({.field = "c:@S@ops@FI@run", .function = f}));
    expect_ok(store->derive_may_call_edges());
    expect_ok(store->derive_module_dependencies());

    const auto diagnostic = [&store](const char* unit, const char* file, std::uint32_t line,
                                     DiagnosticSeverity severity, const std::string& message) {
        expect_ok(store->add_diagnostic({.translation_unit = unit,
                                         .file = file,
                                         .line = line,
                                         .severity = severity,
                                         .message = message}));
    };
    diagnostic("src/b.c", "/opt/two/cfg.h", 7, DiagnosticSeverity::error, "e");
    diagnostic("src/b.c", "/somewhere/else.h", 2, DiagnosticSeverity::warning, "w");
    diagnostic("src/b.c", "", 0, DiagnosticSeverity::fatal, "no input");
    diagnostic("src/a.c", "src/a.h", 3, DiagnosticSeverity::error, "bad \"quote\"\n\t\xFF");
    expect_ok(store->set_coverage({.translation_units_total = 2, .translation_units_parsed = 1}));
    return store;
}

// Verifies: b26-function-pointer-tables-v1 (criterion 4: `may_call`
// records of their own after the calls, format_version 2, no module
// dependency from a may_call edge; ADR-0026).
TEST(FactExport, WritesEveryRecordKindInTheDocumentedOrder) {
    const auto store = sample_facts();
    std::ostringstream out;
    expect_ok(write_jsonl_export(*store, out));

    const std::string expected =
        R"json({"record":"header","format":"lcart-facts","format_version":2,"translation_units_total":2,"translation_units_parsed":1}
{"record":"module","name":"lib"}
{"record":"module","name":"src"}
{"record":"file","path":"lib/x.c","external":false,"header":false,"module":"lib","sha256":"h-x"}
{"record":"file","path":"src/a.c","external":false,"header":false,"module":"src","sha256":"h-a"}
{"record":"file","path":"src/a.h","external":false,"header":true,"module":"src","sha256":"h-ah"}
{"record":"file","path":"src/b.c","external":false,"header":false,"module":"src","sha256":"h-b"}
{"record":"file","path":"cfg.h","external":true,"header":true,"module":null,"sha256":null}
{"record":"file","path":"cfg.h#2","external":true,"header":true,"module":null,"sha256":null}
{"record":"file","path":"stdio.h","external":true,"header":true,"module":null,"sha256":null}
{"record":"file","path":"unreferenced.h","external":true,"header":true,"module":null,"sha256":null}
{"record":"function","usr":"c:@F@h","name":"h","qualified_name":"lib::h","signature":"void (void)","file":"lib/x.c","line_begin":1,"line_end":2,"definition":true,"linkage":"external"}
{"record":"function","usr":"src/a.c#c:a.c@F@f","name":"f","qualified_name":"f","signature":"int (int)","file":"src/a.c","line_begin":4,"line_end":8,"definition":true,"linkage":"internal"}
{"record":"function","usr":"c:@F@g","name":"g","qualified_name":"g","signature":"void (void)","file":"src/b.c","line_begin":3,"line_end":9,"definition":true,"linkage":"external"}
{"record":"include","file":"src/a.c","line":1,"spelled":"stdio.h","system":true,"target":"stdio.h","target_external":true}
{"record":"include","file":"src/a.c","line":2,"spelled":"cfg.h","system":false,"target":"cfg.h","target_external":true}
{"record":"include","file":"src/a.c","line":3,"spelled":"gone.h","system":false,"target":null,"target_external":null}
{"record":"include","file":"src/b.c","line":1,"spelled":"cfg.h","system":false,"target":"cfg.h#2","target_external":true}
{"record":"call","caller":"src/a.c#c:a.c@F@f","file":"src/a.c","line":5,"column":3,"kind":"direct","callee":"c:@F@h","via":""}
{"record":"call","caller":"c:@F@g","file":"src/b.c","line":5,"column":3,"kind":"direct","callee":"src/a.c#c:a.c@F@f","via":""}
{"record":"call","caller":"c:@F@g","file":"src/b.c","line":6,"column":3,"kind":"indirect","callee":null,"via":"ops.run"}
{"record":"may_call","caller":"c:@F@g","file":"src/b.c","line":6,"column":3,"callee":"c:@F@h","via":"ops.run"}
{"record":"may_call","caller":"c:@F@g","file":"src/b.c","line":6,"column":3,"callee":"src/a.c#c:a.c@F@f","via":"ops.run"}
{"record":"module_dependency","from":"src","to":"lib","via":"call","file":"src/a.c","line":5,"column":3}
{"record":"diagnostic","translation_unit":"src/a.c","file":"src/a.h","line":3,"severity":"error","message":"bad \"quote\"\n\t)json"
        "\xEF\xBF\xBD"
        R"json("}
{"record":"diagnostic","translation_unit":"src/b.c","file":null,"line":0,"severity":"fatal","message":"no input"}
{"record":"diagnostic","translation_unit":"src/b.c","file":null,"line":2,"severity":"warning","message":"w"}
{"record":"diagnostic","translation_unit":"src/b.c","file":"cfg.h#2","line":7,"severity":"error","message":"e"}
)json";
    EXPECT_EQ(out.str(), expected);
}

/// One directive `src/a.c:1 "cfg.h"` that reached two different external
/// files (translation units with different -I), recorded in either order;
/// `src/b.c:1` reaches only the second.
std::string export_of_same_directive_two_targets(bool first_one_first) {
    auto store = value_of(SqliteFactStore::open(":memory:"));
    const ModuleId src = value_of(store->add_module({.name = "src"}));
    const FileId a_c = value_of(store->add_file({.path = "src/a.c", .module = src}));
    const auto external = [&store](const char* path, const char* sha256) {
        return value_of(store->add_file(
            {.path = path, .content_sha256 = sha256, .is_header = true, .is_external = true}));
    };
    std::vector<FileId> targets;
    if (first_one_first) {
        targets = {external("/opt/one/cfg.h", "aaaa"), external("/opt/two/cfg.h", "bbbb")};
    } else {
        targets = {external("/opt/two/cfg.h", "bbbb"), external("/opt/one/cfg.h", "aaaa")};
    }
    for (const FileId target : targets) {
        expect_ok(store->add_include(
            {.includer = a_c, .included = target, .spelled = "cfg.h", .line = 1}));
    }
    // Only /opt/two/cfg.h is also reached from b.c, so which file got which
    // name shows in the export.
    const FileId b_c = value_of(store->add_file({.path = "src/b.c", .module = src}));
    const FileId two = first_one_first ? targets[1] : targets[0];
    expect_ok(
        store->add_include({.includer = b_c, .included = two, .spelled = "cfg.h", .line = 1}));
    std::ostringstream out;
    expect_ok(write_jsonl_export(*store, out));
    return out.str();
}

// ADR-0016: external names must not depend on insertion order, which B-16's
// parallel analysis may change.
TEST(FactExport, ExternalNamesDoNotDependOnInsertionOrder) {
    const std::string one_first = export_of_same_directive_two_targets(true);
    EXPECT_EQ(one_first, export_of_same_directive_two_targets(false));
    EXPECT_NE(one_first.find(R"("path":"cfg.h#2")"), std::string::npos) << one_first;
}

TEST(FactExport, EmptyDatabaseHasOnlyTheHeader) {
    const auto store = value_of(SqliteFactStore::open(":memory:"));
    std::ostringstream out;
    expect_ok(write_jsonl_export(*store, out));
    EXPECT_EQ(
        out.str(),
        R"json({"record":"header","format":"lcart-facts","format_version":2,"translation_units_total":0,"translation_units_parsed":0})json"
        "\n");
}

TEST(FactExport, FailedStreamIsAnError) {
    const auto store = sample_facts();
    std::ostringstream good;
    ASSERT_TRUE(write_jsonl_export(*store, good).has_value()); // control
    std::ostringstream out;
    out.setstate(std::ios::badbit);
    EXPECT_FALSE(write_jsonl_export(*store, out).has_value());
}

} // namespace
