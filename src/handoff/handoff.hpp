#pragma once

#include "claims/claim.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lcart::handoff {

/// Options for `lcart handoff` (FR-008, NFR-002). The bundle layout and the
/// prompt are specified in docs/specs/handoff-contract.md.
struct BundleOptions {
    std::filesystem::path output_dir;
    /// Modules to document; empty means all.
    std::vector<std::string> modules;
    /// Copy source excerpts into the bundle. Off by default: the bundle then
    /// holds facts only (names, paths, edges), never code or comments.
    bool include_source{false};
};

/// What a bundle contains; also written to the bundle as MANIFEST.json so
/// the user can review it before giving the bundle to an agent.
struct BundleManifest {
    std::vector<std::filesystem::path> files_written;
    std::vector<std::string> source_files_included; ///< Empty unless include_source.
};

/// Writes a handoff bundle for an external coding agent. Deterministic:
/// the same facts and options produce byte-identical files (NFR-004).
/// Makes no network requests.
// TODO(M2): implement per docs/specs/handoff-contract.md.
[[nodiscard]] common::Result<BundleManifest> write_bundle(const facts::FactQuery& facts,
                                                          const BundleOptions& options);

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
