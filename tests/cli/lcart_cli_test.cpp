// End-to-end tests of the `lcart` executable on the fixtures.
// Verifies: fr006-query-and-export-facts-v1 (criteria 1-3: callers sorted by
// file and line, ambiguous names, golden export; and callees, includes,
// included-by, module-deps), b12-query-and-export-v2 (criterion 1: the T2
// warning; criterion 2: two analyses into fresh databases export the same
// bytes), nfr004-deterministic-output-v1 (criteria 1 and 3: golden export
// without timestamps) and b11-module-rules-v2 (criterion 1: `lcart analyze`
// reads the rules and exclusions from lcart.toml), fr007-module-map-without-llm-v1
// (criteria 2 and 3: `lcart map` from the fact database alone, the same
// bytes twice) and b13-module-map-v1 (golden map of two_modules; the command
// line, exit statuses and T2 warning of ADR-0017).
#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// What a run of `lcart` printed and returned.
struct Outcome {
    int status{-1};
    std::string out;
    std::string err;
};

std::string read_file(const fs::path& path) {
    // Not istreambuf_iterator: GCC 14 at -O3 reports -Wnull-dereference in it.
    const std::ifstream in{path, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return std::move(text).str();
}

fs::path fixture(std::string_view name) {
    return fs::path{LCART_FIXTURES_DIR} / name;
}

/// Exit statuses of ADR-0016.
constexpr int exit_success = 0;
constexpr int exit_failure = 1;
constexpr int exit_usage = 2;
constexpr int exit_not_found = 3;
constexpr int exit_ambiguous = 4;

class LcartCliTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-cli-" + std::to_string(getpid()) + "-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
        work_dir_ = fs::canonical(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    [[nodiscard]] const fs::path& work_dir() const { return work_dir_; }

    /// Runs `lcart arguments...` in `directory` (default: the work directory)
    /// without a shell.
    /// Standard output goes to `stdout_path` if given (then `out` stays
    /// empty).
    [[nodiscard]] Outcome lcart(std::vector<std::string> arguments, const fs::path& directory = {},
                                const fs::path& stdout_path = {}) const {
        const fs::path out_file = stdout_path.empty() ? work_dir_ / "stdout.txt" : stdout_path;
        const fs::path err_file = work_dir_ / "stderr.txt";
        const std::string run_in = (directory.empty() ? work_dir_ : directory).string();
        std::string program{LCART_EXECUTABLE};
        std::vector<char*> argv{program.data()};
        for (std::string& argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions{};
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addchdir_np(&actions, run_in.c_str());
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out_file.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0600);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, err_file.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0600);
        pid_t pid = 0;
        const int spawned =
            posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        Outcome run;
        if (spawned != 0) {
            ADD_FAILURE() << "cannot start " << program << ": errno " << spawned;
            return run;
        }
        int wait_status = 0;
        while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
        }
        run.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
        run.out = stdout_path.empty() ? read_file(out_file) : std::string{};
        run.err = read_file(err_file);
        return run;
    }

    /// Writes compile_commands.json for `units` of `root`, compiled with
    /// `-I. <extra>`, and returns its path.
    [[nodiscard]] fs::path write_compilation_database(const fs::path& root,
                                                      const std::vector<std::string>& units,
                                                      const std::string& extra = "") const {
        const fs::path path = work_dir_ / "compile_commands.json";
        std::ofstream out{path};
        out << "[\n";
        for (std::size_t i = 0; i < units.size(); ++i) {
            out << R"(  {"directory": ")" << root.string() << R"(", "command": "clang -c -I. )"
                << extra << ' ' << units[i] << R"(", "file": ")" << units[i] << "\"}"
                << (i + 1 < units.size() ? ",\n" : "\n");
        }
        out << "]\n";
        return path;
    }

    /// Analyses `units` of fixture `name` into `database` and expects success.
    void analyze(std::string_view name, const std::vector<std::string>& units,
                 const fs::path& database, const std::string& extra = "") const {
        const fs::path root = fixture(name);
        const Outcome run = lcart({"analyze", "--root", root.string(), "--compilation-database",
                                   write_compilation_database(root, units, extra).string(), "--db",
                                   database.string()});
        ASSERT_EQ(run.status, exit_success) << run.err;
    }

    [[nodiscard]] fs::path two_modules_database() const {
        const fs::path database = work_dir_ / "two_modules.lcart.db";
        analyze("two_modules", {"app/legacy.c", "app/main.c", "core/core.c"}, database);
        return database;
    }

    /// Analyses `units` of fixture `name` into a new database named after
    /// `run` and returns its export.
    [[nodiscard]] std::string export_of_fresh_analysis(std::string_view name,
                                                       const std::vector<std::string>& units,
                                                       const std::string& extra,
                                                       std::string_view run) const {
        const fs::path database =
            work_dir_ / (std::string{name} + "-" + std::string{run} + ".lcart.db");
        analyze(name, units, database, extra);
        const Outcome exported = query(database, {"export"});
        EXPECT_EQ(exported.status, exit_success) << exported.err;
        return exported.out;
    }

    /// `lcart query <arguments> --db <database>`.
    [[nodiscard]] Outcome query(const fs::path& database,
                                std::vector<std::string> arguments) const {
        arguments.insert(arguments.begin(), "query");
        arguments.emplace_back("--db");
        arguments.push_back(database.string());
        return lcart(std::move(arguments));
    }

    /// Runs `lcart arguments...` and expects it to refuse with exit 1, an
    /// error naming `reason`, and `file` still holding `content`.
    void expect_refused(const std::vector<std::string>& arguments, std::string_view reason,
                        const fs::path& file, const std::string& content) const {
        const Outcome run = lcart(arguments);
        EXPECT_EQ(run.status, exit_failure) << arguments.back();
        EXPECT_NE(run.err.find(reason), std::string::npos) << run.err;
        EXPECT_EQ(read_file(file), content) << arguments.back();
    }

