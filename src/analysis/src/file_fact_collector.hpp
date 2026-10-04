#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/StringRef.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>

namespace lcart::analysis {

/// Turns the files and `#include` directives Clang sees into file and
/// include facts (FR-002). One collector serves every translation unit of an
/// analysis, so each file is hashed once and a directive in a header reached
/// from several translation units is recorded once.
///
/// Files under the root get a root-relative path and a module. Files outside
/// it are recorded only as targets of a directive in a file under the root,
/// with their absolute path and no module; directives inside them are not
/// recorded (docs/architecture/data-model.md).
///
/// Not thread-safe, and ids follow the order in which files are first
/// reached: parallel workers (B-16) must not share one collector, or ids
/// would depend on scheduling (NFR-004).
class FileFactCollector {
public:
    /// `root` must be absolute and canonical.
    FileFactCollector(std::filesystem::path root, facts::FactWriter& out);

    /// Records the main file of a translation unit.
    void on_main_file(clang::SourceManager& sources);
    void on_include(clang::SourceManager& sources, clang::SourceLocation hash_location,
                    llvm::StringRef spelled, bool is_angled, clang::OptionalFileEntryRef file);

    /// The first error from the fact writer; Clang's callbacks cannot return
    /// one, so later callbacks are ignored once this is set.
    [[nodiscard]] common::Status status() const { return status_; }

private:
    struct FactPath {
        std::string path;
        bool is_external{};
    };

    [[nodiscard]] const FactPath& fact_path(const clang::SourceManager& sources,
                                            clang::FileEntryRef file);
    [[nodiscard]] common::Result<facts::FileId>
    record_file(clang::SourceManager& sources, clang::FileEntryRef file, const FactPath& path);

    std::filesystem::path root_;
    facts::FactWriter* out_;
    common::Status status_;
    /// Clang's absolute file name -> path as stored in the facts.
    std::map<std::string, FactPath> fact_paths_;
    std::map<std::string, facts::FileId> file_ids_;
    /// (includer, line, spelling, included or -1) of directives already written.
    std::set<std::tuple<std::int64_t, std::uint32_t, std::string, std::int64_t>> includes_;
};

} // namespace lcart::analysis
