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
// line, exit statuses and T2 warning of ADR-0017), adr0018-replace-only-exports-v1
// (`query export --output` refuses the database and other files, replaces
// an earlier export in one step and keeps its permissions).
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <spawn.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

// The ownership annotation that cppcoreguidelines-owning-memory checks for,
// defined as the Guidelines Support Library defines it.
namespace gsl {
template <typename T>
using owner = T;
} // namespace gsl

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
/// ADR-0019.
constexpr int exit_interrupted = 5;

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

/// A copy of the two_modules database without its module_dependency table:
/// it opens, but every export and map of it fails half way.
fs::path drop_module_dependencies(const fs::path& database, const fs::path& copy) {
    fs::copy_file(database, copy);
    sqlite3* connection = nullptr;
    EXPECT_EQ(sqlite3_open(copy.c_str(), &connection), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(connection, "DROP TABLE module_dependency", nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(connection);
    return copy;
}

// ADR-0017, ADR-0018: a failed run leaves an earlier output as it was, and
// nothing next to it.
TEST_F(LcartCliTest, FailedRunKeepsTheEarlierExportAndMap) {
    const fs::path database = two_modules_database();
    const fs::path broken = drop_module_dependencies(database, work_dir() / "broken.lcart.db");
    const fs::path directory = work_dir() / "out";
    const fs::path export_file = directory / "facts.jsonl";
    const fs::path map_file = directory / "map.md";
    ASSERT_EQ(query(database, {"export", "--output", export_file.string()}).status, exit_success);
    ASSERT_EQ(lcart({"map", "--db", database.string(), "--output", map_file.string()}).status,
              exit_success);
    const std::string export_bytes = read_file(export_file);
    const std::string map_bytes = read_file(map_file);

    EXPECT_EQ(query(broken, {"export", "--output", export_file.string()}).status, exit_failure);
    EXPECT_EQ(lcart({"map", "--db", broken.string(), "--output", map_file.string()}).status,
              exit_failure);
    EXPECT_EQ(read_file(export_file), export_bytes);
    EXPECT_EQ(read_file(map_file), map_bytes);
    EXPECT_EQ(sorted_names(directory), (std::vector<std::string>{"facts.jsonl", "map.md"}));
}

// ADR-0018: the replaced file keeps its permissions; an export is as
// sensitive as the code (security T2).
TEST_F(LcartCliTest, ReplacingKeepsThePermissions) {
    const fs::path database = two_modules_database();
    const fs::path export_file = work_dir() / "facts.jsonl";
    const fs::path map_file = work_dir() / "map.md";
    ASSERT_EQ(query(database, {"export", "--output", export_file.string()}).status, exit_success);
    ASSERT_EQ(lcart({"map", "--db", database.string(), "--output", map_file.string()}).status,
              exit_success);
    const auto owner_only = fs::perms::owner_read | fs::perms::owner_write;
    fs::permissions(export_file, owner_only);
    fs::permissions(map_file, owner_only);

    EXPECT_EQ(query(database, {"export", "--output", export_file.string()}).status, exit_success);
    EXPECT_EQ(lcart({"map", "--db", database.string(), "--output", map_file.string()}).status,
              exit_success);
    EXPECT_EQ(fs::status(export_file).permissions(), owner_only);
    EXPECT_EQ(fs::status(map_file).permissions(), owner_only);
}

// ADR-0018: an output that names a directory is refused before anything is
// created.
TEST_F(LcartCliTest, OutputEndingInASlashIsRefused) {
    const fs::path database = two_modules_database();
    const Outcome run = query(database, {"export", "--output", "newdir/"});
    EXPECT_EQ(run.status, exit_failure);
    EXPECT_NE(run.err.find("names a directory"), std::string::npos) << run.err;
    EXPECT_FALSE(fs::exists(work_dir() / "newdir"));
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

// ---- B-16: parallel, interruptible and resumable analysis (ADR-0019) -------

/// A file held open with an exclusive `flock`, as a running `lcart analyze`
/// holds its partial database; the lock ends with the object.
class LockedFile {
public:
    explicit LockedFile(const fs::path& path)
        : file_{std::fopen(path.c_str(), "a+e")},
          locked_{file_ != nullptr && flock(fileno(file_), LOCK_EX | LOCK_NB) == 0} {}
    LockedFile(const LockedFile&) = delete;
    LockedFile& operator=(const LockedFile&) = delete;
    LockedFile(LockedFile&&) = delete;
    LockedFile& operator=(LockedFile&&) = delete;
    ~LockedFile() {
        if (file_ != nullptr) {
            static_cast<void>(std::fclose(file_));
        }
    }

    [[nodiscard]] bool locked() const { return locked_; }

private:
    gsl::owner<std::FILE*> file_;
    bool locked_;
};

/// Starts `program arguments...` in `directory` without a shell, standard
/// output and error going to `out` and `err`, or standard error to the
/// descriptor `err_descriptor` if it is not -1. Returns its pid, or -1.
pid_t start(const std::string& program, std::vector<std::string> arguments,
            const fs::path& directory, const fs::path& out, const fs::path& err,
            int err_descriptor = -1) {
    std::string name = program;
    std::vector<char*> argv{name.data()};
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (err_descriptor >= 0) {
        posix_spawn_file_actions_adddup2(&actions, err_descriptor, STDERR_FILENO);
    } else {
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, err.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, name.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        ADD_FAILURE() << "cannot start " << program << ": errno " << spawned;
        return -1;
    }
    return pid;
}

/// Waits for `pid`; the status is -1 if it did not exit normally.
Outcome finish(pid_t pid, const fs::path& out, const fs::path& err) {
    Outcome run;
    if (pid < 0) {
        return run;
    }
    int wait_status = 0;
    while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
    }
    run.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
    run.out = read_file(out);
    run.err = read_file(err);
    return run;
}

/// How often `needle` occurs in `text`.
int count_of(std::string_view text, std::string_view needle) {
    int count = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

/// The translation units of the fact database at `path` that are no longer
/// pending.
int done_units(const fs::path& path) {
    sqlite3* database = nullptr;
    int done = -1;
    if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database,
                               "SELECT count(*) FROM translation_unit WHERE status <> 'pending'",
                               -1, &statement, nullptr) == SQLITE_OK &&
            sqlite3_step(statement) == SQLITE_ROW) {
            done = sqlite3_column_int(statement, 0);
        }
        sqlite3_finalize(statement);
    }
    sqlite3_close(database);
    return done;
}

