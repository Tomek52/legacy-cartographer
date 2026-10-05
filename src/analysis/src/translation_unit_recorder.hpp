#pragma once

#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Tooling/CompilationDatabase.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fact_path.hpp"
#include "translation_unit_batch.hpp"

namespace lcart::analysis {

/// One translation unit to parse, prepared by the main thread: the worker
/// never calls into the shared compilation database (ADR-0019).
struct ParseJob {
    std::size_t index{};
    std::string file;             ///< As the compilation database names it (absolute).
    std::string translation_unit; ///< Spelled like a file fact.
    /// With response files expanded.
    std::vector<clang::tooling::CompileCommand> commands;
};

/// What one worker knows about files across its translation units: how they
/// are named in the facts and the hash of their content, so a header is
/// hashed once per worker, not once per translation unit.
class WorkerFiles {
public:
    explicit WorkerFiles(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] const FactPath& fact_path(const std::string& absolute_name);
    /// The SHA-256 of `file`'s content as `sources` has it, lowercase hex;
    /// empty if it has none.
    [[nodiscard]] const std::optional<std::string>& content_sha256(const std::string& absolute_name,
                                                                   clang::SourceManager& sources,
                                                                   clang::FileEntryRef file);

private:
    struct Known {
        FactPath path;
        bool hashed{};
        std::optional<std::string> content_sha256;
    };
    Known& known(const std::string& absolute_name);

    std::filesystem::path root_;
    /// Clang's absolute file name -> what is known about it.
    std::map<std::string, Known> known_;
};

/// Parses translation units on one worker thread, each with its own
/// ClangTool on its own physical file system (its own working directory),
/// and records them as batches. Not shared between threads.
class TranslationUnitRecorder {
public:
    /// `root` must be absolute and canonical; `root_prefixes` (each ending
    /// in `/`) are removed from diagnostic messages.
    TranslationUnitRecorder(std::filesystem::path root, std::vector<std::string> root_prefixes);

    /// Parses `job` and returns what it contributes to the facts. Clang's
    /// ASTs are freed before this returns (ADR-0019 memory).
    [[nodiscard]] TranslationUnitBatch record(const ParseJob& job);

private:
    WorkerFiles files_;
    std::vector<std::string> root_prefixes_;
};

} // namespace lcart::analysis