private:
    fs::path work_dir_;
};

TEST_F(LcartCliTest, HelpAndVersion) {
    const Outcome help = lcart({"--help"});
    EXPECT_EQ(help.status, exit_success);
    EXPECT_NE(help.out.find("lcart query"), std::string::npos) << help.out;
    EXPECT_NE(help.out.find("lcart map"), std::string::npos) << help.out;
    const Outcome version = lcart({"--version"});
    EXPECT_EQ(version.status, exit_success);
    EXPECT_EQ(version.out.rfind("lcart ", 0), 0U) << version.out;
}

TEST_F(LcartCliTest, BadCommandLineIsAUsageError) {
    const Outcome run = lcart({"query", "everything"});
    EXPECT_EQ(run.status, exit_usage);
    EXPECT_NE(run.err.find("everything"), std::string::npos) << run.err;
}

// FR-006 criterion 1.
TEST_F(LcartCliTest, CallersAreSortedByFileAndLine) {
    const fs::path database = two_modules_database();
    const Outcome run = query(database, {"callers", "app_run"});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(run.out, "app/legacy.c:4:12\tlegacy_run\n"
                       "app/main.c:10:12\tmain\n");
    const Outcome none = query(database, {"callers", "main"});
    EXPECT_EQ(none.status, exit_success) << none.err;
    EXPECT_EQ(none.out, "");
}

// FR-006 criterion 2.
TEST_F(LcartCliTest, AmbiguousNameListsCandidatesWithDistinctStatus) {
    const fs::path database = work_dir() / "static.lcart.db";
    analyze("static_same_name", {"left/util.c", "right/util.c"}, database);

    const Outcome run = query(database, {"callers", "helper"});
    EXPECT_EQ(run.status, exit_ambiguous) << run.err;
    EXPECT_EQ(run.out, "left/util.c:3\thelper\tleft/util.c#c:util.c@F@helper\n"
                       "right/util.c:9\thelper\tright/util.c#c:util.c@F@helper\n");
    EXPECT_NE(run.err.find("ambiguous"), std::string::npos) << run.err;

    // A candidate's USR names one function.
    const Outcome chosen = query(database, {"callers", "right/util.c#c:util.c@F@helper"});
    EXPECT_EQ(chosen.status, exit_success) << chosen.err;
    EXPECT_EQ(chosen.out, "right/util.c:6:18\tright_value\n");
}

TEST_F(LcartCliTest, UnknownSubjectIsNotFound) {
    const fs::path database = two_modules_database();
    for (const std::vector<std::string>& arguments : {std::vector<std::string>{"callers", "nope"},
                                                      {"callees", "nope"},
                                                      {"includes", "nope.c"},
                                                      {"included-by", "nope.h"},
                                                      {"module-deps", "nope"}}) {
        const Outcome run = query(database, arguments);
        EXPECT_EQ(run.status, exit_not_found) << arguments.front();
        EXPECT_EQ(run.out, "") << arguments.front();
        EXPECT_NE(run.err.find("nope"), std::string::npos) << run.err;
    }
}

