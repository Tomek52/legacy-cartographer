#pragma once

#include "analysis/module_rules.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>

#include "translation_unit_batch.hpp"

namespace lcart::analysis {

/// Turns the files and `#include` directives of translation unit batches
/// into file and include facts (FR-002). Runs on the writer only: one
/// collector serves every translation unit of an analysis, so a directive in
/// a header reached from several translation units is recorded once.
///
/// Files under the root get a module. Files outside it are recorded only as
/// targets of a directive in a file under the root, with their absolute path
/// and no module; directives inside them are not recorded
/// (docs/architecture/data-model.md).
///
/// Not thread-safe, and ids follow the order in which files are first
/// reached, so batches must be replayed in translation unit order (NFR-004,
/// ADR-0019).
class FileFactCollector {
public:
    /// A file under the root belongs to the module module_of() gives for
    /// its root-relative path.
    FileFactCollector(ModuleRules rules, facts::FactWriter& out);

    /// Takes over the files and directives already in `facts`, written by
    /// an earlier run of the same analysis (ADR-0019 resume).
    [[nodiscard]] common::Status restore(const facts::FactQuery& facts);

    /// Records the main file of a translation unit.
    void on_main_file(const BatchFile& file);
    void on_include(const IncludeEvent& include, std::span<const BatchFile> files);

    /// The id of `file` if it is under the root, recorded if it was not yet;
    /// empty for an external file or once status() holds an error.
    [[nodiscard]] std::optional<facts::FileId> internal_file(const BatchFile& file);

    /// The first error from the fact writer; later events are ignored once
    /// this is set.
    [[nodiscard]] common::Status status() const { return status_; }

private:
    [[nodiscard]] common::Result<facts::FileId> record_file(const BatchFile& file);

    ModuleRules rules_;
    facts::FactWriter* out_;
    common::Status status_;
    /// Path as stored in the facts -> id.
    std::map<std::string, facts::FileId> file_ids_;
    /// (includer, line, spelling, included or -1) of directives already written.
    std::set<std::tuple<std::int64_t, std::uint32_t, std::string, std::int64_t>> includes_;
};

} // namespace lcart::analysis
