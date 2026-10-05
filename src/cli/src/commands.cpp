#include "cli/commands.hpp"

#include "analysis/clang_analyzer.hpp"
#include "cli/git_exposure.hpp"
#include "common/version.hpp"
#include "config/project_config.hpp"
#include "facts/fact_export.hpp"
#include "facts/sqlite_fact_store.hpp"
#include "report/report_writer.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

// The ownership annotation that cppcoreguidelines-owning-memory checks for,
// defined as the Guidelines Support Library defines it; lcart does not
// depend on the GSL.
namespace gsl {
template <typename T>
using owner = T;
} // namespace gsl

namespace lcart::cli {

namespace {

namespace fs = std::filesystem;
using facts::FactQuery;
using facts::FileId;
using facts::FunctionFact;
using facts::FunctionId;

/// Why a command stopped, and the exit status that says so.
struct Failure {
    ExitStatus status{ExitStatus::failure};
    std::string message;
};

template <typename T>
using Outcome = std::expected<T, Failure>;

std::unexpected<Failure> fail(ExitStatus status, std::string message) {
    return std::unexpected(Failure{.status = status, .message = std::move(message)});
}

/// Reports `failure` on `err` and returns its exit status.
ExitStatus report(const Failure& failure, std::ostream& err) {
    err << "lcart: " << failure.message << '\n';
    return failure.status;
}

// ---- analyze ---------------------------------------------------------------

/// Security T2 (ADR-0016): warns on `err` if git could pick up the output
/// file `path`; `what` names it in the message.
void warn_if_exposed(const fs::path& path, std::string_view what, std::ostream& err) {
    if (const auto work_tree = unignored_git_work_tree(path, false)) {
        err << "lcart: warning: " << path.string() << " is inside the git work tree "
            << work_tree->string() << " and not ignored; " << what
            << " is as sensitive as the analysed code (docs/security.md, T2): add it to"
               " .gitignore\n";
    }
}

/// What `lcart analyze` analyses, with every path absolute.
struct AnalysisInput {
    fs::path compilation_database;
    fs::path root;
    config::ProjectConfig config;
};

/// Combines the command line and lcart.toml (ADR-0016): the command line
/// wins; paths in lcart.toml are relative to its directory.
Outcome<AnalysisInput> resolve_input(const AnalyzeCommand& command) {
    std::error_code ec;
    std::optional<fs::path> config_file = command.config;
    if (!config_file && fs::is_regular_file(default_config, ec)) {
        config_file = fs::path{default_config};
    }
    AnalysisInput input;
    fs::path base = fs::current_path(ec);
    if (ec) {
        return fail(ExitStatus::failure, "cannot read the working directory: " + ec.message());
    }
    if (config_file) {
        auto loaded = config::load_project_config(*config_file);
        if (!loaded) {
            // Unreadable is a failure; readable but invalid is a usage error.
            return fail(loaded.error().code == common::ErrorCode::io_error ? ExitStatus::failure
                                                                           : ExitStatus::usage,
                        loaded.error().message);
        }
        input.config = std::move(*loaded);
        base = fs::absolute(*config_file, ec).parent_path();
    }
    // An absolute path in lcart.toml stays as it is (operator/ replaces).
    const auto from_config = [&base](const std::string& path) {
        return (base / path).lexically_normal();
    };
    if (command.root) {
        input.root = fs::absolute(*command.root, ec);
    } else {
        input.root = input.config.root.empty() ? base : from_config(input.config.root);
    }
    if (command.compilation_database) {
        input.compilation_database = fs::absolute(*command.compilation_database, ec);
    } else if (!input.config.compilation_database.empty()) {
        input.compilation_database = from_config(input.config.compilation_database);
    } else {
        input.compilation_database = input.root / "compile_commands.json";
    }
    if (fs::is_directory(input.compilation_database, ec)) {
        input.compilation_database /= "compile_commands.json";
    }
    return input;
}

/// The fact database of a run of `lcart analyze` (ADR-0019): written to
/// `.<name>.partial` next to `target`, locked with `flock` for the whole
/// run, and moved over the target only by commit(). An interrupted or
/// failed run leaves it in place for `--resume`.
class PartialDatabase {
public:
    /// Fails if `target` exists and is not a fact database (lcart replaces
    /// only its own files), or exists and `replace` is not set; if another
    /// run holds the partial database; and, for `resume`, if there is no
    /// partial database or it is not a fact database. Without `resume`, an
    /// existing partial database (lcart's own file) is replaced, together
    /// with a `-journal` file that SQLite would otherwise roll back into the
    /// new database.
    static Outcome<PartialDatabase> open(const fs::path& target, bool replace, bool resume) {
        if (auto checked = check_target(target, replace); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        // The directory resolved (it exists now): SQLITE_OPEN_NOFOLLOW
        // refuses a link in any component of the name, and only the
        // partial database's own name must not be one. The lock, the
        // store and the final rename all use these paths.
        std::error_code ec;
        const fs::path directory = fs::canonical(target.parent_path(), ec);
        if (ec) {
            return fail(ExitStatus::failure,
                        "cannot resolve " + target.parent_path().string() + ": " + ec.message());
        }
        PartialDatabase partial;
        partial.target_ = directory / target.filename();
        partial.file_ = directory / ("." + target.filename().string() + ".partial");
        if (auto locked = partial.lock(resume); !locked) {
            return std::unexpected(std::move(locked.error()));
        }
        if (auto prepared = partial.prepare(resume); !prepared) {
            return std::unexpected(std::move(prepared.error()));
        }
        auto store =
            facts::SqliteFactStore::open(partial.file_.string(), facts::LinkPolicy::refuse);
        if (!store) {
            return fail(ExitStatus::failure, partial.file_.string() + ": " + store.error().message);
        }
        partial.store_ = std::move(*store);
        return partial;
    }

    PartialDatabase(const PartialDatabase&) = delete;
    PartialDatabase& operator=(const PartialDatabase&) = delete;
    PartialDatabase(PartialDatabase&& other) noexcept = default;
    PartialDatabase& operator=(PartialDatabase&&) = delete;
    /// Keeps the file: an interrupted or failed run is resumed from it.
    ~PartialDatabase() {
        // SQLite first: closing any descriptor of the file drops SQLite's
        // POSIX locks on it.
        store_.reset();
        lock_.reset();
    }

    [[nodiscard]] facts::AnalysisStore& store() const { return *store_; }
    [[nodiscard]] const fs::path& file() const { return file_; }

    /// Closes the database and moves it to the target, replacing any file
    /// there in one step.
    Outcome<void> commit() {
        store_.reset();
        std::error_code ec;
        fs::rename(file_, target_, ec);
        if (ec) {
            return fail(ExitStatus::failure, "cannot move the new database to " + target_.string() +
                                                 ": " + ec.message());
        }
        return {};
    }

    /// Closes and removes the database: a completed run with nothing worth
    /// keeping.
    void discard() {
        store_.reset();
        std::error_code ec;
        fs::remove(file_, ec);                       // best effort; the run has failed already
        fs::remove(file_.string() + "-journal", ec); // none after a clean close
    }

private:
    PartialDatabase() = default;

    /// Fails if `target` exists and is not a fact database, or exists and
    /// `replace` is not set; creates its directory.
    static Outcome<void> check_target(const fs::path& target, bool replace) {
        std::error_code ec;
        const fs::file_status status = fs::symlink_status(target, ec);
        if (fs::exists(status)) {
            if (!fs::is_regular_file(status) ||
                !facts::SqliteFactStore::is_fact_database(target.string())) {
                return fail(ExitStatus::failure, target.string() +
                                                     " exists and is not an lcart fact database;"
                                                     " lcart replaces only its own databases");
            }
            if (!replace) {
                return fail(ExitStatus::failure,
                            target.string() + " exists; pass --force to replace it");
            }
        }
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            return fail(ExitStatus::failure,
                        "cannot create " + target.parent_path().string() + ": " + ec.message());
        }
        return {};
    }