// FR-006 criterion 3 and NFR-004 criteria 1 and 3.
TEST_F(LcartCliTest, ExportEqualsTheGoldenFile) {
    const fs::path database = two_modules_database();
    const std::string golden = read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl");
    ASSERT_FALSE(golden.empty());

    const Outcome run = query(database, {"export"});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(run.out, golden);

    const fs::path file = work_dir() / "facts.jsonl";
    const Outcome to_file = query(database, {"export", "--output", file.string()});
    EXPECT_EQ(to_file.status, exit_success) << to_file.err;
    EXPECT_EQ(to_file.out, "");
    EXPECT_EQ(read_file(file), golden);
}

// ADR-0017: `--output -` and `--output=-` mean standard output.
TEST_F(LcartCliTest, ExportToStandardOutputWithDash) {
    const fs::path database = two_modules_database();
    const std::string golden = read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl");
    for (const std::vector<std::string>& output :
         {std::vector<std::string>{"--output", "-"}, {"--output=-"}}) {
        std::vector<std::string> arguments{"export"};
        arguments.insert(arguments.end(), output.begin(), output.end());
        const Outcome run = query(database, arguments);
        EXPECT_EQ(run.status, exit_success) << run.err;
        EXPECT_EQ(run.out, golden) << output.back();
        EXPECT_FALSE(fs::exists(work_dir() / "-")) << output.back();
    }
}

TEST_F(LcartCliTest, CalleesListDirectAndIndirectCalls) {
    const fs::path database = work_dir() / "fn_pointers.lcart.db";
    analyze("fn_pointers", {"core.c", "rtu.c"}, database);

    const Outcome run = query(database, {"callees", "run"});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(run.out, "core.c:20:12\tdirect\tapply\n"
                       "core.c:20:28\tdirect\ttransfer\n"
                       "core.c:20:48\tindirect\tlocal.recv\n");
    // A plain function pointer has no member name.
    EXPECT_EQ(query(database, {"callees", "apply"}).out, "core.c:15:12\tindirect\t-\n"
                                                         "core.c:15:29\tindirect\t-\n");
    EXPECT_EQ(query(database, {"callees", "checksum"}).out, "");
}

TEST_F(LcartCliTest, IncludesOfAFile) {
    const fs::path database = two_modules_database();
    const Outcome run = query(database, {"includes", "app/main.c"});
    EXPECT_EQ(run.status, exit_success) << run.err;
    // The external header is shown with its path on this machine.
    const std::string first = "app/main.c:1\t\"app.h\"\tapp/app.h\n";
    const std::string second = "app/main.c:3\t<stdio.h>\t/";
    ASSERT_EQ(run.out.rfind(first, 0), 0U) << run.out;
    EXPECT_EQ(run.out.find(second), first.size()) << run.out;
    EXPECT_TRUE(run.out.ends_with("/stdio.h\n")) << run.out;
}

TEST_F(LcartCliTest, IncludedByAFile) {
    const fs::path database = two_modules_database();
    const Outcome run = query(database, {"included-by", "core/core.h"});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(run.out, "app/app.h:4\t\"core/core.h\"\n"
                       "core/core.c:1\t\"core.h\"\n");
}

TEST_F(LcartCliTest, ModuleDependencies) {
    const fs::path database = two_modules_database();
    const Outcome all = query(database, {"module-deps"});
    EXPECT_EQ(all.status, exit_success) << all.err;
    EXPECT_EQ(all.out, "app\tcore\n");
    EXPECT_EQ(query(database, {"module-deps", "app"}).out, "app\tcore\n");
    const Outcome core = query(database, {"module-deps", "core"});
    EXPECT_EQ(core.status, exit_success) << core.err;
    EXPECT_EQ(core.out, "");
}

