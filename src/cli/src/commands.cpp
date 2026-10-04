#include "cli/commands.hpp"

#include "analysis/clang_analyzer.hpp"
#include "cli/git_exposure.hpp"
#include "common/version.hpp"
#include "config/project_config.hpp"
#include "facts/fact_export.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

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
            return fail(ExitStatus::usage, loaded.error().message);
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

/// A new fact database, written to a file next to `target` and moved over
/// it only by commit(), so a failed run leaves an existing database as it
/// was. Removes its file if it is never committed.
class PendingDatabase {
public:
    /// Fails if `target` exists and is not a fact database (lcart replaces
    /// only its own files), or exists and `replace` is not set.
    static Outcome<PendingDatabase> create(const fs::path& target, bool replace) {
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
        PendingDatabase pending;
        pending.target_ = target;
        pending.file_ = target.parent_path() /
                        ("." + target.filename().string() + ".partial-" + std::to_string(getpid()));
        fs::remove(pending.file_, ec); // a leftover of a crashed run with this pid
        auto store = facts::SqliteFactStore::open(pending.file_.string());
        if (!store) {
            return fail(ExitStatus::failure, store.error().message);
        }
        pending.store_ = std::move(*store);
        return pending;
    }

    PendingDatabase(const PendingDatabase&) = delete;
    PendingDatabase& operator=(const PendingDatabase&) = delete;
    PendingDatabase(PendingDatabase&& other) noexcept
        : target_{std::move(other.target_)}, file_{std::exchange(other.file_, {})},
          store_{std::move(other.store_)} {}
    PendingDatabase& operator=(PendingDatabase&&) = delete;
    ~PendingDatabase() {
        store_.reset();
        if (!file_.empty()) {
            std::error_code ec;
            fs::remove(file_, ec); // best effort; the run has failed already
        }
    }

    [[nodiscard]] facts::FactWriter& writer() const { return *store_; }

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
        file_.clear();
        return {};
    }

private:
    PendingDatabase() = default;

    fs::path target_;
    fs::path file_;
    std::unique_ptr<facts::SqliteFactStore> store_;
};

ExitStatus run_analyze(const AnalyzeCommand& command, std::ostream& out, std::ostream& err) {
    auto input = resolve_input(command);
    if (!input) {
        return report(input.error(), err);
    }
    std::error_code ec;
    const fs::path database = fs::absolute(command.database, ec);
    if (const auto work_tree = unignored_git_work_tree(database, false)) {
        err << "lcart: warning: " << database.string() << " is inside the git work tree "
            << work_tree->string()
            << " and not ignored; the fact database is as sensitive as the analysed code"
               " (docs/security.md, T2): add it to .gitignore\n";
    }
    auto pending = PendingDatabase::create(database, command.replace_database);
    if (!pending) {
        return report(pending.error(), err);
    }

    analysis::ClangAnalyzer analyzer;
    auto summary = analyzer.analyze({.compilation_database = input->compilation_database,
                                     .root = input->root,
                                     .exclude_globs = std::move(input->config.exclude),
                                     .module_rules = std::move(input->config.modules)},
                                    pending->writer());
    if (!summary) {
        return report({.status = ExitStatus::failure, .message = summary.error().message}, err);
    }
    for (const std::string& warning : summary->warnings) {
        err << "lcart: warning: " << warning << '\n';
    }
    out << "analysed " << summary->translation_units_parsed << '/'
        << summary->translation_units_total << " translation units, " << summary->diagnostics
        << " diagnostics\n";
    // FR-001 criterion 3: a partial analysis is still a result; none is a
    // failure, and an earlier database stays in place.
    if (summary->translation_units_parsed == 0) {
        return report({.status = ExitStatus::failure,
                       .message = "no translation unit could be parsed; " + database.string() +
                                  " was not written"},
                      err);
    }
    if (auto committed = pending->commit(); !committed) {
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

Outcome<void> export_facts(const FactQuery& facts, const std::optional<fs::path>& output,
                           std::ostream& out) {
    if (!output) {
        return or_failure(facts::write_jsonl_export(facts, out));
    }
    std::ofstream file{*output, std::ios::binary | std::ios::trunc};
    if (!file) {
        return fail(ExitStatus::failure, "cannot write " + output->string());
    }
    return or_failure(facts::write_jsonl_export(facts, file));
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
        done = export_facts(facts, command.output, out);
        break;
    }
    out.flush();
    return done ? ExitStatus::success : report(done.error(), err);
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
    };
    return std::visit(Visitor{.out = &out, .err = &err}, command);
}

} // namespace lcart::cli