    /// Opens the partial database file and takes the `flock` on it; for a
    /// new analysis, creates it if missing. Nothing is created or written
    /// through a symbolic link: a new file is created with O_EXCL; an
    /// existing name is opened as it is (a link is followed, since open()
    /// with O_NOFOLLOW is out of reach without a vararg call), then refused
    /// unless it is the regular file at that name, before anything is
    /// written. The store reopens it with SQLITE_OPEN_NOFOLLOW.
    Outcome<void> lock(bool resume) {
        std::error_code ec;
        const bool exists = fs::exists(fs::symlink_status(file_, ec));
        if (resume && !exists) {
            return fail(ExitStatus::failure,
                        "no interrupted analysis to resume: " + file_.string() + " does not exist");
        }
        // "x" (O_EXCL) creates the file and fails on any existing name, a
        // symbolic link included. An existing name is opened without
        // creating ("r+"; this follows a link) and checked below.
        // "e": closed on exec (glibc).
        lock_ = std::make_unique<OpenFile>(file_, exists ? "r+e" : "w+xe");
        if (!lock_->is_open()) {
            return fail(ExitStatus::failure,
                        "cannot open " + file_.string() + ": " +
                            std::error_code{errno, std::generic_category()}.message());
        }
        const int descriptor = lock_->descriptor();
        if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            return fail(ExitStatus::failure, "another lcart analyze is writing " + file_.string() +
                                                 "; wait for it to end");
        }
        // lstat after the open: a symbolic link, or another file put at the
        // name after the open, is refused (lcart writes only its own file).
        struct stat opened{};
        struct stat named{};
        if (::fstat(descriptor, &opened) != 0 || ::lstat(file_.c_str(), &named) != 0 ||
            !S_ISREG(named.st_mode) || opened.st_dev != named.st_dev ||
            opened.st_ino != named.st_ino) {
            return fail(ExitStatus::failure, file_.string() + " is not a regular file");
        }
        return {};
    }