// B-12 criterion 2 (NFR-004): the same input analysed twice into fresh
// databases exports the same bytes.
TEST_F(LcartCliTest, TwoAnalysesExportTheSameBytes) {
    const std::string first = export_of_fresh_analysis(
        "two_modules", {"app/legacy.c", "app/main.c", "core/core.c"}, "", "first");
    const std::string second = export_of_fresh_analysis(
        "two_modules", {"app/legacy.c", "app/main.c", "core/core.c"}, "", "second");
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

// The same with a failing translation unit, so diagnostics are compared too
// (the analysis still succeeds, FR-001).
TEST_F(LcartCliTest, TwoAnalysesWithDiagnosticsExportTheSameBytes) {
    const std::vector<std::string> units{"ok.c", "broken.c", "also_ok.c"};
    const std::string first = export_of_fresh_analysis("broken_tu", units, "-Iinclude", "first");
    const std::string second = export_of_fresh_analysis("broken_tu", units, "-Iinclude", "second");
    EXPECT_NE(
        first.find(
            R"("record":"diagnostic","translation_unit":"broken.c","file":"broken.c","line":4)"),
        std::string::npos)
        << first;
    EXPECT_EQ(first, second);
}

// ADR-0016: the analysis root is removed from diagnostic messages, so the
// export holds no checkout-specific path.
TEST_F(LcartCliTest, DiagnosticMessagesAreRootRelative) {
    const fs::path root = fixture("two_modules");
    const fs::path database = work_dir() / "gone.lcart.db";
    const fs::path compilation_database = work_dir() / "gone.json";
    std::ofstream{compilation_database}
        << R"([{"directory": ")" << root.string()
        << R"(", "command": "clang -c -I. core/core.c", "file": "core/core.c"},
 {"directory": ")"
        << root.string() << R"(", "command": "clang -c )" << (root / "gone.c").string()
        << R"(", "file": ")" << (root / "gone.c").string() << R"("}]
)";
    const Outcome analyzed = lcart({"analyze", "--root", root.string(), "--compilation-database",
                                    compilation_database.string(), "--db", database.string()});
    ASSERT_EQ(analyzed.status, exit_success) << analyzed.err;
    const Outcome exported = query(database, {"export"});
    EXPECT_NE(exported.out.find("'gone.c'"), std::string::npos) << exported.out;
    EXPECT_EQ(exported.out.find(root.string()), std::string::npos) << exported.out;
}

// B-12 criterion 1 (security T2).
TEST_F(LcartCliTest, WarnsWhenTheDatabaseIsInAWorkTreeAndNotIgnored) {
    const fs::path repo = work_dir() / "repo";
    fs::create_directories(repo / ".git");
    const fs::path root = fixture("two_modules");
    const std::string compilation_database =
        write_compilation_database(root, {"app/main.c", "core/core.c"}).string();
    const std::vector<std::string> arguments{"analyze", "--root", root.string(),
                                             "--compilation-database", compilation_database};

    const Outcome exposed = lcart(arguments, repo);
    EXPECT_EQ(exposed.status, exit_success) << exposed.err;
    EXPECT_TRUE(fs::is_regular_file(repo / "lcart-out" / "facts.lcart.db"));
    EXPECT_NE(exposed.err.find("warning"), std::string::npos) << exposed.err;
    EXPECT_NE(exposed.err.find("not ignored"), std::string::npos) << exposed.err;
    EXPECT_NE(exposed.err.find(repo.string()), std::string::npos) << exposed.err;

    std::ofstream{repo / ".gitignore"} << "/lcart-out/\n";
    std::vector<std::string> replace = arguments;
    replace.emplace_back("--force");
    const Outcome ignored = lcart(replace, repo);
    EXPECT_EQ(ignored.status, exit_success) << ignored.err;
    EXPECT_EQ(ignored.err.find("not ignored"), std::string::npos) << ignored.err;
}

// B-12 criterion 1 for the other output: `query export --output`.
TEST_F(LcartCliTest, WarnsWhenTheExportIsInAWorkTreeAndNotIgnored) {
    const fs::path database = two_modules_database();
    const fs::path repo = work_dir() / "repo";
    fs::create_directories(repo / ".git");
    const fs::path output = repo / "lcart-out" / "facts.jsonl";
    fs::create_directories(output.parent_path());

    const Outcome exposed = query(database, {"export", "--output", output.string()});
    EXPECT_EQ(exposed.status, exit_success) << exposed.err;
    EXPECT_NE(exposed.err.find("not ignored"), std::string::npos) << exposed.err;
    EXPECT_NE(exposed.err.find(repo.string()), std::string::npos) << exposed.err;
    EXPECT_TRUE(fs::is_regular_file(output));

    std::ofstream{repo / ".gitignore"} << "lcart-out/\n";
    const Outcome ignored = query(database, {"export", "--output", output.string()});
    EXPECT_EQ(ignored.status, exit_success) << ignored.err;
    EXPECT_EQ(ignored.err.find("not ignored"), std::string::npos) << ignored.err;
}

TEST_F(LcartCliTest, NoWarningOutsideAWorkTree) {
    const Outcome run =
        lcart({"analyze", "--root", fixture("two_modules").string(), "--compilation-database",
               write_compilation_database(fixture("two_modules"), {"core/core.c"}).string()});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(run.err.find("not ignored"), std::string::npos) << run.err;
    EXPECT_TRUE(fs::is_regular_file(work_dir() / "lcart-out" / "facts.lcart.db"));
}