/// Whether process `pid` has a handler for `signal`: its bit in the SigCgt
/// mask of /proc/<pid>/status (proc(5)). Empty if that cannot be read.
std::optional<bool> catches(pid_t pid, int signal) {
    std::ifstream status{"/proc/" + std::to_string(pid) + "/status"};
    constexpr std::string_view key = "SigCgt:";
    for (std::string line; std::getline(status, line);) {
        if (line.starts_with(key)) {
            constexpr int hexadecimal = 16;
            const std::uint64_t mask = std::stoull(line.substr(key.size()), nullptr, hexadecimal);
            return ((mask >> (signal - 1)) & 1U) != 0;
        }
    }
    return std::nullopt;
}

/// Closes the descriptor it holds.
class Descriptor {
public:
    explicit Descriptor(int descriptor) : descriptor_{descriptor} {}
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&&) = delete;
    Descriptor& operator=(Descriptor&&) = delete;
    ~Descriptor() { close(); }

    [[nodiscard]] int get() const { return descriptor_; }
    void close() {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
            descriptor_ = -1;
        }
    }

private:
    int descriptor_;
};

/// Reads what is available on the socket `descriptor` into `text`, without
/// waiting. True at the end of file: every other end is closed.
bool read_available(int descriptor, std::string& text) {
    std::array<char, 4096> buffer{};
    ssize_t got = 0;
    while ((got = ::recv(descriptor, buffer.data(), buffer.size(), MSG_DONTWAIT)) > 0) {
        text.append(buffer.data(), static_cast<std::size_t>(got));
    }
    return got == 0;
}

/// Sends to the socket `descriptor` without waiting until its buffer is
/// full: a blocking write of even one byte to it then waits for a reader.
void fill_socket(int descriptor) {
    const std::string page(4096, '.');
    constexpr int no_wait = MSG_DONTWAIT | MSG_NOSIGNAL;
    while (::send(descriptor, page.data(), page.size(), no_wait) > 0) {
    }
    while (::send(descriptor, page.data(), 1, no_wait) > 0) {
    }
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << "errno " << errno;
}

