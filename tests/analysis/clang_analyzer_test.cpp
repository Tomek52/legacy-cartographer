// Verifies: fr001-ingest-compilation-database-v1 (criterion 2: a failed TU
// is a diagnostic and analysis continues; criterion 1 on two_modules)
// and docs/security.md T5 (plugin arguments, also inside response files,
// never reach Clang). File and include facts (FR-002) are verified on the
// two_modules fixture further down.
#include "analysis/clang_analyzer.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::ClangAnalyzer;
using lcart::common::Status;
using namespace lcart::facts;

fs::path fixture_dir() {
    return fs::path{LCART_FIXTURES_DIR} / "broken_tu";
}

fs::path two_modules_dir() {
    return fs::path{LCART_FIXTURES_DIR} / "two_modules";
}

fs::path unresolved_include_dir() {
    return fs::path{LCART_FIXTURES_DIR} / "unresolved_include";
}

/// Records what the analyzer writes; ids are assigned in call order. Like
/// the real store, a known module name or file path returns its id.
class RecordingFactWriter final : public FactWriter {
public:
    lcart::common::Result<ModuleId> add_module(const ModuleFact& module) override {
        const auto [it, inserted] = module_ids_.try_emplace(module.name, ModuleId{next_id_ + 1});
        if (inserted) {
            ++next_id_;
        }
        return it->second;
    }
    lcart::common::Result<FileId> add_file(const FileFact& file) override {
        for (const FileFact& known : files_) {
            if (known.path == file.path) {
                return known.id;
            }
        }
        FileFact stored = file;
        stored.id = FileId{++next_id_};
        files_.push_back(stored);
        return stored.id;
    }
    Status add_include(const IncludeFact& include) override {
        includes_.push_back(include);
        return {};
    }
    lcart::common::Result<FunctionId> add_function(const FunctionFact& /*function*/) override {
        return FunctionId{++next_id_};
    }
    Status add_call(const CallFact& /*call*/) override { return {}; }
    Status add_diagnostic(const DiagnosticFact& diagnostic) override {
        diagnostics_.push_back(diagnostic);
        return {};
    }
    Status set_coverage(const AnalysisCoverage& value) override {
        coverage_ = value;
        return {};
    }
    Status derive_module_dependencies() override { return {}; }

    [[nodiscard]] const std::vector<DiagnosticFact>& diagnostics() const { return diagnostics_; }
    [[nodiscard]] const AnalysisCoverage& coverage() const { return coverage_; }
    [[nodiscard]] const std::vector<FileFact>& files() const { return files_; }
    [[nodiscard]] const std::vector<IncludeFact>& includes() const { return includes_; }

    [[nodiscard]] const FileFact* file(FileId id) const {
        const auto it = std::ranges::find(files_, id, &FileFact::id);
        return it == files_.end() ? nullptr : &*it;
    }
    [[nodiscard]] const FileFact* file(const std::string& path) const {
        const auto it = std::ranges::find(files_, path, &FileFact::path);
        return it == files_.end() ? nullptr : &*it;
    }
    [[nodiscard]] std::string module_name(ModuleId id) const {
        for (const auto& [name, module_id] : module_ids_) {
            if (module_id == id) {
                return name;
            }
        }
        return {};
    }

private:
    std::int64_t next_id_{0};
    std::map<std::string, ModuleId> module_ids_;
    std::vector<FileFact> files_;
    std::vector<IncludeFact> includes_;
    std::vector<DiagnosticFact> diagnostics_;
    AnalysisCoverage coverage_;
};

class ClangAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-analyzer-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    [[nodiscard]] const fs::path& work_dir() const { return work_dir_; }

    /// Writes compile_commands.json with one entry per file, each compiled
    /// with `extra` arguments added.
    [[nodiscard]] fs::path write_compilation_database(const std::vector<std::string>& files,
                                                      const std::string& extra = "") const {
        const fs::path path = work_dir_ / "compile_commands.json";
        std::ofstream out{path};
        out << "[\n";
        for (std::size_t i = 0; i < files.size(); ++i) {
            out << R"(  {"directory": ")" << fixture_dir().string() << R"(", "command": "clang -c )"
                << extra << " -Iinclude " << files[i] << R"(", "file": ")" << files[i] << "\"}"
                << (i + 1 < files.size() ? ",\n" : "\n");
        }
        out << "]\n";
        return path;
    }

    /// compile_commands.json for `fixture`: every TU compiled with the
    /// fixture root on the include path.
    [[nodiscard]] fs::path write_fixture_database(const fs::path& fixture,
                                                  const std::vector<std::string>& files) const {
        const fs::path path = work_dir_ / "compile_commands.json";
        std::ofstream out{path};
        out << "[\n";
        for (std::size_t i = 0; i < files.size(); ++i) {
            out << R"(  {"directory": ")" << fixture.string() << R"(", "command": "clang -c -I. )"
                << files[i] << R"(", "file": ")" << files[i] << "\"}"
                << (i + 1 < files.size() ? ",\n" : "\n");
        }
        out << "]\n";
        return path;
    }

    [[nodiscard]] lcart::common::Result<lcart::analysis::AnalysisSummary>
    analyze_two_modules(FactWriter& facts) const {
        ClangAnalyzer analyzer;
        return analyzer.analyze(
            {.compilation_database = write_fixture_database(
                 two_modules_dir(), {"app/legacy.c", "app/main.c", "core/core.c"}),
             .root = two_modules_dir(),
             .exclude_globs = {},
             .module_rules = {}},
            facts);
    }

private:
    fs::path work_dir_;
};

TEST_F(ClangAnalyzerTest, CountsParsedAndFailedTranslationUnits) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(
        {.compilation_database = write_compilation_database({"ok.c", "broken.c", "also_ok.c"}),
         .root = fixture_dir(),
         .exclude_globs = {},
         .module_rules = {}},
        facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_total, 3U);
    EXPECT_EQ(summary->translation_units_parsed, 2U);
    EXPECT_EQ(facts.coverage().translation_units_total, 3U);
    EXPECT_EQ(facts.coverage().translation_units_parsed, 2U);
}

TEST_F(ClangAnalyzerTest, RecordsSyntaxErrorAsDiagnosticWithLocation) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary =
        analyzer.analyze({.compilation_database = write_compilation_database({"broken.c", "ok.c"}),
                          .root = fixture_dir(),
                          .exclude_globs = {},
                          .module_rules = {}},
                         facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    ASSERT_EQ(facts.diagnostics().size(), 1U);
    const DiagnosticFact& diagnostic = facts.diagnostics().front();
    // Root-relative, like file facts, so exports do not depend on where the
    // code is checked out (ADR-0016).
    EXPECT_EQ(diagnostic.translation_unit, "broken.c");
    EXPECT_EQ(diagnostic.file, "broken.c");
    EXPECT_EQ(diagnostic.line, 4U);
    EXPECT_EQ(diagnostic.severity, DiagnosticSeverity::error);
    EXPECT_FALSE(diagnostic.message.empty());
    EXPECT_EQ(summary->diagnostics, 1U);
}

TEST_F(ClangAnalyzerTest, StripsPluginArgumentsAndWarns) {
    // LibTooling would not load these plugins (it never calls
    // LoadRequestedPlugins(), ADR-0021); the filter removes them anyway, as
    // defence in depth, and warns for each.
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(
        {.compilation_database = write_compilation_database(
             {"ok.c"}, "-fplugin=/nonexistent/evil.so -Xclang -load -Xclang /nonexistent/evil.so"),
         .root = fixture_dir(),
         .exclude_globs = {},
         .module_rules = {}},
        facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_parsed, 1U);
    ASSERT_EQ(summary->warnings.size(), 2U);
    EXPECT_NE(summary->warnings[0].find("-fplugin=/nonexistent/evil.so"), std::string::npos);
    EXPECT_NE(summary->warnings[1].find("-load"), std::string::npos);
}

TEST_F(ClangAnalyzerTest, StripsPluginArgumentsInsideResponseFiles) {
    {
        std::ofstream rsp{work_dir() / "flags.rsp"};
        rsp << "-fplugin=/nonexistent/evil.so\n";
    }
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = write_compilation_database(
                                               {"ok.c"}, "@" + (work_dir() / "flags.rsp").string()),
                                           .root = fixture_dir(),
                                           .exclude_globs = {},
                                           .module_rules = {}},
                                          facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_parsed, 1U);
    ASSERT_EQ(summary->warnings.size(), 1U);
    EXPECT_NE(summary->warnings[0].find("-fplugin=/nonexistent/evil.so"), std::string::npos);
}