// B-11 criterion 1 from the command line: rules and exclusions of
// lcart.toml reach the analysis; paths in it are relative to its directory.
TEST_F(LcartCliTest, AnalyzeReadsRulesAndExclusionsFromLcartToml) {
    const fs::path root = fixture("two_modules");
    static_cast<void>(
        write_compilation_database(root, {"app/legacy.c", "app/main.c", "core/core.c"}));
    const fs::path project = work_dir() / "project";
    fs::create_directories(project);
    fs::rename(work_dir() / "compile_commands.json", project / "compile_commands.json");
    std::ofstream{project / "lcart.toml"} << "[project]\n"
                                          << "root = \"" << root.string() << "\"\n"
                                          << "compilation_database = \"compile_commands.json\"\n"
                                          << "exclude = [\"app/legacy.c\"]\n"
                                          << "\n[[modules]]\nname = \"headers\"\n"
                                          << "paths = [\"**/*.h\"]\n";

    // lcart.toml in the working directory is read by default.
    const Outcome analyzed = lcart({"analyze", "--db", "facts.lcart.db"}, project);
    ASSERT_EQ(analyzed.status, exit_success) << analyzed.err;
    EXPECT_NE(analyzed.out.find("2/2"), std::string::npos) << analyzed.out;
    const fs::path database = project / "facts.lcart.db";
    EXPECT_EQ(query(database, {"module-deps"}).out, "app\tcore\n"
                                                    "app\theaders\n"
                                                    "core\theaders\n");
    EXPECT_EQ(query(database, {"callers", "app_run"}).out, "app/main.c:10:12\tmain\n");
    EXPECT_EQ(query(database, {"includes", "app/legacy.c"}).status, exit_not_found);

    // --config names it from elsewhere.
    const Outcome named = lcart({"analyze", "--config", (project / "lcart.toml").string(), "--db",
                                 (work_dir() / "named.lcart.db").string()});
    ASSERT_EQ(named.status, exit_success) << named.err;
    EXPECT_EQ(query(work_dir() / "named.lcart.db", {"module-deps"}).out,
              "app\tcore\napp\theaders\ncore\theaders\n");
}

TEST_F(LcartCliTest, InvalidLcartTomlIsAUsageError) {
    std::ofstream{work_dir() / "lcart.toml"} << "[project]\nunknown_key = 1\n";
    const Outcome run = lcart({"analyze"});
    EXPECT_EQ(run.status, exit_usage);
    EXPECT_NE(run.err.find("unknown_key"), std::string::npos) << run.err;
    EXPECT_FALSE(fs::exists(work_dir() / "lcart-out"));
}

// ADR-0016: a file that cannot be read is a failure (1); invalid content
// is a usage error (2).
TEST_F(LcartCliTest, MissingNamedConfigIsAFailure) {
    const Outcome run = lcart({"analyze", "--config", "nowhere.toml"});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_NE(run.err.find("nowhere.toml"), std::string::npos) << run.err;
}

TEST_F(LcartCliTest, ExistingDatabaseIsReplacedOnlyWithForce) {
    const fs::path database = two_modules_database();
    const fs::path root = fixture("two_modules");
    std::vector<std::string> arguments{"analyze",
                                       "--root",
                                       root.string(),
                                       "--compilation-database",
                                       write_compilation_database(root, {"core/core.c"}).string(),
                                       "--db",
                                       database.string()};

    const Outcome refused = lcart(arguments);
    EXPECT_EQ(refused.status, exit_failure);
    EXPECT_NE(refused.err.find("--force"), std::string::npos) << refused.err;
    EXPECT_EQ(query(database, {"callers", "app_run"}).out.empty(), false); // untouched

    arguments.emplace_back("--force");
    const Outcome replaced = lcart(arguments);
    EXPECT_EQ(replaced.status, exit_success) << replaced.err;
    EXPECT_EQ(query(database, {"callers", "app_run"}).status, exit_not_found);
}

TEST_F(LcartCliTest, ForceRefusesAFileThatIsNotAFactDatabase) {
    const fs::path root = fixture("two_modules");
    const fs::path compilation_database = write_compilation_database(root, {"core/core.c"});
    const std::string commands = read_file(compilation_database);
    const fs::path source = work_dir() / "main.c";
    std::ofstream{source} << "int main(void) { return 0; }\n";
    const fs::path other_sqlite = work_dir() / "other.sqlite";
    {
        // A SQLite database that lcart did not write.
        std::ofstream out{other_sqlite, std::ios::binary};
        out << std::string{"SQLite format 3\0", 16} << std::string(512, '\0');
    }
    const std::string other_bytes = read_file(other_sqlite);

    for (const fs::path& target : {source, compilation_database, other_sqlite}) {
        const Outcome run =
            lcart({"analyze", "--root", root.string(), "--compilation-database",
                   compilation_database.string(), "--db", target.string(), "--force"});
        EXPECT_EQ(run.status, exit_failure) << target;
        EXPECT_NE(run.err.find("not an lcart fact database"), std::string::npos) << run.err;
    }
    EXPECT_EQ(read_file(source), "int main(void) { return 0; }\n");
    EXPECT_EQ(read_file(compilation_database), commands);
    EXPECT_EQ(read_file(other_sqlite), other_bytes);
}