/// Expects the wait status `wait_status` to say that `signal` ended the
/// process.
void expect_ended_by(int wait_status, int signal) {
    ASSERT_TRUE(WIFSIGNALED(wait_status))
        << "exit status " << (WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1);
    EXPECT_EQ(WTERMSIG(wait_status), signal);
}

/// Changes one META value of the fact database at `path`.
void set_meta(const fs::path& path, const std::string& key, const std::string& value) {
    sqlite3* database = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &database), SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(database, "UPDATE meta SET value = ?2 WHERE key = ?1", -1,
                                 &statement, nullptr),
              SQLITE_OK);
    sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    EXPECT_EQ(sqlite3_step(statement), SQLITE_DONE);
    EXPECT_EQ(sqlite3_changes(database), 1) << key;
    sqlite3_finalize(statement);
    sqlite3_close(database);
}

class LcartAnalyzeRunTest : public LcartCliTest {
protected:
    /// Generates a project of `units` translation units with
    /// scripts/gen-many-tus.py and returns its root (with its
    /// compile_commands.json).
    [[nodiscard]] fs::path generate(int units,
                                    const std::vector<std::string>& extra_arguments = {}) const {
        const fs::path root = work_dir() / "generated";
        const fs::path log = work_dir() / "generate.txt";
        std::vector<std::string> arguments{std::string{LCART_SOURCE_DIR} +
                                               "/scripts/gen-many-tus.py",
                                           root.string(), "--units", std::to_string(units)};
        for (const std::string& extra : extra_arguments) {
            arguments.push_back("--extra-argument=" + extra);
        }
        const Outcome run =
            finish(start("python3", std::move(arguments), work_dir(), log, log), log, log);
        EXPECT_EQ(run.status, 0) << run.err;
        return root;
    }

    /// `lcart analyze` of the generated project into `database`, plus
    /// `options`; waits for it.
    [[nodiscard]] Outcome analyze_generated(const fs::path& root, const fs::path& database,
                                            const std::vector<std::string>& options) const {
        return finish(start_analyze(root, database, options), work_dir() / "run.out",
                      work_dir() / "run.err");
    }

    /// Standard error goes to run.err, or to `err_descriptor` if it is not -1.
    [[nodiscard]] pid_t start_analyze(const fs::path& root, const fs::path& database,
                                      const std::vector<std::string>& options,
                                      int err_descriptor = -1) const {
        std::vector<std::string> arguments{"analyze", "--root", root.string(), "--db",
                                           database.string()};
        arguments.insert(arguments.end(), options.begin(), options.end());
        return start(LCART_EXECUTABLE, arguments, work_dir(), work_dir() / "run.out",
                     work_dir() / "run.err", err_descriptor);
    }

    [[nodiscard]] static fs::path partial_of(const fs::path& database) {
        return database.parent_path() / ("." + database.filename().string() + ".partial");
    }

    /// Arguments of `lcart analyze` for the two_modules fixture into `database`.
    [[nodiscard]] std::vector<std::string> two_modules_arguments(const fs::path& database) const {
        const fs::path root = fixture("two_modules");
        return {"analyze",
                "--root",
                root.string(),
                "--compilation-database",
                write_compilation_database(root, {"app/legacy.c", "app/main.c", "core/core.c"})
                    .string(),
                "--db",
                database.string()};
    }

    /// Translation units of the project that the signal tests interrupt:
    /// enough that, with one worker, the run is far from its end when the
    /// first unit is committed.
    static constexpr int interrupted_units = 120;
    /// A stripped argument (security T5): each unit then prints one warning
    /// when it is committed, which tells the test that the run is under way.
    static constexpr std::string_view warning_argument = "-fplugin=/nonexistent/evil.so";

    /// An uninterrupted analysis of a generated project.
    struct Reference {
        fs::path root;
        fs::path database;
    };
    [[nodiscard]] Reference reference(int units) const {
        Reference result{.root = generate(units, {std::string{warning_argument}}),
                         .database = work_dir() / "reference.lcart.db"};
        const Outcome run = analyze_generated(result.root, result.database, {"--jobs", "1"});
        EXPECT_EQ(run.status, exit_success) << run.err;
        return result;
    }

