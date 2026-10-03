#pragma once

#include "llm/provider.hpp"

namespace lcart::llm {

/// User consent for sending source code off the machine (NFR-002, ADR-0004).
struct EgressPolicy {
    /// Set only from the `--allow-remote-source` command-line flag of the
    /// current invocation, never from a configuration file, so consent
    /// cannot be given once and forgotten.
    bool allow_remote_source{false};
};

/// True if `request` may be sent to a provider with `locality` under
/// `policy`. Requests to local providers are always allowed; requests to
/// remote providers only when they contain no source code or the policy
/// allows it.
// TODO(M2): implement, with exhaustive tests of the decision table.
[[nodiscard]] bool may_send(const EgressPolicy& policy, Locality locality,
                            const CompletionRequest& request) noexcept;

} // namespace lcart::llm