TEST_F(LcartCliTest, FailedAnalysisKeepsThePreviousDatabase) {
    const fs::path database = two_modules_database();
    const fs::path root = fixture("broken_tu");
    // No translation unit parses: the run fails (FR-001 criterion 3).
    const Outcome run = lcart({"analyze", "--root", root.string(), "--compilation-database",
                               write_compilation_database(root, {"broken.c"}, "-Iinclude").string(),
                               "--db", database.string(), "--force"});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_EQ(query(database, {"callers", "app_run"}).out,
              "app/legacy.c:4:12\tlegacy_run\napp/main.c:10:12\tmain\n");
    // Nothing is left next to it.
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator{work_dir()}) {
        names.push_back(entry.path().filename().string());
    }
    std::ranges::sort(names);
    EXPECT_EQ(names, (std::vector<std::string>{"compile_commands.json", "stderr.txt", "stdout.txt",
                                               "two_modules.lcart.db"}));
}

TEST_F(LcartCliTest, ForceDoesNotRemoveADirectory) {
    const fs::path directory = work_dir() / "not-a-database";
    fs::create_directories(directory);
    const fs::path root = fixture("two_modules");
    const Outcome run = lcart({"analyze", "--root", root.string(), "--compilation-database",
                               write_compilation_database(root, {"core/core.c"}).string(), "--db",
                               directory.string(), "--force"});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_TRUE(fs::is_directory(directory));
}

// ADR-0017: lcart replaces only its own files. The database being read is
// never replaced, however it is spelled.
TEST_F(LcartCliTest, MapRefusesToReplaceTheDatabase) {
    analyze("two_modules", {"app/legacy.c", "app/main.c", "core/core.c"},
            work_dir() / "lcart-out" / "facts.lcart.db");
    const fs::path database = work_dir() / "lcart-out" / "facts.lcart.db";
    const std::string bytes = read_file(database);
    ASSERT_FALSE(bytes.empty());

    for (const std::vector<std::string>& arguments :
         {std::vector<std::string>{"map", "--output", "lcart-out/facts.lcart.db"},
          {"map", "--output", "./lcart-out/../lcart-out/facts.lcart.db"},
          {"map", "--db", database.string(), "--output", "lcart-out/facts.lcart.db"}}) {
        expect_refused(arguments, "fact database", database, bytes);
    }
    // A link to the database is the same file.
    fs::create_symlink(database, work_dir() / "link.lcart.db");
    expect_refused({"map", "--db", "link.lcart.db", "--output", database.string()}, "fact database",
                   database, bytes);
}

// ADR-0017: an existing file is replaced only if it is a module map (its
// first line is exactly "# Module map").
TEST_F(LcartCliTest, MapRefusesToReplaceAFileThatIsNotAMap) {
    const fs::path database = two_modules_database();
    for (const std::string& content :
         {std::string{"int main(void) { return 0; }\n"}, std::string{"# Module mapping\n"},
          std::string{" # Module map\n"}, std::string{}}) {
        const fs::path other = work_dir() / "other.c";
        std::ofstream{other, std::ios::binary | std::ios::trunc} << content;
        expect_refused({"map", "--db", database.string(), "--output", other.string()},
                       "not a module map", other, content);
    }
}

std::vector<std::string> sorted_names(const fs::path& directory) {
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator{directory}) {
        names.push_back(entry.path().filename().string());
    }
    std::ranges::sort(names);
    return names;
}

// ADR-0018: `query export --output` replaces only its own files too. The
// database being read is never replaced, however it is spelled.
TEST_F(LcartCliTest, ExportRefusesToReplaceTheDatabase) {
    analyze("two_modules", {"app/legacy.c", "app/main.c", "core/core.c"},
            work_dir() / "lcart-out" / "facts.lcart.db");
    const fs::path database = work_dir() / "lcart-out" / "facts.lcart.db";
    const std::string bytes = read_file(database);
    ASSERT_FALSE(bytes.empty());

    for (const std::vector<std::string>& arguments :
         {std::vector<std::string>{"query", "export", "--output", "lcart-out/facts.lcart.db"},
          {"query", "export", "--output", "./lcart-out/../lcart-out/facts.lcart.db"},
          {"query", "export", "--db", database.string(), "--output", "lcart-out/facts.lcart.db"}}) {
        expect_refused(arguments, "fact database", database, bytes);
    }
    fs::create_symlink(database, work_dir() / "link.lcart.db");
    expect_refused({"query", "export", "--db", "link.lcart.db", "--output", database.string()},
                   "fact database", database, bytes);
}