    /// Waits until the analysis `pid` has committed its first unit (its
    /// warning is on standard error, so the signal handlers are installed),
    /// or has ended, or a minute has passed. True in the first case.
    [[nodiscard]] bool wait_for_first_commit(pid_t pid) const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes{1};
        while (std::chrono::steady_clock::now() < deadline) {
            if (read_file(work_dir() / "run.err").find("lcart: warning:") != std::string::npos) {
                return true;
            }
            // WNOWAIT: an ended run stays waitable for finish().
            siginfo_t info{};
            if (waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0 ||
                info.si_pid != 0) {
                return false; // ended, or not our child
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    }

    /// Waits until `condition()` holds while `pid` runs, or a minute has
    /// passed. True if it held.
    template <typename Condition>
    [[nodiscard]] static bool wait_while_running(pid_t pid, Condition condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes{1};
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) {
                return true;
            }
            // WNOWAIT: an ended run stays waitable.
            siginfo_t info{};
            if (waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0 ||
                info.si_pid != 0) {
                return false; // ended, or not our child
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    }

    /// Analyses the project of `reference` into `database` with standard
    /// error on a socket. Once the first unit is committed, fills that
    /// socket, so that lcart's next write to standard error blocks and the
    /// run cannot end by itself; sends `first`, waits until lcart has no
    /// handler left for SIGINT and SIGTERM, then sends `second`. Returns
    /// lcart's wait status.
    [[nodiscard]] int stop_twice(const Reference& reference, const fs::path& database, int first,
                                 int second) const {
        std::array<int, 2> ends{-1, -1};
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends.data()) != 0) {
            ADD_FAILURE() << "no socket pair: errno " << errno;
            return -1;
        }
        const Descriptor test_end{ends[0]};
        Descriptor lcart_end{ends[1]};
        const pid_t pid = start_analyze(reference.root, database, {"--jobs", "1"}, lcart_end.get());
        if (pid <= 0) {
            // kill(-1, ...) would signal every process we may signal.
            ADD_FAILURE() << "lcart did not start";
            return -1;
        }
        hold_after_first_commit(pid, test_end.get(), lcart_end);
        send_twice(pid, first, second);
        return wait_after_releasing(pid, test_end.get());
    }

    /// Waits until the analysis `pid`, whose standard error the test reads
    /// from `test_end`, has committed a unit; then fills the socket through
    /// `lcart_end` and closes the test's copy of that end.
    static void hold_after_first_commit(pid_t pid, int test_end, Descriptor& lcart_end) {
        std::string err;
        EXPECT_TRUE(wait_while_running(pid, [&] {
            read_available(test_end, err);
            return err.find("lcart: warning:") != std::string::npos;
        })) << "no unit committed";
        fill_socket(lcart_end.get());
        lcart_end.close();
    }

    /// Sends `first` to `pid`, waits until it has no handler left for
    /// SIGINT and SIGTERM, and sends `second`.
    static void send_twice(pid_t pid, int first, int second) {
        EXPECT_EQ(kill(pid, first), 0);
        EXPECT_TRUE(wait_while_running(pid, [pid] {
            return catches(pid, SIGINT) == false && catches(pid, SIGTERM) == false;
        })) << "lcart still has a handler for SIGINT or SIGTERM";
        EXPECT_EQ(kill(pid, second), 0);
    }

    /// Reads the standard error of `pid` from `test_end`, which lets it
    /// write again in case it did not end, and returns its wait status.
    [[nodiscard]] static int wait_after_releasing(pid_t pid, int test_end) {
        std::string err;
        const bool ended = wait_while_running(pid, [&] { return read_available(test_end, err); });
        EXPECT_TRUE(ended) << "lcart did not end";
        if (!ended) {
            EXPECT_EQ(kill(pid, SIGKILL), 0);
        }
        int wait_status = 0;
        while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
        }
        return wait_status;
    }

    /// Ends an analysis of a generated project with `first` and then
    /// `second`, expects `second` to end it at once, then resumes it and
    /// checks that the result equals an uninterrupted analysis.
    void stop_twice_and_resume(int first, int second) const {
        const Reference uninterrupted = reference(interrupted_units);
        const fs::path database = work_dir() / "facts.lcart.db";
        expect_ended_by(stop_twice(uninterrupted, database, first, second), second);
        EXPECT_FALSE(fs::exists(database));
        const int done = done_units(partial_of(database));
        EXPECT_GT(done, 0) << "ended before the first unit";
        EXPECT_LT(done, interrupted_units) << "ended after the last unit";
        expect_resume_finishes(uninterrupted, database, done, "4");
    }

    /// Analyses the project of `reference` into `database`, sends `signal`
    /// once the first unit is committed and expects a clean end: exit
    /// status 5, the summary, the partial database with the units done and
    /// a warning printed for each of them. Returns their number.
    [[nodiscard]] int interrupt_after_first_commit(const Reference& reference,
                                                   const fs::path& database, int signal) const {
        const pid_t pid = start_analyze(reference.root, database, {"--jobs", "1"});
        if (pid <= 0) {
            // kill(-1, ...) would signal every process we may signal.
            ADD_FAILURE() << "lcart did not start";
            return -1;
        }
        EXPECT_TRUE(wait_for_first_commit(pid)) << "no unit committed";
        EXPECT_EQ(kill(pid, signal), 0);
        const Outcome run = finish(pid, work_dir() / "run.out", work_dir() / "run.err");
        EXPECT_EQ(run.status, exit_interrupted) << run.out << run.err;
        EXPECT_FALSE(fs::exists(database));
        const int done = done_units(partial_of(database));
        expect_interrupted_output(run, done);
        return done;
    }

    /// The output of a run interrupted after `done` units: the summary, the
    /// hint to resume, progress, and one warning per unit done.
    static void expect_interrupted_output(const Outcome& run, int done) {
        EXPECT_NE(run.err.find("--resume"), std::string::npos) << run.err;
        EXPECT_NE(run.err.find("lcart: progress:"), std::string::npos) << run.err;
        EXPECT_EQ(count_of(run.err, warning_argument), done) << run.err;
        EXPECT_NE(run.out.find("interrupted: " + std::to_string(done) + "/" +
                               std::to_string(interrupted_units) + " translation units done"),
                  std::string::npos)
            << run.out;
    }

    /// Interrupts an analysis of a generated project with `signal` once a
    /// unit is committed, resumes it with `resume_jobs` workers and checks
    /// that the result equals an uninterrupted analysis.
    void interrupt_and_resume(int signal, const std::string& resume_jobs) const {
        const Reference uninterrupted = reference(interrupted_units);
        const fs::path database = work_dir() / "facts.lcart.db";
        const int done = interrupt_after_first_commit(uninterrupted, database, signal);
        EXPECT_GT(done, 0) << "interrupted before the first unit";
        EXPECT_LT(done, interrupted_units) << "interrupted after the last unit";
        expect_resume_finishes(uninterrupted, database, done, resume_jobs);
    }

    /// Resumes the interrupted analysis in `database`, of which `done` units
    /// were committed, with `resume_jobs` workers: the summary and progress
    /// count them as already done, and the export equals `uninterrupted`'s.
    void expect_resume_finishes(const Reference& uninterrupted, const fs::path& database, int done,
                                const std::string& resume_jobs) const {
        const Outcome resumed =
            analyze_generated(uninterrupted.root, database, {"--resume", "--jobs", resume_jobs});
        ASSERT_EQ(resumed.status, exit_success) << resumed.err;
        const std::string already_done = std::to_string(done) + " already done";
        EXPECT_NE(resumed.out.find(already_done), std::string::npos) << resumed.out;
        EXPECT_NE(
            resumed.err.find(already_done + ", " + std::to_string(interrupted_units) + " total"),
            std::string::npos)
            << resumed.err;
        // The warnings of the units done before were printed by that run.
        EXPECT_EQ(count_of(resumed.err, warning_argument), interrupted_units - done) << resumed.err;
        EXPECT_FALSE(fs::exists(partial_of(database)));
        EXPECT_EQ(query(database, {"export"}).out, query(uninterrupted.database, {"export"}).out);
    }
};

