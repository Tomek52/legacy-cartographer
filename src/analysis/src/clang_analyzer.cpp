#include "analysis/clang_analyzer.hpp"

#include "common/version.hpp"
#include "config/path_glob.hpp"
#include "facts/fact_model.hpp"

#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "batch_writer.hpp"
#include "fact_path.hpp"
#include "translation_unit_batch.hpp"
#include "translation_unit_recorder.hpp"

namespace lcart::analysis {

namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

// What a resumed run must find unchanged (ADR-0019), as META keys
// (docs/architecture/data-model.md). schema_version is checked when the
// store is opened.
constexpr std::string_view tool_version_key = "tool_version";
constexpr std::string_view analysis_root_key = "analysis_root";
constexpr std::string_view compilation_database_key = "compilation_database_sha256";
constexpr std::string_view configuration_key = "configuration_sha256";

/// How often the writer wakes up while it waits for a batch, to notice a
/// stop request and to keep the progress reports due (ADR-0019).
constexpr milliseconds poll_period{100};

[[nodiscard]] common::Error invalid(std::string message) {
    return common::Error{.code = common::ErrorCode::invalid_argument,
                         .message = std::move(message)};
}

[[nodiscard]] bool stop_requested(const std::atomic<bool>* stop) {
    return stop != nullptr && stop->load();
}

[[nodiscard]] common::Result<std::unique_ptr<clang::tooling::CompilationDatabase>>
load_compilation_database(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::io_error,
                          .message = "compilation database not found: " + path.string()});
    }
    std::string message;
    std::unique_ptr<clang::tooling::CompilationDatabase> database =
        clang::tooling::JSONCompilationDatabase::loadFromFile(
            path.string(), message, clang::tooling::JSONCommandLineSyntax::AutoDetect);
    if (!database) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::parse_error, .message = message});
    }
    // Expand @file arguments before filtering, so a response file cannot
    // smuggle plugin options past remove_code_loading_arguments(). Only the
    // main thread fetches commands, so the real file system is safe here.
    return clang::tooling::expandResponseFiles(std::move(database), llvm::vfs::getRealFileSystem());
}