// ADR-0018: an existing file is replaced only if it is an export (its first
// line starts with the export header).
TEST_F(LcartCliTest, ExportRefusesToReplaceAFileThatIsNotAnExport) {
    const fs::path database = two_modules_database();
    for (const std::string& content :
         {std::string{"int main(void) { return 0; }\n"}, std::string{"# Module map\n"},
          std::string{"{\"record\":\"header\",\"format\":\"lcart-factsx\"}\n"},
          std::string{" {\"record\":\"header\",\"format\":\"lcart-facts\",\n"}, std::string{}}) {
        const fs::path other = work_dir() / "other.jsonl";
        std::ofstream{other, std::ios::binary | std::ios::trunc} << content;
        expect_refused({"query", "export", "--db", database.string(), "--output", other.string()},
                       "not a facts export", other, content);
    }
}

// ADR-0018: an earlier export is replaced through a file next to it that is
// renamed, so nothing is left; the parent directory is created.
TEST_F(LcartCliTest, ExportReplacesAnEarlierExport) {
    const fs::path database = two_modules_database();
    const std::string golden = read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl");
    const fs::path output = work_dir() / "exports" / "facts.jsonl";
    const Outcome first = query(database, {"export", "--output", output.string()});
    EXPECT_EQ(first.status, exit_success) << first.err;
    EXPECT_EQ(read_file(output), golden);
    const Outcome again = query(database, {"export", "--output", output.string()});
    EXPECT_EQ(again.status, exit_success) << again.err;

    std::ofstream{output, std::ios::binary | std::ios::trunc}
        << "{\"record\":\"header\",\"format\":\"lcart-facts\",\"format_version\":0}\nstale\n";
    const Outcome second = query(database, {"export", "--output", output.string()});
    EXPECT_EQ(second.status, exit_success) << second.err;
    EXPECT_EQ(read_file(output), golden);
    EXPECT_EQ(sorted_names(output.parent_path()), (std::vector<std::string>{"facts.jsonl"}));
}

TEST_F(LcartCliTest, FailedWriteToStandardOutputIsAFailure) {
    const fs::path database = two_modules_database();
    for (const std::vector<std::string>& arguments :
         {std::vector<std::string>{"query", "callers", "app_run"},
          {"query", "module-deps"},
          {"query", "export"},
          {"map", "--output", "-"},
          {"--help"}}) {
        std::vector<std::string> with_database = arguments;
        if (arguments.front() == "query" || arguments.front() == "map") {
            with_database.emplace_back("--db");
            with_database.push_back(database.string());
        }
        const Outcome run = lcart(with_database, {}, "/dev/full");
        EXPECT_EQ(run.status, exit_failure) << arguments.back();
        EXPECT_NE(run.err.find("cannot write"), std::string::npos) << run.err;
    }
}

// ---- lcart map (ADR-0017) ----------------------------------------------------

std::string golden_map() {
    return read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.map.md");
}

// FR-007 criteria 2 and 3, B-13: with the default database and output, the
// map of two_modules is the golden file; a second run replaces it with the
// same bytes. No LLM and no network are involved: the command reads the
// database only.
TEST_F(LcartCliTest, MapWritesTheDefaultPath) {
    const fs::path output = work_dir() / "lcart-out" / "map.md";
    analyze("two_modules", {"app/legacy.c", "app/main.c", "core/core.c"},
            work_dir() / "lcart-out" / "facts.lcart.db");
    ASSERT_FALSE(golden_map().empty());

    const Outcome first = lcart({"map"});
    EXPECT_EQ(first.status, exit_success) << first.err;
    EXPECT_NE(first.out.find(output.string()), std::string::npos) << first.out;
    EXPECT_EQ(read_file(output), golden_map());

    // An earlier map is derived output: replaced without --force.
    std::ofstream{output} << "# Module map\nstale\n";
    const Outcome second = lcart({"map"});
    EXPECT_EQ(second.status, exit_success) << second.err;
    EXPECT_EQ(read_file(output), golden_map());
    // Written through a file next to it that is renamed: nothing is left.
    EXPECT_EQ(sorted_names(output.parent_path()),
              (std::vector<std::string>{"facts.lcart.db", "map.md"}));
}

TEST_F(LcartCliTest, MapToStandardOutput) {
    const fs::path database = two_modules_database();
    const Outcome run = lcart({"map", "--db", database.string(), "--output", "-"});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(run.out, golden_map());
    EXPECT_FALSE(fs::exists(work_dir() / "lcart-out"));
    EXPECT_FALSE(fs::exists(work_dir() / "-"));
}