    /// For `resume`, checks the file is a fact database; otherwise empties
    /// it (keeping the locked file) and removes a leftover `-journal` file.
    [[nodiscard]] Outcome<void> prepare(bool resume) const {
        std::error_code ec;
        const fs::path journal = file_.string() + "-journal";
        if (resume) {
            // With a -journal file left by a crash, SQLite first rolls the
            // unfinished translation unit back, which a read-only check
            // cannot do; opening the store checks it then.
            if (!fs::exists(journal, ec) &&
                !facts::SqliteFactStore::is_fact_database(file_.string())) {
                return fail(ExitStatus::failure,
                            file_.string() + " is not an lcart fact database; start a new analysis"
                                             " without --resume");
            }
            return {};
        }
        // Truncated rather than removed: the lock stays on this file.
        if (::ftruncate(lock_->descriptor(), 0) != 0) {
            return fail(ExitStatus::failure,
                        "cannot replace " + file_.string() + ": " +
                            std::error_code{errno, std::generic_category()}.message());
        }
        fs::remove(journal, ec);
        if (ec) {
            return fail(ExitStatus::failure,
                        "cannot remove " + journal.string() + ": " + ec.message());
        }
        return {};
    }

    /// A file opened with std::fopen, closed with the object.
    class OpenFile {
    public:
        OpenFile(const fs::path& path, const char* mode) : file_{std::fopen(path.c_str(), mode)} {}
        OpenFile(const OpenFile&) = delete;
        OpenFile& operator=(const OpenFile&) = delete;
        OpenFile(OpenFile&&) = delete;
        OpenFile& operator=(OpenFile&&) = delete;
        ~OpenFile() {
            if (file_ != nullptr) {
                static_cast<void>(std::fclose(file_));
            }
        }

        [[nodiscard]] bool is_open() const { return file_ != nullptr; }
        /// Precondition: is_open().
        [[nodiscard]] int descriptor() const { return ::fileno(file_); }

    private:
        gsl::owner<std::FILE*> file_;
    };