/// Whether the translation unit `file` (as the compilation database names
/// it) is under `root` and its root-relative path matches one of `exclude`.
/// The path is computed as for file facts.
[[nodiscard]] bool is_excluded(const std::string& file, const std::filesystem::path& root,
                               std::span<const config::PathGlob> exclude) {
    if (exclude.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(file, ec);
    if (ec) {
        canonical = std::filesystem::path{file}.lexically_normal();
    }
    const std::filesystem::path relative = canonical.lexically_relative(root);
    if (relative.empty() || *relative.begin() == std::filesystem::path{".."}) {
        return false;
    }
    const std::string path = relative.generic_string();
    return std::ranges::any_of(
        exclude, [&path](const config::PathGlob& glob) { return glob.matches(path); });
}

/// The spellings of the analysis root that messages may contain, each with
/// a trailing `/`, longest first: canonical, and as given (made absolute).
[[nodiscard]] std::vector<std::string> message_root_prefixes(const std::filesystem::path& given,
                                                             const std::filesystem::path& root) {
    std::error_code ec;
    std::vector<std::string> prefixes{root.string()};
    const std::filesystem::path absolute = std::filesystem::absolute(given, ec).lexically_normal();
    if (!ec) {
        prefixes.push_back(absolute.string());
    }
    for (std::string& prefix : prefixes) {
        if (!prefix.ends_with('/')) {
            prefix += '/';
        }
    }
    std::ranges::sort(prefixes, [](const std::string& left, const std::string& right) {
        return left.size() > right.size();
    });
    const auto [first, last] = std::ranges::unique(prefixes);
    prefixes.erase(first, last);
    return prefixes;
}

/// SHA-256 over a sequence of strings, each length-prefixed so that no two
/// different sequences hash alike.
class SequenceHash {
public:
    void add(std::string_view text) {
        sha_.update(std::to_string(text.size()) + ':');
        sha_.update(text);
    }
    [[nodiscard]] std::string hex() { return llvm::toHex(sha_.final(), /*LowerCase=*/true); }

private:
    llvm::SHA256 sha_;
};

[[nodiscard]] common::Result<std::string> file_sha256(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    std::ostringstream content;
    content << in.rdbuf();
    if (!in) {
        return std::unexpected(common::Error{.code = common::ErrorCode::io_error,
                                             .message = "cannot read " + path.string()});
    }
    return llvm::toHex(llvm::SHA256::hash(llvm::arrayRefFromStringRef(content.str())),
                       /*LowerCase=*/true);
}

/// What a resumed run must find unchanged (ADR-0019).
struct Fingerprint {
    std::string tool_version;
    std::string analysis_root;
    std::string compilation_database_sha256;
    /// The analysis root, the module rules and exclusions in order, and the
    /// sorted translation units after filtering.
    std::string configuration_sha256;
};

[[nodiscard]] common::Result<Fingerprint> fingerprint_of(const AnalysisRequest& request,
                                                         const std::filesystem::path& root,
                                                         std::span<const std::string> files) {
    auto compilation_database = file_sha256(request.compilation_database);
    if (!compilation_database) {
        return std::unexpected(compilation_database.error());
    }
    SequenceHash configuration;
    configuration.add(root.string());
    configuration.add(std::to_string(request.module_rules.size()));
    for (const config::ModuleRule& rule : request.module_rules) {
        configuration.add(rule.name);
        configuration.add(std::to_string(rule.paths.size()));
        for (const config::PathGlob& glob : rule.paths) {
            configuration.add(glob.pattern());
        }
    }
    configuration.add(std::to_string(request.exclude_globs.size()));
    for (const config::PathGlob& glob : request.exclude_globs) {
        configuration.add(glob.pattern());
    }
    configuration.add(std::to_string(files.size()));
    for (const std::string& file : files) {
        configuration.add(file);
    }
    return Fingerprint{.tool_version = std::string{common::version},
                       .analysis_root = root.string(),
                       .compilation_database_sha256 = std::move(*compilation_database),
                       .configuration_sha256 = configuration.hex()};
}

[[nodiscard]] common::Status record_fingerprint(facts::AnalysisStore& store,
                                                const Fingerprint& fingerprint) {
    for (const auto& [key, value] :
         {std::pair{tool_version_key, &fingerprint.tool_version},
          std::pair{analysis_root_key, &fingerprint.analysis_root},
          std::pair{compilation_database_key, &fingerprint.compilation_database_sha256},
          std::pair{configuration_key, &fingerprint.configuration_sha256}}) {
        if (auto written = store.set_meta(key, *value); !written) {
            return written;
        }
    }
    return {};
}

/// Fails, naming every difference, unless `store` holds an analysis with
/// `fingerprint`.
[[nodiscard]] common::Status check_fingerprint(const facts::AnalysisStore& store,
                                               const Fingerprint& fingerprint,
                                               const std::filesystem::path& compilation_database) {
    std::map<std::string_view, std::string> recorded;
    for (const std::string_view key :
         {tool_version_key, compilation_database_key, configuration_key}) {
        auto value = store.meta(key);
        if (!value) {
            return std::unexpected(value.error());
        }
        if (!*value) {
            return std::unexpected(common::Error{
                .code = common::ErrorCode::incompatible_database,
                .message = "cannot resume: the database holds no analysis to resume"});
        }
        recorded.emplace(key, std::move(**value));
    }
    std::vector<std::string> differences;
    if (recorded[compilation_database_key] != fingerprint.compilation_database_sha256) {
        differences.push_back("the compilation database " + compilation_database.string() +
                              " changed");
    }
    if (recorded[tool_version_key] != fingerprint.tool_version) {
        differences.push_back("it was started by lcart version " + recorded[tool_version_key] +
                              ", this is lcart version " + fingerprint.tool_version);
    }
    if (recorded[configuration_key] != fingerprint.configuration_sha256) {
        differences.emplace_back("the configuration changed (analysis root, module rules,"
                                 " exclusions or the translation units to analyse)");
    }
    if (differences.empty()) {
        return {};
    }
    std::string message = "cannot resume the interrupted analysis: ";
    for (std::size_t i = 0; i < differences.size(); ++i) {
        message += (i == 0 ? "" : "; ") + differences[i];
    }
    return std::unexpected(common::Error{.code = common::ErrorCode::incompatible_database,
                                         .message = message + "; start a new analysis"});
}

/// Reports progress (ADR-0019) on the writer thread. Its clock reaches
/// nothing but the reports (NFR-004).
class ProgressReporter {
public:
    explicit ProgressReporter(const AnalysisRequest& request) : request_(&request) {}

    /// Starts the clock of this run.
    void start(std::uint32_t total, std::uint32_t already_done) {
        total_ = total;
        already_done_ = already_done;
        started_ = now();
        last_report_ = started_;
    }

    void count(facts::TranslationUnitStatus status) {
        (status == facts::TranslationUnitStatus::parsed ? parsed_ : failed_) += 1;
    }

    /// Reports if the interval since the last report has passed.
    void report_if_due() {
        if (request_->on_progress && now() - last_report_ >= request_->progress_interval) {
            report(false);
        }
    }

    void report(bool finished) {
        if (!request_->on_progress) {
            return;
        }
        last_report_ = now();
        AnalysisProgress progress{
            .parsed = parsed_,
            .failed = failed_,
            .already_done = already_done_,
            .total = total_,
            .elapsed = std::chrono::duration_cast<milliseconds>(last_report_ - started_),
            .remaining = std::nullopt,
            .finished = finished};
        // Elapsed wall time of this run / units done in this run * units left.
        if (const std::uint32_t done = parsed_ + failed_; done > 0) {
            const std::uint32_t left = total_ - already_done_ - done;
            progress.remaining = milliseconds{progress.elapsed.count() * left / done};
        }
        request_->on_progress(progress);
    }

private:
    [[nodiscard]] steady_clock::time_point now() const {
        return request_->clock ? request_->clock() : steady_clock::now();
    }

    const AnalysisRequest* request_;
    std::uint32_t total_{};
    std::uint32_t already_done_{};
    std::uint32_t parsed_{};
    std::uint32_t failed_{};
    steady_clock::time_point started_;
    steady_clock::time_point last_report_;
};

/// State shared by the writer (the calling thread) and the workers, guarded
/// by `mutex`.
struct Pipeline {
    std::mutex mutex;
    std::condition_variable work_ready;  ///< A job was queued, or the workers are to end.
    std::condition_variable batch_ready; ///< A worker returned a batch.
    std::deque<ParseJob> queue;          ///< In translation unit order.
    /// Returned batches the writer does not need yet: at most 2 x jobs
    /// (ADR-0019 dispatch window).
    std::map<std::size_t, TranslationUnitBatch> finished;
    std::size_t busy{}; ///< Jobs taken by workers and not returned yet.
    bool closing{};
    /// Set by a worker whose parse threw: the run ends with this error.
    std::optional<common::Error> failure;
};

/// Closes the pipeline when it goes out of scope, before the worker
/// threads declared ahead of it are joined, however the writer leaves:
/// workers waiting for a job would otherwise never wake up.
class PipelineCloser {
public:
    explicit PipelineCloser(Pipeline& pipeline) : pipeline_(&pipeline) {}
    PipelineCloser(const PipelineCloser&) = delete;
    PipelineCloser& operator=(const PipelineCloser&) = delete;
    PipelineCloser(PipelineCloser&&) = delete;
    PipelineCloser& operator=(PipelineCloser&&) = delete;
    ~PipelineCloser() {
        {
            const std::lock_guard lock{pipeline_->mutex};
            pipeline_->closing = true;
            pipeline_->queue.clear();
        }
        pipeline_->work_ready.notify_all();
    }

private:
    Pipeline* pipeline_;
};

/// Parses `job`; an exception (from Clang or the allocator) becomes an
/// error instead of ending the process from a worker thread.
std::expected<TranslationUnitBatch, common::Error> record_safely(TranslationUnitRecorder& recorder,
                                                                 const ParseJob& job) {
    try {
        return recorder.record(job);
    } catch (const std::exception& error) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::io_error,
                          .message = "parsing " + job.file + " failed: " + error.what()});
    }
}