// Verifies: b16-resumable-analysis-v2 (criterion 3: SIGINT ends the run
// cleanly, completed units are committed, the summary is printed, exit
// status 5) and nfr005-performance-budget-v2 (criteria 3 and 4: progress
// and summary; the resumed export equals an uninterrupted one and units
// done before are not parsed again, counted in the summary).
TEST_F(LcartAnalyzeRunTest, SigintEndsTheRunCleanlyAndResumeFinishesIt) {
    interrupt_and_resume(SIGINT, "4");
}

// Verifies: b16-resumable-analysis-v2 (criterion 3, for SIGTERM) and
// nfr005-performance-budget-v2 (criterion 4).
TEST_F(LcartAnalyzeRunTest, SigtermEndsTheRunCleanlyAndResumeFinishesIt) {
    interrupt_and_resume(SIGTERM, "1");
}

// Verifies: b18-second-signal-of-either-kind-v1 (criterion 2: SIGINT and
// then SIGTERM end lcart by SIGTERM; --resume then finishes the analysis
// with the export of an uninterrupted run),
// adr0020-second-signal-of-either-kind-v1 and nfr005-performance-budget-v2
// (criterion 4: the resumed export equals an uninterrupted one).
TEST_F(LcartAnalyzeRunTest, SigintThenSigtermEndsLcartAtOnceAndResumeFinishesIt) {
    stop_twice_and_resume(SIGINT, SIGTERM);
}