    fs::path target_;
    fs::path file_;
    /// Holds the `flock` for the whole run (ADR-0019).
    std::unique_ptr<OpenFile> lock_;
    std::unique_ptr<facts::SqliteFactStore> store_;
};

/// Set by the SIGINT and SIGTERM handler, read by the analysis (ADR-0019).
/// Lock-free, so storing to it is async-signal-safe; constant-initialised,
/// so the handler never runs an initialisation.
std::atomic<bool>& interrupt_requested() {
    static constinit std::atomic<bool> requested{false};
    return requested;
}
static_assert(std::atomic<bool>::is_always_lock_free);

void request_interrupt(int /*signal*/) {
    interrupt_requested().store(true);
}

/// Installs the SIGINT and SIGTERM handler for one analysis, and restores
/// the earlier handlers afterwards. SA_RESETHAND resets only the signal
/// delivered: the next signal of the same kind has the default action and
/// ends the process at once; one of the other kind is another stop request
/// (ADR-0019; open questions).
class InterruptHandlers {
public:
    InterruptHandlers() {
        interrupt_requested().store(false);
        struct sigaction action{};
        action.sa_handler = &request_interrupt;
        sigemptyset(&action.sa_mask);
        // SA_RESTART: system calls in Clang and SQLite go on after the signal.
        action.sa_flags = static_cast<int>(SA_RESETHAND | SA_RESTART);
        ::sigaction(SIGINT, &action, &previous_interrupt_);
        ::sigaction(SIGTERM, &action, &previous_terminate_);
    }
    InterruptHandlers(const InterruptHandlers&) = delete;
    InterruptHandlers& operator=(const InterruptHandlers&) = delete;
    InterruptHandlers(InterruptHandlers&&) = delete;
    InterruptHandlers& operator=(InterruptHandlers&&) = delete;
    ~InterruptHandlers() {
        ::sigaction(SIGINT, &previous_interrupt_, nullptr);
        ::sigaction(SIGTERM, &previous_terminate_, nullptr);
    }

    [[nodiscard]] static const std::atomic<bool>& flag() { return interrupt_requested(); }

private:
    struct sigaction previous_interrupt_{};
    struct sigaction previous_terminate_{};
};

/// `3 min 20 s`, rounded up to whole seconds.
std::string duration_text(std::chrono::milliseconds duration) {
    const auto seconds = std::chrono::ceil<std::chrono::seconds>(duration).count();
    constexpr long long minute = 60;
    constexpr long long hour = 60 * minute;
    if (seconds >= hour) {
        return std::to_string(seconds / hour) + " h " + std::to_string(seconds % hour / minute) +
               " min";
    }
    if (seconds >= minute) {
        return std::to_string(seconds / minute) + " min " + std::to_string(seconds % minute) + " s";
    }
    return std::to_string(seconds) + " s";
}

/// One progress line on standard error (ADR-0019).
void print_progress(const analysis::AnalysisProgress& progress, std::ostream& err) {
    err << "lcart: progress: " << progress.parsed << " parsed and " << progress.failed
        << " failed in this run, " << progress.already_done << " already done, " << progress.total
        << " total";
    if (!progress.finished) {
        if (progress.remaining) {
            err << "; about " << duration_text(*progress.remaining) << " left";
        } else {
            err << "; time left not known yet";
        }
    }
    err << '\n' << std::flush;
}

/// The number of worker threads when `--jobs` is not given (ADR-0019).
unsigned default_jobs() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return std::clamp(hardware, 1U, analysis::max_jobs);
}