/// A worker: takes jobs in order until the pipeline closes, and takes none
/// once a stop is requested. It never blocks while holding a batch.
void work(Pipeline& pipeline, TranslationUnitRecorder& recorder, const std::atomic<bool>* stop) {
    while (true) {
        ParseJob job;
        {
            std::unique_lock lock{pipeline.mutex};
            pipeline.work_ready.wait(lock, [&pipeline, stop] {
                return pipeline.closing || (!pipeline.queue.empty() && !stop_requested(stop));
            });
            if (pipeline.closing) {
                return;
            }
            job = std::move(pipeline.queue.front());
            pipeline.queue.pop_front();
            ++pipeline.busy;
        }
        auto batch = record_safely(recorder, job);
        {
            const std::lock_guard lock{pipeline.mutex};
            --pipeline.busy;
            if (batch) {
                pipeline.finished.emplace(job.index, std::move(*batch));
            } else if (!pipeline.failure) {
                pipeline.failure = std::move(batch.error());
            }
        }
        pipeline.batch_ready.notify_one();
    }
}

/// Starts one worker thread per recorder. If the system runs out of
/// threads (e.g. a low process limit), goes on with those that started:
/// the facts do not depend on their number; fails only if none started.
common::Status start_workers(Pipeline& pipeline, std::vector<TranslationUnitRecorder>& recorders,
                             std::vector<std::jthread>& threads, const std::atomic<bool>* stop) {
    for (TranslationUnitRecorder& recorder : recorders) {
        try {
            threads.emplace_back([&pipeline, &recorder, stop] { work(pipeline, recorder, stop); });
        } catch (const std::system_error& error) {
            if (threads.empty()) {
                return std::unexpected(common::Error{
                    .code = common::ErrorCode::io_error,
                    .message = std::string{"cannot start a worker thread: "} + error.what()});
            }
            break;
        }
    }
    return {};
}