// Verifies: security-malicious-build-input-v1 (T5: a Clang configuration
// file named in the command does not influence the analysis; its arguments
// are removed and the removal is reported). ADR-0023.
TEST_F(ClangAnalyzerTest, IgnoresExplicitConfigFileAndWarns) {
    // A translation unit that only compiles when the config defines
    // FROM_CONFIG; the config would define it, so if the config were read
    // the TU would parse. The filter removes --config, so the #error fires.
    {
        std::ofstream src{work_dir() / "needs_config.c"};
        src << "#ifndef FROM_CONFIG\n#error configuration file was read\n#endif\nint ok;\n";
    }
    const fs::path config = work_dir() / "evil.cfg";
    {
        std::ofstream cfg{config};
        cfg << "-DFROM_CONFIG=1\n";
    }
    const fs::path database = work_dir() / "compile_commands.json";
    {
        std::ofstream out{database};
        out << R"([{"directory": ")" << work_dir().string()
            << R"(", "command": "clang -c --config=)" << config.string()
            << R"( needs_config.c", "file": "needs_config.c"}])" << '\n';
    }
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = database,
                                           .root = work_dir(),
                                           .exclude_globs = {},
                                           .module_rules = {}},
                                          facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    // The config was ignored, so FROM_CONFIG is undefined and the TU fails.
    EXPECT_EQ(summary->translation_units_parsed, 0U);
    EXPECT_GT(summary->diagnostics, 0U);
    ASSERT_EQ(summary->warnings.size(), 1U);
    EXPECT_NE(summary->warnings[0].find("--config=" + config.string()), std::string::npos);
    EXPECT_NE(summary->warnings[0].find("reads a configuration file"), std::string::npos);
}

// Verifies: security-malicious-build-input-v1 (T5: a default clang.cfg next
// to the program named by the command does not influence the analysis;
// --no-default-config, inserted by the recorder, stops it). ADR-0023.
TEST_F(ClangAnalyzerTest, IgnoresDefaultConfigFileNextToTheProgram) {
    {
        std::ofstream src{work_dir() / "needs_config.c"};
        src << "#ifndef FROM_CONFIG\n#error configuration file was read\n#endif\nint ok;\n";
    }
    // The program named by the command: any path whose DIRECTORY holds
    // clang.cfg. ClangTool reads the default config from that directory and
    // does not require the program to exist as an executable (verified on
    // clang-20 20.1.2 with LibTooling). The basename stays "clang" so the
    // driver mode is inferred the usual way.
    const fs::path toolbin = work_dir() / "toolbin";
    fs::create_directories(toolbin);
    {
        std::ofstream cfg{toolbin / "clang.cfg"};
        cfg << "-DFROM_CONFIG=1\n";
    }
    const std::string program = (toolbin / "clang").string();
    const fs::path database = work_dir() / "compile_commands.json";
    {
        std::ofstream out{database};
        out << R"([{"directory": ")" << work_dir().string() << R"(", "command": ")" << program
            << R"( -c needs_config.c", "file": "needs_config.c"}])" << '\n';
    }
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = database,
                                           .root = work_dir(),
                                           .exclude_globs = {},
                                           .module_rules = {}},
                                          facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    // The default config was ignored, so the TU fails and nothing is removed
    // (no explicit --config in the command).
    EXPECT_EQ(summary->translation_units_parsed, 0U);
    EXPECT_GT(summary->diagnostics, 0U);
    EXPECT_TRUE(summary->warnings.empty());
}

TEST_F(ClangAnalyzerTest, MissingCompilationDatabaseIsAnError) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = work_dir() / "missing.json",
                                           .root = fixture_dir(),
                                           .exclude_globs = {},
                                           .module_rules = {}},
                                          facts);

    ASSERT_FALSE(summary.has_value());
    EXPECT_EQ(summary.error().code, lcart::common::ErrorCode::io_error);
}