ExitStatus run_analyze(const AnalyzeCommand& command, std::ostream& out, std::ostream& err) {
    auto input = resolve_input(command);
    if (!input) {
        return report(input.error(), err);
    }
    std::error_code ec;
    const fs::path database = fs::absolute(command.database, ec);
    warn_if_exposed(database, "the fact database", err);
    auto partial = PartialDatabase::open(database, command.replace_database, command.resume);
    if (!partial) {
        return report(partial.error(), err);
    }

    const InterruptHandlers handlers;
    analysis::AnalysisRequest request{.compilation_database = input->compilation_database,
                                      .root = input->root,
                                      .exclude_globs = std::move(input->config.exclude),
                                      .module_rules = std::move(input->config.modules)};
    request.jobs = command.jobs.value_or(default_jobs());
    request.resume = command.resume;
    request.stop = &InterruptHandlers::flag();
    request.on_progress = [&err](const analysis::AnalysisProgress& progress) {
        print_progress(progress, err);
    };
    // Printed as each unit is committed, so a later failure or a second
    // signal does not lose them; --resume does not repeat them (ADR-0019).
    request.on_warning = [&err](const std::string& warning) {
        err << "lcart: warning: " << warning << '\n' << std::flush;
    };
    analysis::ClangAnalyzer analyzer;
    auto summary = analyzer.analyze(request, partial->store());
    if (!summary) {
        return report({.status = ExitStatus::failure, .message = summary.error().message}, err);
    }
    if (summary->interrupted) {
        // Counts of the whole analysis, unlike the progress line, which
        // counts this run apart from the units already done.
        out << "interrupted: "
            << summary->translation_units_parsed + summary->translation_units_failed << '/'
            << summary->translation_units_total << " translation units done ("
            << summary->translation_units_parsed << " parsed, " << summary->translation_units_failed
            << " failed, of which " << summary->translation_units_already_done
            << " by an earlier run), " << summary->diagnostics << " diagnostics in this run\n";
        err << "lcart: interrupted; the translation units done are kept in "
            << partial->file().string() << "; continue with the same options and --resume\n";
        return ExitStatus::interrupted;
    }
    out << "analysed " << summary->translation_units_parsed << '/'
        << summary->translation_units_total << " translation units";
    if (summary->translation_units_already_done > 0) {
        out << " (" << summary->translation_units_already_done << " already done)";
    }
    out << ", " << summary->diagnostics << " diagnostics\n";
    // FR-001 criterion 3: a partial analysis is still a result; none is a
    // failure, and an earlier database stays in place.
    if (summary->translation_units_parsed == 0) {
        partial->discard();
        return report({.status = ExitStatus::failure,
                       .message = "no translation unit could be parsed; " + database.string() +
                                  " was not written"},
                      err);
    }
    if (auto committed = partial->commit(); !committed) {
        return report(committed.error(), err);
    }
    out << "facts in " << database.string() << '\n';
    return ExitStatus::success;
}

// ---- query -----------------------------------------------------------------

Outcome<std::unique_ptr<facts::SqliteFactStore>> open_database(const fs::path& path) {
    std::error_code ec;
    const fs::path database = fs::absolute(path, ec);
    // Opening would create an empty database where none is.
    if (!fs::is_regular_file(database, ec)) {
        return fail(ExitStatus::failure,
                    "no fact database at " + database.string() + "; run lcart analyze first");
    }
    auto store = facts::SqliteFactStore::open(database.string());
    if (!store) {
        return fail(ExitStatus::failure, store.error().message);
    }
    return std::move(*store);
}

template <typename T>
Outcome<T> or_failure(common::Result<T> result) {
    if (!result) {
        return fail(ExitStatus::failure, result.error().message);
    }
    return std::move(*result);
}

Outcome<void> or_failure(const common::Status& status) {
    if (!status) {
        return fail(ExitStatus::failure, status.error().message);
    }
    return {};
}

/// Names of files and functions by id, read once each.
class Names {
public:
    explicit Names(const FactQuery& facts) : facts_(&facts) {}

    Outcome<std::string> file(FileId id) {
        if (const auto known = files_.find(id); known != files_.end()) {
            return known->second;
        }
        auto file = or_failure(facts_->file(id));
        if (!file) {
            return std::unexpected(file.error());
        }
        std::string path = *file ? (*file)->path : "?";
        files_.emplace(id, path);
        return path;
    }

    Outcome<std::string> function(FunctionId id) {
        auto function = or_failure(facts_->function(id));
        if (!function) {
            return std::unexpected(function.error());
        }
        return *function ? (*function)->name : "?";
    }

private:
    const FactQuery* facts_;
    std::map<FileId, std::string> files_;
};

/// The function `subject` names: a stored USR, else a unique name. For an
/// ambiguous name the candidates go to `out`, one per line (file, first
/// line, name, USR), and the status is ExitStatus::ambiguous (FR-006
/// criterion 2).
Outcome<FunctionFact> resolve_function(const FactQuery& facts, Names& names,
                                       const std::string& subject, std::ostream& out) {
    auto by_usr = or_failure(facts.find_function_by_usr(subject));
    if (!by_usr) {
        return std::unexpected(by_usr.error());
    }
    if (*by_usr) {
        return std::move(**by_usr);
    }
    auto candidates = or_failure(facts.find_functions(subject));
    if (!candidates) {
        return std::unexpected(candidates.error());
    }
    if (candidates->empty()) {
        return fail(ExitStatus::not_found, "no function '" + subject + "' in the fact database");
    }
    if (candidates->size() > 1) {
        for (const FunctionFact& candidate : *candidates) {
            auto path = names.file(candidate.file);
            if (!path) {
                return std::unexpected(path.error());
            }
            out << *path << ':' << candidate.line_begin << '\t' << candidate.name << '\t'
                << candidate.usr << '\n';
        }
        return fail(ExitStatus::ambiguous,
                    "'" + subject + "' is ambiguous: " + std::to_string(candidates->size()) +
                        " functions have this name; name one by its USR (third column)");
    }
    return std::move(candidates->front());
}

