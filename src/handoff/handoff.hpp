#pragma once

#include "claims/claim.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::handoff {

/// Version of the handoff contract (docs/specs/handoff-contract.md),
/// recorded in every bundle's MANIFEST.json. A change to the prompt or the
/// agent output on that page bumps it (ADR-0028).
inline constexpr int contract_version = 1;

/// Options for `lcart handoff` (FR-008, NFR-002). The bundle layout and the
/// prompt are specified in docs/specs/handoff-contract.md.
struct BundleOptions {
    /// Written into PROMPT.md and facts/project.json; see project_name().
    std::string project_name{};
    /// Modules to document; empty means all.
    std::vector<std::string> modules{};
    /// Copy source excerpts into the bundle. Off by default: the bundle then
    /// holds facts only (names, paths, edges), never code or comments.
    /// Not available until B-31: build_bundle() refuses it.
    bool include_source{false};
};

/// A bundle in memory, before it is written.
struct Bundle {
    /// First 12 hexadecimal digits of the SHA-256 of the facts export and
    /// the options (ADR-0028); names the bundle directory.
    std::string run_id;
    /// Every file, MANIFEST.json included: path relative to the bundle
    /// directory (`/` separators) to content.
    std::map<std::string, std::string> files;
};

/// What write_bundle() wrote; MANIFEST.json in the bundle says the same
/// for the user to review before giving the bundle to an agent.
struct BundleManifest {
    std::string run_id;
    std::filesystem::path directory; ///< The bundle directory.
    /// Relative to `directory`, sorted.
    std::vector<std::filesystem::path> files_written;
    std::vector<std::string> source_files_included; ///< Empty unless include_source.
};

/// The project name for a bundle (ADR-0028): the last path component of
/// `analysis_root` (as stored in the fact database), or `project` if it
/// has none.
[[nodiscard]] std::string project_name(std::string_view analysis_root);

/// Builds a facts-only handoff bundle from `facts`, as the handoff contract
/// specifies: no source code, comments, string literals, diagnostics or
/// absolute paths. Deterministic: the same facts and options give the same
/// bytes (NFR-004). Makes no network requests and writes nothing.
///
/// Errors: ErrorCode::not_found if a module in `options.modules` is not in
/// the database; ErrorCode::invalid_argument if a module is named twice or
/// `options.include_source` is set; the database's errors.
[[nodiscard]] common::Result<Bundle> build_bundle(const facts::FactQuery& facts,
                                                  const BundleOptions& options);

/// The directory write_bundle() writes `run_id`'s bundle to:
/// `output_dir/handoff/<run_id>`.
[[nodiscard]] std::filesystem::path bundle_directory(const std::filesystem::path& output_dir,
                                                     std::string_view run_id);

/// Writes `bundle` to bundle_directory(output_dir, bundle.run_id), creating
/// the directories it needs. The files go to a hidden directory next to it
/// first, which is then renamed into place. An existing directory there is
/// replaced only if it holds nothing but bundle files (lcart replaces only
/// its own files, ADR-0028); otherwise ErrorCode::io_error, as for any
/// file that cannot be written.
[[nodiscard]] common::Result<BundleManifest> write_bundle(const Bundle& bundle,
                                                          const std::filesystem::path& output_dir);

/// Documentation and claims an agent produced from a bundle.
struct AgentOutput {
    std::vector<std::filesystem::path> documents; ///< Markdown files.
    std::vector<claims::Claim> claims;
};

/// Reads and validates an agent's output directory (FR-008, FR-009): every
/// required file present, claims valid against the schema, every claim's
/// `source.document` naming a produced document. The directory content is
/// untrusted input (docs/security.md).
// TODO(M2): implement.
[[nodiscard]] common::Result<AgentOutput> read_agent_output(const std::filesystem::path& dir);

} // namespace lcart::handoff