/// What the writer found after waiting for the batch it needs next.
struct Awaited {
    std::optional<TranslationUnitBatch> batch; ///< The batch of the unit needed next.
    std::optional<common::Error> failure;      ///< A worker's parse threw.
    bool nothing_more{};                       ///< Stopping, and no worker is busy.
};

/// Waits up to poll_period for the batch of unit `next`, a worker failure,
/// or, when `stopping`, for the workers to be idle.
Awaited await_batch(Pipeline& pipeline, std::size_t next, bool stopping) {
    std::unique_lock lock{pipeline.mutex};
    const auto ready = [&pipeline, next, stopping] {
        return pipeline.finished.contains(next) || pipeline.failure ||
               (stopping && pipeline.busy == 0);
    };
    pipeline.batch_ready.wait_for(lock, poll_period, ready);
    Awaited awaited;
    if (pipeline.failure) {
        awaited.failure = pipeline.failure;
    } else if (const auto found = pipeline.finished.find(next); found != pipeline.finished.end()) {
        awaited.batch = std::move(found->second);
        pipeline.finished.erase(found);
    } else {
        awaited.nothing_more = stopping && pipeline.busy == 0;
    }
    return awaited;
}

/// One analysis: the translation units, the store and the writer.
class AnalysisRun {
public:
    AnalysisRun(const AnalysisRequest& request, facts::FactWriter& out, facts::AnalysisStore* store)
        : request_(&request), out_(&out), store_(store), writer_(request.module_rules, out),
          progress_(request) {}