Outcome<facts::FileFact> resolve_file(const FactQuery& facts, const std::string& subject) {
    auto file = or_failure(facts.find_file(subject));
    if (!file) {
        return std::unexpected(file.error());
    }
    if (!*file) {
        return fail(ExitStatus::not_found, "no file '" + subject + "' in the fact database");
    }
    return std::move(**file);
}

std::string spelled_directive(const facts::IncludeFact& include) {
    return include.is_system ? '<' + include.spelled + '>' : '"' + include.spelled + '"';
}

Outcome<void> print_callers(const FactQuery& facts, const std::string& subject, std::ostream& out) {
    Names names{facts};
    auto function = resolve_function(facts, names, subject, out);
    if (!function) {
        return std::unexpected(function.error());
    }
    auto calls = or_failure(facts.calls_to(function->id));
    if (!calls) {
        return std::unexpected(calls.error());
    }
    for (const facts::CallFact& call : *calls) {
        auto file = names.file(call.site.file);
        auto caller = names.function(call.caller);
        if (!file || !caller) {
            return std::unexpected(file ? caller.error() : file.error());
        }
        out << *file << ':' << call.site.line << ':' << call.site.column << '\t' << *caller << '\n';
    }
    return {};
}

Outcome<void> print_callees(const FactQuery& facts, const std::string& subject, std::ostream& out) {
    Names names{facts};
    auto function = resolve_function(facts, names, subject, out);
    if (!function) {
        return std::unexpected(function.error());
    }
    auto calls = or_failure(facts.calls_from(function->id));
    if (!calls) {
        return std::unexpected(calls.error());
    }
    for (const facts::CallFact& call : *calls) {
        auto file = names.file(call.site.file);
        if (!file) {
            return std::unexpected(file.error());
        }
        out << *file << ':' << call.site.line << ':' << call.site.column << '\t';
        if (call.callee) {
            auto callee = names.function(*call.callee);
            if (!callee) {
                return std::unexpected(callee.error());
            }
            out << "direct\t" << *callee << '\n';
        } else {
            // A plain function pointer has no member name to show.
            out << "indirect\t" << (call.via.empty() ? "-" : call.via) << '\n';
        }
    }
    return {};
}

Outcome<void> print_includes(const FactQuery& facts, const std::string& subject,
                             std::ostream& out) {
    auto file = resolve_file(facts, subject);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto includes = or_failure(facts.includes_of(file->id));
    if (!includes) {
        return std::unexpected(includes.error());
    }
    Names names{facts};
    for (const facts::IncludeFact& include : *includes) {
        std::string target = "(unresolved)";
        if (include.included) {
            auto path = names.file(*include.included);
            if (!path) {
                return std::unexpected(path.error());
            }
            target = *path;
        }
        out << file->path << ':' << include.line << '\t' << spelled_directive(include) << '\t'
            << target << '\n';
    }
    return {};
}

Outcome<void> print_included_by(const FactQuery& facts, const std::string& subject,
                                std::ostream& out) {
    auto file = resolve_file(facts, subject);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto includes = or_failure(facts.included_by(file->id));
    if (!includes) {
        return std::unexpected(includes.error());
    }
    Names names{facts};
    for (const facts::IncludeFact& include : *includes) {
        auto includer = names.file(include.includer);
        if (!includer) {
            return std::unexpected(includer.error());
        }
        out << *includer << ':' << include.line << '\t' << spelled_directive(include) << '\n';
    }
    return {};
}