// Verifies: fr001-ingest-compilation-database-v1 (criterion 1)
TEST_F(ClangAnalyzerTest, ParsesEveryTranslationUnitOfTwoModules) {
    RecordingFactWriter facts;
    const auto summary = analyze_two_modules(facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(facts.coverage(),
              (AnalysisCoverage{.translation_units_total = 3, .translation_units_parsed = 3}));
    EXPECT_TRUE(facts.diagnostics().empty());
}

TEST_F(ClangAnalyzerTest, EmptyOrMissingRootIsAnError) {
    for (const fs::path& root : {fs::path{}, work_dir() / "no-such-root"}) {
        RecordingFactWriter facts;
        ClangAnalyzer analyzer;
        const auto summary =
            analyzer.analyze({.compilation_database = write_compilation_database({"ok.c"}),
                              .root = root,
                              .exclude_globs = {},
                              .module_rules = {}},
                             facts);

        ASSERT_FALSE(summary.has_value()) << root;
        EXPECT_EQ(summary.error().code, lcart::common::ErrorCode::invalid_argument) << root;
    }
}

// --- File and include facts (FR-002) on the two_modules fixture ---

/// How an include target is shown in the expected lists: the root-relative
/// path for a file under the root, "<external>" plus the file name for a file
/// outside it, empty for an unresolved include.
[[nodiscard]] std::string target_name(const RecordingFactWriter& facts,
                                      std::optional<FileId> included) {
    if (!included) {
        return {};
    }
    const FileFact* file = facts.file(*included);
    if (file == nullptr) {
        return "<unknown id>";
    }
    if (file->is_external) {
        return "<external>" + fs::path{file->path}.filename().string();
    }
    return file->path;
}

[[nodiscard]] std::string path_of(const RecordingFactWriter& facts, FileId id) {
    const FileFact* file = facts.file(id);
    return file == nullptr ? "<unknown id>" : file->path;
}

using IncludePair = std::pair<std::string, std::string>;

[[nodiscard]] std::set<IncludePair> include_pairs(const RecordingFactWriter& facts) {
    std::set<IncludePair> pairs;
    for (const IncludeFact& include : facts.includes()) {
        pairs.emplace(path_of(facts, include.includer), target_name(facts, include.included));
    }
    return pairs;
}

/// An include directive with paths instead of ids.
struct Directive {
    std::string includer;
    std::string target;
    std::string spelled;
    std::uint32_t line{};
    bool is_system{};
    bool operator==(const Directive&) const = default;
};

[[nodiscard]] std::vector<Directive> directives_in(const RecordingFactWriter& facts,
                                                   const std::string& includer) {
    std::vector<Directive> directives;
    for (const IncludeFact& include : facts.includes()) {
        if (path_of(facts, include.includer) == includer) {
            directives.push_back({.includer = includer,
                                  .target = target_name(facts, include.included),
                                  .spelled = include.spelled,
                                  .line = include.line,
                                  .is_system = include.is_system});
        }
    }
    return directives;
}

std::ostream& operator<<(std::ostream& out, const Directive& directive) {
    return out << directive.includer << ":" << directive.line << " -> '" << directive.spelled
               << "' (" << directive.target << (directive.is_system ? ", system)" : ", user)");
}

// Verifies: fr002-record-files-and-includes-v1 (criterion 1)
TEST_F(ClangAnalyzerTest, IncludePairsOfTwoModulesMatchTheExpectedList) {
    RecordingFactWriter facts;
    const auto summary = analyze_two_modules(facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    const std::set<IncludePair> expected{
        {"app/app.h", "core/core.h"},          {"app/legacy.c", "app/app.h"},
        {"app/main.c", "app/app.h"},           {"app/main.c", "<external>stdio.h"},
        {"core/core.c", "core/core.h"},        {"core/core.c", "<external>string.h"},
        {"core/core.h", "<external>stddef.h"},
    };
    EXPECT_EQ(include_pairs(facts), expected);
}

// Verifies: fr002-record-files-and-includes-v1 (criterion 1: each directive
// once, although headers are reached from several translation units)
TEST_F(ClangAnalyzerTest, RecordsEachIncludeDirectiveOnce) {
    RecordingFactWriter facts;
    const auto summary = analyze_two_modules(facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(facts.includes().size(), 7U);
}

// Verifies: fr002-record-files-and-includes-v1 (criterion 2)
TEST_F(ClangAnalyzerTest, RecordsUnresolvedIncludeWithSpelling) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(
        {.compilation_database = write_fixture_database(unresolved_include_dir(), {"main.c"}),
         .root = unresolved_include_dir(),
         .exclude_globs = {},
         .module_rules = {}},
        facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_parsed, 0U);
    EXPECT_EQ(directives_in(facts, "main.c"),
              (std::vector<Directive>{{.includer = "main.c",
                                       .target = "<external>stddef.h",
                                       .spelled = "stddef.h",
                                       .line = 1,
                                       .is_system = true},
                                      {.includer = "main.c",
                                       .target = "",
                                       .spelled = "generated/config.h",
                                       .line = 7,
                                       .is_system = false}}));
}

// Verifies: fr002-record-files-and-includes-v1 (description: line, spelling,
// system or user include)
TEST_F(ClangAnalyzerTest, RecordsSpellingLineAndKindOfInclude) {
    RecordingFactWriter facts;
    const auto summary = analyze_two_modules(facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(directives_in(facts, "app/main.c"),
              (std::vector<Directive>{{.includer = "app/main.c",
                                       .target = "app/app.h",
                                       .spelled = "app.h",
                                       .line = 1,
                                       .is_system = false},
                                      {.includer = "app/main.c",
                                       .target = "<external>stdio.h",
                                       .spelled = "stdio.h",
                                       .line = 3,
                                       .is_system = true}}));
}

/// A file fact with the module's name instead of its id.
struct FileSummary {
    std::string sha256;
    bool is_header{};
    std::string module;
    bool operator==(const FileSummary&) const = default;
};

std::ostream& operator<<(std::ostream& out, const FileSummary& file) {
    return out << "{" << file.sha256 << (file.is_header ? ", header, " : ", source, ")
               << file.module << "}";
}

// Verifies: fr002-record-files-and-includes-v1 (description: path relative to
// the root, SHA-256 of the content, header or source; FR-005 default module)
TEST_F(ClangAnalyzerTest, RecordsFilesUnderTheRootWithHashKindAndModule) {
    RecordingFactWriter facts;
    const auto summary = analyze_two_modules(facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    std::map<std::string, FileSummary> internal;
    for (const FileFact& file : facts.files()) {
        if (!file.is_external) {
            internal[file.path] = {.sha256 = file.content_sha256,
                                   .is_header = file.is_header,
                                   .module = facts.module_name(file.module.value_or(ModuleId{}))};
        }
    }
    // Hashes from sha256sum over the fixture files.
    const std::map<std::string, FileSummary> expected{
        {"app/app.h",
         {.sha256 = "e3cf35f02e930d661de4201b25445434ae644789e55bf037d2ced14b4ed04fe1",
          .is_header = true,
          .module = "app"}},
        {"app/legacy.c",
         {.sha256 = "7e5a5fdcb319aca6012c6f7798b440b7caaed354b436135a192ebbaba8cfec88",
          .is_header = false,
          .module = "app"}},
        {"app/main.c",
         {.sha256 = "61bd5865431f07d98ce8d8dbd3d4f559962361b27e548865501e5cd8e9282465",
          .is_header = false,
          .module = "app"}},
        {"core/core.c",
         {.sha256 = "2ab898c7ec59e3a8d0185c88c23e702cccb35086431b2aa93c6538aca4529049",
          .is_header = false,
          .module = "core"}},
        {"core/core.h",
         {.sha256 = "4669d955a5dcd8795387a6677f2cab893513fe100b74c49a9828304efe3a028a",
          .is_header = true,
          .module = "core"}},
    };
    EXPECT_EQ(internal, expected);
}

// Verifies: fr002-record-files-and-includes-v1 (every source file reached,
// also one without any #include)
TEST_F(ClangAnalyzerTest, RecordsMainFileWithoutIncludes) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary =
        analyzer.analyze({.compilation_database = write_compilation_database({"also_ok.c"}),
                          .root = fixture_dir(),
                          .exclude_globs = {},
                          .module_rules = {}},
                         facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    ASSERT_EQ(facts.files().size(), 1U);
    EXPECT_EQ(facts.files().front().path, "also_ok.c");
    EXPECT_FALSE(facts.files().front().is_header);
    EXPECT_EQ(facts.module_name(facts.files().front().module.value_or(ModuleId{})), "(root)");
}

// Verifies: fr002-record-files-and-includes-v1 (every header reached, also
// one forced in with -include, which no #include directive names)
TEST_F(ClangAnalyzerTest, RecordsHeaderForcedWithIncludeOption) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = write_compilation_database(
                                               {"also_ok.c"}, "-include include/forced.h"),
                                           .root = fixture_dir(),
                                           .exclude_globs = {},
                                           .module_rules = {}},
                                          facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_TRUE(facts.includes().empty());
    std::set<std::string> internal_paths;
    for (const FileFact& file : facts.files()) {
        if (!file.is_external) {
            internal_paths.insert(file.path);
        }
    }
    EXPECT_EQ(internal_paths, (std::set<std::string>{"also_ok.c", "include/forced.h"}));
}

/// What criterion 3 requires of an external file.
struct ExternalSummary {
    std::string name;
    bool has_module{};
    bool is_absolute{};
    bool is_include_target{};
    bool is_includer{};
    bool operator==(const ExternalSummary&) const = default;
};

std::ostream& operator<<(std::ostream& out, const ExternalSummary& file) {
    return out << "{" << file.name << ", module " << file.has_module << ", absolute "
               << file.is_absolute << ", target " << file.is_include_target << ", includer "
               << file.is_includer << "}";
}

// Verifies: fr002-record-files-and-includes-v1 (criterion 3)
TEST_F(ClangAnalyzerTest, ExternalFilesAreOnlyIncludeTargetsWithoutModule) {
    RecordingFactWriter facts;
    const auto summary = analyze_two_modules(facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    std::vector<ExternalSummary> external;
    for (const FileFact& file : facts.files()) {
        if (!file.is_external) {
            continue;
        }
        external.push_back(
            {.name = fs::path{file.path}.filename().string(),
             .has_module = file.module.has_value(),
             .is_absolute = fs::path{file.path}.is_absolute(),
             .is_include_target = std::ranges::any_of(
                 facts.includes(),
                 [&](const IncludeFact& include) { return include.included == file.id; }),
             .is_includer = std::ranges::any_of(facts.includes(), [&](const IncludeFact& include) {
                 return include.includer == file.id;
             })});
    }
    std::ranges::sort(external, {}, &ExternalSummary::name);
    const auto expected_for = [](std::string name) {
        return ExternalSummary{.name = std::move(name),
                               .has_module = false,
                               .is_absolute = true,
                               .is_include_target = true,
                               .is_includer = false};
    };
    EXPECT_EQ(external,
              (std::vector<ExternalSummary>{expected_for("stddef.h"), expected_for("stdio.h"),
                                            expected_for("string.h")}));
}

// Verifies: fr002-record-files-and-includes-v1 (the facts satisfy the
// schema's constraints in the SQLite store)
TEST_F(ClangAnalyzerTest, WritesFileAndIncludeFactsToTheStore) {
    auto store = SqliteFactStore::open(":memory:");
    ASSERT_TRUE(store.has_value()) << store.error().message;
    const auto summary = analyze_two_modules(**store);
    ASSERT_TRUE(summary.has_value()) << summary.error().message;

    const std::optional<ModuleFact> core = (*store)->find_module("core").value_or(std::nullopt);
    const std::optional<FileFact> core_h =
        (*store)->find_file("core/core.h").value_or(std::nullopt);
    ASSERT_NE(core, std::nullopt);
    EXPECT_EQ(core_h.and_then([](const FileFact& file) { return file.module; }),
              core.transform([](const ModuleFact& module) { return module.id; }));

    const FileId main_c = (*store)
                              ->find_file("app/main.c")
                              .value_or(std::nullopt)
                              .transform([](const FileFact& file) { return file.id; })
                              .value_or(FileId{});
    std::vector<std::string> spellings;
    for (const IncludeFact& include :
         (*store)->includes_of(main_c).value_or(std::vector<IncludeFact>{})) {
        spellings.push_back(include.spelled);
    }
    EXPECT_EQ(spellings, (std::vector<std::string>{"app.h", "stdio.h"}));
}

} // namespace