    common::Result<AnalysisSummary> run();

private:
    [[nodiscard]] common::Status prepare();
    [[nodiscard]] common::Status start_or_resume();
    [[nodiscard]] common::Status commit(const TranslationUnitBatch& batch);
    [[nodiscard]] common::Status analyse_pending();
    [[nodiscard]] common::Status finish();

    const AnalysisRequest* request_;
    facts::FactWriter* out_;
    facts::AnalysisStore* store_; ///< Null: no transactions, no resume.
    BatchWriter writer_;
    std::unique_ptr<clang::tooling::CompilationDatabase> database_;
    std::filesystem::path root_;
    std::vector<std::string> root_prefixes_;
    std::vector<std::string> files_; ///< As the compilation database names them, sorted.
    std::vector<std::string> paths_; ///< The same, spelled like file facts.
    std::size_t first_pending_{};
    std::size_t next_{}; ///< The index the writer needs next.
    std::vector<facts::TranslationUnitStatus> statuses_;
    ProgressReporter progress_;
    AnalysisSummary summary_;
};

common::Status AnalysisRun::prepare() {
    if (request_->jobs < 1 || request_->jobs > max_jobs) {
        return std::unexpected(invalid("jobs must be from 1 to " + std::to_string(max_jobs) +
                                       ", not " + std::to_string(request_->jobs)));
    }
    auto database = load_compilation_database(request_->compilation_database);
    if (!database) {
        return std::unexpected(database.error());
    }
    database_ = std::move(*database);
    files_ = database_->getAllFiles();
    std::ranges::sort(files_); // deterministic order (NFR-004)

    std::error_code ec;
    if (!request_->root.empty() && std::filesystem::is_directory(request_->root, ec)) {
        const std::filesystem::path absolute = std::filesystem::absolute(request_->root, ec);
        if (!ec) {
            root_ = std::filesystem::weakly_canonical(absolute, ec);
        }
    }
    if (root_.empty() || ec) {
        return std::unexpected(
            invalid("analysis root is not a directory: " + request_->root.string()));
    }
    std::erase_if(files_, [this](const std::string& file) {
        return is_excluded(file, root_, request_->exclude_globs);
    });
    root_prefixes_ = message_root_prefixes(request_->root, root_);
    // Absolute before any worker starts: nothing below depends on the
    // working directory (ADR-0019).
    // Entries that name one file (through a symbolic link, or spelled
    // differently) are one translation unit, the first in sorted order: the
    // database records a unit once per fact path, so the total, the
    // configuration hash and resume all use this list.
    std::vector<std::string> unique_files;
    std::set<std::string, std::less<>> seen;
    for (const std::string& file : files_) {
        const std::filesystem::path absolute = std::filesystem::absolute(file, ec);
        std::string name = ec ? file : absolute.string();
        std::string path = fact_path_of(name, root_).path;
        if (seen.insert(path).second) {
            unique_files.push_back(std::move(name));
            paths_.push_back(std::move(path));
        }
    }
    files_ = std::move(unique_files);
    statuses_.assign(files_.size(), facts::TranslationUnitStatus::pending);
    return {};
}