Outcome<void> print_module_dependencies(const FactQuery& facts, const std::string& subject,
                                        std::ostream& out) {
    std::vector<facts::ModuleFact> modules;
    if (subject.empty()) {
        auto all = or_failure(facts.modules());
        if (!all) {
            return std::unexpected(all.error());
        }
        modules = std::move(*all);
    } else {
        auto module = or_failure(facts.find_module(subject));
        if (!module) {
            return std::unexpected(module.error());
        }
        if (!*module) {
            return fail(ExitStatus::not_found, "no module '" + subject + "' in the fact database");
        }
        modules.push_back(std::move(**module));
    }
    for (const facts::ModuleFact& module : modules) {
        auto dependencies = or_failure(facts.module_dependencies(module.id));
        if (!dependencies) {
            return std::unexpected(dependencies.error());
        }
        for (const facts::ModuleFact& dependency : *dependencies) {
            out << module.name << '\t' << dependency.name << '\n';
        }
    }
    return {};
}

// ---- files lcart replaces (ADR-0017, ADR-0018) -----------------------------

/// Calls `write` on a hidden file next to `target`, creating its directory,
/// then renames it over `target`, so an existing file is replaced in one
/// step and a failed write leaves it as it was. The new file gets the
/// permissions of the one it replaces: an export or map is as sensitive as
/// the code, and a user who restricted it must not find it widened
/// (ADR-0018). Call check_own_output() first: derived output is replaced
/// without `--force`, but no other file is.
template <typename Write>
Outcome<void> replace_file(const fs::path& target, const Write& write) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        return fail(ExitStatus::failure,
                    "cannot create " + target.parent_path().string() + ": " + ec.message());
    }
    const fs::path partial = target.parent_path() / ("." + target.filename().string() +
                                                     ".partial-" + std::to_string(getpid()));
    {
        std::ofstream file{partial, std::ios::binary | std::ios::trunc};
        Outcome<void> written = file ? write(file) : Outcome<void>{};
        file.close();
        if (written && !file) {
            written = fail(ExitStatus::failure, "cannot write " + target.string());
        }
        if (written && fs::exists(target, ec)) {
            fs::permissions(partial, fs::status(target, ec).permissions(), ec);
            if (ec) {
                written = fail(ExitStatus::failure, "cannot keep the permissions of " +
                                                        target.string() + ": " + ec.message());
            }
        }
        if (!written) {
            fs::remove(partial, ec); // best effort; the run has failed already
            return written;
        }
    }
    fs::rename(partial, target, ec);
    if (ec) {
        const std::string message =
            "cannot move the new file to " + target.string() + ": " + ec.message();
        fs::remove(partial, ec); // best effort; the run has failed already
        return fail(ExitStatus::failure, message);
    }
    return {};
}

/// A kind of file lcart writes and may later replace: its name in messages
/// and how its first line is recognised.
struct OwnFileKind {
    std::string_view name;
    bool (*has_first_line)(std::string_view line);
};

/// Every module map starts with this line (report::write_module_map).
constexpr OwnFileKind module_map{
    .name = "module map",
    .has_first_line = [](std::string_view line) { return line == "# Module map"; },
};

/// Every facts export starts with its header record, whose first two keys
/// are fixed (facts::write_jsonl_export, ADR-0016); the rest of the line
/// holds the format version and the coverage, which vary.
constexpr OwnFileKind facts_export{
    .name = "facts export",
    .has_first_line =
        [](std::string_view line) {
            return line.starts_with(R"({"record":"header","format":"lcart-facts",)");
        },
};

/// lcart replaces only its own files (ADR-0016, ADR-0017, ADR-0018):
/// `output` (absolute) may be replaced if nothing is there or it is a file
/// of `kind`, and never if it is the fact database being read, however it
/// is spelled.
Outcome<void> check_own_output(const fs::path& output, const fs::path& database,
                               const OwnFileKind& kind) {
    if (!output.has_filename()) {
        return fail(ExitStatus::failure, output.string() + " names a directory, not a file");
    }
    std::error_code ec;
    const fs::path database_path = fs::absolute(database, ec).lexically_normal();
    if (output.lexically_normal() == database_path ||
        (fs::exists(output, ec) && fs::equivalent(output, database_path, ec))) {
        return fail(ExitStatus::failure,
                    output.string() + " is the fact database being read; choose another --output");
    }
    const fs::file_status status = fs::symlink_status(output, ec);
    if (!fs::exists(status)) {
        return {};
    }
    std::string first_line;
    if (fs::is_regular_file(status)) {
        std::ifstream file{output, std::ios::binary};
        std::getline(file, first_line);
        if (!file) {
            first_line.clear();
        }
    }
    if (first_line.empty() || !kind.has_first_line(first_line)) {
        return fail(ExitStatus::failure, output.string() + " exists and is not a " +
                                             std::string{kind.name} +
                                             "; lcart replaces only its own files");
    }
    return {};
}