// Verifies: b18-second-signal-of-either-kind-v1 (criterion 2, SIGTERM and
// then SIGINT), adr0020-second-signal-of-either-kind-v1 and
// nfr005-performance-budget-v2 (criterion 4).
TEST_F(LcartAnalyzeRunTest, SigtermThenSigintEndsLcartAtOnceAndResumeFinishesIt) {
    stop_twice_and_resume(SIGTERM, SIGINT);
}

// Verifies: b16-resumable-analysis-v2 (criteria 1 and 4: the generated
// project analysed with 1 and with 4 workers gives the same export) and
// nfr004-deterministic-output-v1 (criterion 2, and the module map).
TEST_F(LcartAnalyzeRunTest, OneAndFourWorkersExportAndMapTheSameBytes) {
    const fs::path root = generate(40);
    const fs::path one = work_dir() / "one.lcart.db";
    const fs::path four = work_dir() / "four.lcart.db";
    const Outcome first = analyze_generated(root, one, {"--jobs", "1"});
    ASSERT_EQ(first.status, exit_success) << first.err;
    const Outcome second = analyze_generated(root, four, {"--jobs=4"});
    ASSERT_EQ(second.status, exit_success) << second.err;
    EXPECT_EQ(second.out.substr(0, second.out.find("facts in")),
              first.out.substr(0, first.out.find("facts in")));
    EXPECT_NE(second.err.find(
                  "lcart: progress: 39 parsed and 1 failed in this run, 0 already done, 40 total"),
              std::string::npos)
        << second.err;

    const Outcome exported = query(one, {"export"});
    ASSERT_EQ(exported.status, exit_success) << exported.err;
    EXPECT_NE(exported.out.find("#c:@F@main"), std::string::npos); // ADR-0013 renames
    EXPECT_EQ(query(four, {"export"}).out, exported.out);
    EXPECT_EQ(lcart({"map", "--db", four.string(), "--output", "-"}).out,
              lcart({"map", "--db", one.string(), "--output", "-"}).out);
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: `--jobs` outside 1..256
// is a usage error).
TEST_F(LcartAnalyzeRunTest, JobsOutOfRangeIsAUsageError) {
    for (const char* jobs : {"0", "257", "many"}) {
        const Outcome run = lcart({"analyze", "--jobs", jobs});
        EXPECT_EQ(run.status, exit_usage) << jobs;
        EXPECT_NE(run.err.find("--jobs"), std::string::npos) << run.err;
    }
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: one run per target; the
// partial database is locked for the whole run).
TEST_F(LcartAnalyzeRunTest, SecondRunOnTheSameTargetIsRefused) {
    const fs::path database = work_dir() / "facts.lcart.db";
    const fs::path partial = partial_of(database);
    const std::vector<std::string> arguments = two_modules_arguments(database);
    std::vector<std::string> resume = arguments;
    resume.emplace_back("--resume");
    {
        const LockedFile running{partial};
        ASSERT_TRUE(running.locked());
        const Outcome refused = lcart(arguments);
        EXPECT_EQ(refused.status, exit_failure);
        EXPECT_NE(refused.err.find("another lcart analyze"), std::string::npos) << refused.err;
        EXPECT_EQ(lcart(resume).status, exit_failure);
    }
    // The lock ended with the file.
    const Outcome analysed = lcart(arguments);
    EXPECT_EQ(analysed.status, exit_success) << analysed.err;
    EXPECT_FALSE(fs::exists(partial));
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: without --resume, an
// unlocked partial database is replaced together with its -journal file).
TEST_F(LcartAnalyzeRunTest, NewRunReplacesAnUnlockedPartialDatabaseAndItsJournal) {
    const fs::path database = work_dir() / "facts.lcart.db";
    const fs::path partial = partial_of(database);
    const fs::path journal = partial.string() + "-journal";
    std::ofstream{partial} << "left behind by a crash";
    std::ofstream{journal} << "left behind by a crash";

    const fs::path root = fixture("two_modules");
    const Outcome run = lcart(
        {"analyze", "--root", root.string(), "--compilation-database",
         write_compilation_database(root, {"app/legacy.c", "app/main.c", "core/core.c"}).string(),
         "--db", database.string()});
    ASSERT_EQ(run.status, exit_success) << run.err;
    EXPECT_FALSE(fs::exists(partial));
    EXPECT_FALSE(fs::exists(journal));
    EXPECT_EQ(query(database, {"export"}).out,
              read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl"));
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: the partial database is
// lcart's own file; a symbolic link in its place is refused, with and
// without --resume, and the file it names is left alone).
TEST_F(LcartAnalyzeRunTest, SymbolicLinkInPlaceOfThePartialDatabaseIsRefused) {
    const fs::path database = work_dir() / "facts.lcart.db";
    const fs::path partial = partial_of(database);
    const fs::path victim = work_dir() / "victim.txt";
    std::ofstream{victim} << "not lcart's";
    fs::create_symlink(victim, partial);

    std::vector<std::string> arguments = two_modules_arguments(database);
    const Outcome refused = lcart(arguments);
    EXPECT_EQ(refused.status, exit_failure);
    EXPECT_NE(refused.err.find("not a regular file"), std::string::npos) << refused.err;
    arguments.emplace_back("--resume");
    const Outcome resume_refused = lcart(arguments);
    EXPECT_EQ(resume_refused.status, exit_failure);
    EXPECT_NE(resume_refused.err.find("not a regular file"), std::string::npos)
        << resume_refused.err;

    EXPECT_TRUE(fs::is_symlink(fs::symlink_status(partial)));
    EXPECT_EQ(read_file(victim), "not lcart's");
    EXPECT_FALSE(fs::exists(database));
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: only the partial database's
// own name must not be a symbolic link; a database in a directory reached
// through a link is analysed, replaced and resumed as usual).
TEST_F(LcartAnalyzeRunTest, DatabaseInALinkedDirectoryIsAnalysedAndResumed) {
    fs::create_directory(work_dir() / "out");
    fs::create_directory_symlink(work_dir() / "out", work_dir() / "outlink");
    const fs::path database = work_dir() / "outlink" / "facts.lcart.db";
    const fs::path real_database = work_dir() / "out" / "facts.lcart.db";
    std::vector<std::string> arguments = two_modules_arguments(database);

    const Outcome analysed = lcart(arguments);
    ASSERT_EQ(analysed.status, exit_success) << analysed.err;
    EXPECT_TRUE(fs::is_regular_file(fs::symlink_status(real_database)));
    EXPECT_FALSE(fs::exists(partial_of(real_database)));
    const std::string golden = read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl");
    EXPECT_EQ(query(database, {"export"}).out, golden);

    // A completed analysis in the place of the partial database: resume
    // has nothing left to parse and moves it over the target.
    fs::copy_file(real_database, partial_of(real_database));
    arguments.emplace_back("--force");
    arguments.emplace_back("--resume");
    const Outcome resumed = lcart(arguments);
    ASSERT_EQ(resumed.status, exit_success) << resumed.err;
    EXPECT_NE(resumed.out.find("3 already done"), std::string::npos) << resumed.out;
    EXPECT_FALSE(fs::exists(partial_of(real_database)));
    EXPECT_EQ(query(database, {"export"}).out, golden);
}

/// Arguments of `lcart analyze --resume` for the two_modules fixture, and
/// the files involved.
struct ResumeCase {
    fs::path compilation_database;
    fs::path database;
    fs::path partial;
    std::vector<std::string> arguments;
};

class LcartResumeTest : public LcartAnalyzeRunTest {
protected:
    void SetUp() override {
        LcartAnalyzeRunTest::SetUp();
        const fs::path root = fixture("two_modules");
        case_.compilation_database = write_compilation_database(root, units_);
        case_.database = work_dir() / "facts.lcart.db";
        case_.partial = partial_of(case_.database);
        case_.arguments = {"analyze",
                           "--root",
                           root.string(),
                           "--compilation-database",
                           case_.compilation_database.string(),
                           "--db",
                           case_.database.string(),
                           "--resume"};
    }

    /// Puts a completed analysis in the place of the partial database:
    /// resuming it has nothing left to parse.
    void put_completed_analysis() const {
        const fs::path completed = work_dir() / "completed.lcart.db";
        if (!fs::exists(completed)) {
            analyze("two_modules", units_, completed);
        }
        fs::copy_file(completed, case_.partial, fs::copy_options::overwrite_existing);
    }

    /// Expects `lcart analyze --resume` to refuse, naming `reason`, and to
    /// leave the partial database for another try.
    void expect_refused_because(std::string_view reason) const {
        const Outcome run = lcart(case_.arguments);
        EXPECT_EQ(run.status, exit_failure) << reason;
        EXPECT_NE(run.err.find(reason), std::string::npos) << run.err;
        EXPECT_TRUE(fs::exists(case_.partial)) << reason;
        EXPECT_FALSE(fs::exists(case_.database)) << reason;
    }

    [[nodiscard]] const ResumeCase& resume_case() const { return case_; }
    [[nodiscard]] const std::vector<std::string>& units() const { return units_; }

private:
    std::vector<std::string> units_{"app/legacy.c", "app/main.c", "core/core.c"};
    ResumeCase case_;
};

// Verifies: b16-resumable-analysis-v2 (criterion 2: resume refuses, and
// says why, when there is nothing to resume or the partial database is not
// a fact database).
TEST_F(LcartResumeTest, ResumeRefusesWithoutAnInterruptedAnalysis) {
    const Outcome nothing = lcart(resume_case().arguments);
    EXPECT_EQ(nothing.status, exit_failure);
    EXPECT_NE(nothing.err.find("no interrupted analysis"), std::string::npos) << nothing.err;
    EXPECT_FALSE(fs::exists(resume_case().partial));

    std::ofstream{resume_case().partial} << "not a database";
    expect_refused_because("not an lcart fact database");
}

// Verifies: b16-resumable-analysis-v2 (criterion 2: resume refuses, and
// says why, when the schema version or the lcart version changed).
TEST_F(LcartResumeTest, ResumeRefusesAnotherSchemaOrLcartVersion) {
    put_completed_analysis();
    set_meta(resume_case().partial, "schema_version", "1");
    expect_refused_because("schema version 1");

    put_completed_analysis();
    set_meta(resume_case().partial, "tool_version", "0.0.0-other");
    expect_refused_because("lcart version");
}

// Verifies: b16-resumable-analysis-v2 (criterion 2: resume refuses, and
// says why, when the compilation database or the configuration changed).
TEST_F(LcartResumeTest, ResumeRefusesAChangedCompilationDatabaseOrConfiguration) {
    put_completed_analysis();
    std::ofstream{resume_case().compilation_database, std::ios::app} << "\n";
    expect_refused_because("compilation database");
    static_cast<void>(write_compilation_database(fixture("two_modules"), units()));

    std::ofstream{work_dir() / "lcart.toml"} << "[project]\nexclude = [\"app/legacy.c\"]\n";
    expect_refused_because("configuration");
}

// Verifies: b16-resumable-analysis-v2 (resume of an analysis whose units
// are all done writes the database; ADR-0019: the summary counts them).
TEST_F(LcartResumeTest, ResumeOfACompletedAnalysisWritesTheDatabase) {
    put_completed_analysis();
    const Outcome resumed = lcart(resume_case().arguments);
    ASSERT_EQ(resumed.status, exit_success) << resumed.err;
    EXPECT_NE(resumed.out.find("3 already done"), std::string::npos) << resumed.out;
    EXPECT_FALSE(fs::exists(resume_case().partial));
    EXPECT_EQ(query(resume_case().database, {"export"}).out,
              read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl"));
}

} // namespace