common::Status AnalysisRun::start_or_resume() {
    if (store_ == nullptr) {
        if (request_->resume) {
            return std::unexpected(invalid("resuming an analysis needs its fact database"));
        }
        return {};
    }
    auto fingerprint = fingerprint_of(*request_, root_, files_);
    if (!fingerprint) {
        return std::unexpected(fingerprint.error());
    }
    if (!request_->resume) {
        // One transaction, so a crash leaves either a resumable database
        // or one that resume refuses as holding no analysis.
        const auto started = [&]() -> common::Status {
            if (auto begun = store_->begin_transaction(); !begun) {
                return begun;
            }
            if (auto added = store_->add_translation_units(paths_); !added) {
                return added;
            }
            if (auto recorded = record_fingerprint(*store_, *fingerprint); !recorded) {
                return recorded;
            }
            return store_->commit_transaction();
        }();
        if (!started) {
            store_->rollback_transaction();
        }
        return started;
    }

    if (auto same = check_fingerprint(*store_, *fingerprint, request_->compilation_database);
        !same) {
        return same;
    }
    auto units = store_->translation_units();
    if (!units) {
        return std::unexpected(units.error());
    }
    const bool same_units = std::ranges::equal(*units, paths_, std::ranges::equal_to{},
                                               &facts::TranslationUnitFact::path);
    // Units are committed in order, so the done ones are a prefix.
    const auto first_pending = std::ranges::find(*units, facts::TranslationUnitStatus::pending,
                                                 &facts::TranslationUnitFact::status);
    if (!same_units ||
        std::any_of(first_pending, units->end(), [](const facts::TranslationUnitFact& unit) {
            return unit.status != facts::TranslationUnitStatus::pending;
        })) {
        return std::unexpected(common::Error{
            .code = common::ErrorCode::incompatible_database,
            .message = "cannot resume: the database does not match the analysis it records;"
                       " start a new analysis"});
    }
    first_pending_ = static_cast<std::size_t>(first_pending - units->begin());
    for (std::size_t i = 0; i < first_pending_; ++i) {
        statuses_[i] = (*units)[i].status;
    }
    // The writer continues from the state the earlier run left behind.
    return writer_.restore(*store_);
}

common::Status AnalysisRun::commit(const TranslationUnitBatch& batch) {
    const facts::TranslationUnitStatus status =
        batch.parsed ? facts::TranslationUnitStatus::parsed : facts::TranslationUnitStatus::failed;
    // Handed out only once the unit is committed: an uncommitted unit is
    // parsed again, with its warnings, on resume.
    std::vector<std::string> warnings;
    // One transaction per translation unit, with its status (ADR-0019).
    const auto applied = [&]() -> common::Status {
        if (store_ != nullptr) {
            if (auto begun = store_->begin_transaction(); !begun) {
                return begun;
            }
        }
        auto diagnostics = writer_.apply(batch, warnings);
        if (!diagnostics) {
            return std::unexpected(diagnostics.error());
        }
        summary_.diagnostics += *diagnostics;
        if (store_ != nullptr) {
            if (auto set = store_->set_translation_unit_status(paths_[batch.index], status); !set) {
                return set;
            }
            return store_->commit_transaction();
        }
        return {};
    }();
    if (!applied) {
        if (store_ != nullptr) {
            store_->rollback_transaction();
        }
        return applied;
    }
    statuses_[batch.index] = status;
    progress_.count(status);
    for (std::string& warning : warnings) {
        if (request_->on_warning) {
            request_->on_warning(warning);
        }
        summary_.warnings.push_back(std::move(warning));
    }
    return {};
}