TEST_F(LcartCliTest, MapCreatesTheParentDirectory) {
    const fs::path database = two_modules_database();
    const fs::path output = work_dir() / "docs" / "generated" / "map.md";
    const Outcome run = lcart({"map", "--db", database.string(), "--output=" + output.string()});
    EXPECT_EQ(run.status, exit_success) << run.err;
    EXPECT_EQ(read_file(output), golden_map());
}

TEST_F(LcartCliTest, MapWithoutDatabaseFailsAndWritesNothing) {
    const Outcome run = lcart({"map", "--db", "absent.lcart.db"});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_NE(run.err.find("lcart analyze"), std::string::npos) << run.err;
    EXPECT_FALSE(fs::exists(work_dir() / "absent.lcart.db"));
    EXPECT_FALSE(fs::exists(work_dir() / "lcart-out"));

    // A file that is not a fact database cannot be read either.
    std::ofstream{work_dir() / "not.lcart.db"} << "not a database\n";
    const Outcome unreadable = lcart({"map", "--db", "not.lcart.db"});
    EXPECT_EQ(unreadable.status, exit_failure);
    EXPECT_FALSE(fs::exists(work_dir() / "lcart-out"));
}

TEST_F(LcartCliTest, MapUsageErrors) {
    for (const std::vector<std::string>& arguments :
         {std::vector<std::string>{"map", "--frobnicate"}, {"map", "extra"}, {"map", "--output"}}) {
        const Outcome run = lcart(arguments);
        EXPECT_EQ(run.status, exit_usage) << arguments.back();
        EXPECT_NE(run.err.find(arguments.back()), std::string::npos) << run.err;
    }
}

TEST_F(LcartCliTest, MapToADirectoryFails) {
    const fs::path database = two_modules_database();
    const fs::path directory = work_dir() / "a-directory";
    fs::create_directories(directory);
    const Outcome run = lcart({"map", "--db", database.string(), "--output", directory.string()});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_TRUE(fs::is_directory(directory));
    EXPECT_TRUE(fs::is_empty(directory));
    // No hidden partial file is left next to it.
    for (const std::string& name : sorted_names(work_dir())) {
        EXPECT_FALSE(name.starts_with(".a-directory")) << name;
    }
}

// Security T2 for the map file (ADR-0017).
TEST_F(LcartCliTest, WarnsWhenTheMapIsInAWorkTreeAndNotIgnored) {
    const fs::path database = two_modules_database();
    const fs::path repo = work_dir() / "repo";
    fs::create_directories(repo / ".git");

    const Outcome exposed = lcart({"map", "--db", database.string()}, repo);
    EXPECT_EQ(exposed.status, exit_success) << exposed.err;
    EXPECT_NE(exposed.err.find("not ignored"), std::string::npos) << exposed.err;
    EXPECT_NE(exposed.err.find("module map"), std::string::npos) << exposed.err;
    EXPECT_TRUE(fs::is_regular_file(repo / "lcart-out" / "map.md"));

    std::ofstream{repo / ".gitignore"} << "/lcart-out/\n";
    const Outcome ignored = lcart({"map", "--db", database.string()}, repo);
    EXPECT_EQ(ignored.status, exit_success) << ignored.err;
    EXPECT_EQ(ignored.err.find("not ignored"), std::string::npos) << ignored.err;

    // Standard output is no file: no warning.
    fs::remove(repo / ".gitignore");
    const Outcome to_stdout = lcart({"map", "--db", database.string(), "--output", "-"}, repo);
    EXPECT_EQ(to_stdout.err.find("not ignored"), std::string::npos) << to_stdout.err;
}

TEST_F(LcartCliTest, QueryWithoutDatabaseFailsAndCreatesNothing) {
    const fs::path database = work_dir() / "absent.lcart.db";
    const Outcome run = query(database, {"module-deps"});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_NE(run.err.find("lcart analyze"), std::string::npos) << run.err;
    EXPECT_FALSE(fs::exists(database));
}

// FR-001 criterion 3: non-zero only if no translation unit could be parsed.
TEST_F(LcartCliTest, AnalysisWithoutAnyParsedUnitFails) {
    const fs::path root = fixture("broken_tu");
    const Outcome run = lcart({"analyze", "--root", root.string(), "--compilation-database",
                               write_compilation_database(root, {"broken.c"}, "-Iinclude").string(),
                               "--db", (work_dir() / "broken.lcart.db").string()});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_NE(run.out.find("0/1"), std::string::npos) << run.out;
}

} // namespace