Outcome<void> export_facts(const FactQuery& facts, const std::optional<fs::path>& output,
                           const fs::path& database, std::ostream& out, std::ostream& err) {
    if (!output) {
        return or_failure(facts::write_jsonl_export(facts, out));
    }
    std::error_code ec;
    const fs::path target = fs::absolute(*output, ec);
    if (auto allowed = check_own_output(target, database, facts_export); !allowed) {
        return allowed;
    }
    warn_if_exposed(target, "the export", err);
    return replace_file(target, [&facts](std::ostream& file) {
        return or_failure(facts::write_jsonl_export(facts, file));
    });
}

ExitStatus run_query(const QueryCommand& command, std::ostream& out, std::ostream& err) {
    auto store = open_database(command.database);
    if (!store) {
        return report(store.error(), err);
    }
    const FactQuery& facts = **store;
    Outcome<void> done;
    switch (command.kind) {
    case QueryKind::callers:
        done = print_callers(facts, command.subject, out);
        break;
    case QueryKind::callees:
        done = print_callees(facts, command.subject, out);
        break;
    case QueryKind::includes:
        done = print_includes(facts, command.subject, out);
        break;
    case QueryKind::included_by:
        done = print_included_by(facts, command.subject, out);
        break;
    case QueryKind::module_deps:
        done = print_module_dependencies(facts, command.subject, out);
        break;
    case QueryKind::export_facts:
        done = export_facts(facts, command.output, command.database, out, err);
        break;
    }
    out.flush();
    return done ? ExitStatus::success : report(done.error(), err);
}

// ---- map -------------------------------------------------------------------

ExitStatus run_map(const MapCommand& command, std::ostream& out, std::ostream& err) {
    auto store = open_database(command.database);
    if (!store) {
        return report(store.error(), err);
    }
    if (!command.output) {
        const Outcome<void> written = or_failure(report::write_module_map(**store, out));
        return written ? ExitStatus::success : report(written.error(), err);
    }
    std::error_code ec;
    const fs::path output = fs::absolute(*command.output, ec);
    if (auto allowed = check_own_output(output, command.database, module_map); !allowed) {
        return report(allowed.error(), err);
    }
    warn_if_exposed(output, "the module map", err);
    const FactQuery& facts = **store;
    if (auto replaced = replace_file(output,
                                     [&facts](std::ostream& file) {
                                         return or_failure(report::write_module_map(facts, file));
                                     });
        !replaced) {
        return report(replaced.error(), err);
    }
    out << "module map in " << output.string() << '\n';
    return ExitStatus::success;
}

} // namespace

ExitStatus run(const Command& command, std::ostream& out, std::ostream& err) {
    struct Visitor {
        std::ostream* out;
        std::ostream* err;

        ExitStatus operator()(const HelpCommand& /*help*/) const {
            *out << usage();
            return ExitStatus::success;
        }
        ExitStatus operator()(const VersionCommand& /*version*/) const {
            *out << "lcart " << common::version << '\n';
            return ExitStatus::success;
        }
        ExitStatus operator()(const AnalyzeCommand& analyze) const {
            return run_analyze(analyze, *out, *err);
        }
        ExitStatus operator()(const QueryCommand& query) const {
            return run_query(query, *out, *err);
        }
        ExitStatus operator()(const MapCommand& map) const { return run_map(map, *out, *err); }
    };
    const ExitStatus status = std::visit(Visitor{.out = &out, .err = &err}, command);
    // Results that did not reach their reader (a full disk, a closed pipe)
    // are a failure, not a success with no output.
    out.flush();
    if (!out && status == ExitStatus::success) {
        return report({.status = ExitStatus::failure, .message = "cannot write the output"}, err);
    }
    return status;
}

} // namespace lcart::cli