common::Status AnalysisRun::analyse_pending() {
    const std::size_t total = files_.size();
    next_ = first_pending_;
    if (next_ == total) {
        return {};
    }
    const std::size_t jobs = request_->jobs;
    const std::size_t window = 2 * jobs;
    Pipeline pipeline;
    std::vector<TranslationUnitRecorder> recorders;
    const std::size_t workers = std::min(jobs, total - next_);
    recorders.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        recorders.emplace_back(root_, root_prefixes_);
    }
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    // Declared after `threads`, so destroyed first: closes the pipeline
    // before the threads are joined, also when an exception leaves here.
    const PipelineCloser closer{pipeline};
    if (auto started = start_workers(pipeline, recorders, threads, request_->stop); !started) {
        return started;
    }

    common::Status status;
    std::size_t dispatched = next_;
    bool stopping = false;
    while (next_ < total) {
        if (!stopping && stop_requested(request_->stop)) {
            // Queued units are parsed again on resume.
            stopping = true;
            {
                const std::lock_guard lock{pipeline.mutex};
                pipeline.queue.clear();
            }
            pipeline.work_ready.notify_all();
        }
        // Dispatch window: a unit is queued only while it is less than
        // 2 x jobs ahead of the one the writer needs (ADR-0019).
        for (; !stopping && dispatched < total && dispatched < next_ + window; ++dispatched) {
            ParseJob job{.index = dispatched,
                         .file = files_[dispatched],
                         .translation_unit = paths_[dispatched],
                         .commands = database_->getCompileCommands(files_[dispatched])};
            {
                const std::lock_guard lock{pipeline.mutex};
                pipeline.queue.push_back(std::move(job));
            }
            pipeline.work_ready.notify_one();
        }

        Awaited awaited = await_batch(pipeline, next_, stopping);
        if (awaited.failure) {
            status = std::unexpected(std::move(*awaited.failure));
            break;
        }
        if (awaited.batch) {
            status = commit(*awaited.batch);
            if (!status) {
                break;
            }
            ++next_;
        } else if (awaited.nothing_more) {
            // Interrupted: later batches are discarded and parsed on resume.
            break;
        }
        progress_.report_if_due();
    }
    // The closer ends the workers; the jthreads join when this returns,
    // after the workers' last parses.
    summary_.interrupted = status && next_ < total;
    return status;
}

common::Status AnalysisRun::finish() {
    summary_.translation_units_total = static_cast<std::uint32_t>(files_.size());
    std::vector<facts::TranslationUnitStatus> statuses = statuses_;
    if (store_ != nullptr) {
        // Coverage comes from the database, which also holds the units of
        // an earlier, interrupted run (ADR-0019).
        auto units = store_->translation_units();
        if (!units) {
            return std::unexpected(units.error());
        }
        statuses.clear();
        std::ranges::transform(*units, std::back_inserter(statuses),
                               &facts::TranslationUnitFact::status);
        summary_.translation_units_total = static_cast<std::uint32_t>(statuses.size());
    }
    summary_.translation_units_parsed = static_cast<std::uint32_t>(
        std::ranges::count(statuses, facts::TranslationUnitStatus::parsed));
    summary_.translation_units_failed = static_cast<std::uint32_t>(
        std::ranges::count(statuses, facts::TranslationUnitStatus::failed));
    if (summary_.interrupted) {
        return {};
    }
    // Derived once, after the last unit (ADR-0019).
    if (auto derived = out_->derive_module_dependencies(); !derived) {
        return derived;
    }
    return out_->set_coverage({.translation_units_total = summary_.translation_units_total,
                               .translation_units_parsed = summary_.translation_units_parsed});
}

common::Result<AnalysisSummary> AnalysisRun::run() {
    if (auto prepared = prepare(); !prepared) {
        return std::unexpected(prepared.error());
    }
    if (auto started = start_or_resume(); !started) {
        return std::unexpected(started.error());
    }
    summary_.translation_units_already_done = static_cast<std::uint32_t>(first_pending_);
    progress_.start(static_cast<std::uint32_t>(files_.size()),
                    summary_.translation_units_already_done);
    if (auto analysed = analyse_pending(); !analysed) {
        return std::unexpected(analysed.error());
    }
    if (auto finished = finish(); !finished) {
        return std::unexpected(finished.error());
    }
    progress_.report(true);
    return std::move(summary_);
}

} // namespace

common::Result<AnalysisSummary> ClangAnalyzer::analyze(const AnalysisRequest& request,
                                                       facts::FactWriter& out) {
    return AnalysisRun{request, out, nullptr}.run();
}

common::Result<AnalysisSummary> ClangAnalyzer::analyze(const AnalysisRequest& request,
                                                       facts::AnalysisStore& out) {
    return AnalysisRun{request, out, &out}.run();
}

} // namespace lcart::analysis
