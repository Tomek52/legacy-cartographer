#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lcart::llm {

enum class ProviderKind : std::uint8_t {
    none, ///< No LLM: static analysis only (NFR-001). The default.
    openai_compatible,
    anthropic,
    ollama,
};

/// Where a request is processed. A provider is `local` only if its endpoint
/// is a loopback address; an Ollama server on another host is `remote`
/// (ADR-0004).
enum class Locality : std::uint8_t { local, remote };

enum class Role : std::uint8_t { system, user, assistant };

struct Message {
    Role role{Role::user};
    std::string content;
};

struct CompletionRequest {
    std::vector<Message> messages;
    /// When non-empty, the provider requests structured output conforming
    /// to this JSON Schema (claims, FR-009).
    std::string json_schema;
    /// Set by the prompt builder whenever any message contains text taken
    /// from the analysed code base (code, comments, identifiers in context).
    bool contains_source_code{true};
};

struct CompletionResponse {
    std::string text;
    std::string model;
};

/// Provider-neutral LLM interface for built-in providers (ADR-0004, deferred
/// to M6 by ADR-0006; v1 uses an external agent through `handoff`). Implementations do no egress
/// checks themselves: callers go through the egress guard, which is the
/// single place that decides whether a request may leave the machine.
// TODO(M6): implementations for OpenAI-compatible, Anthropic and Ollama APIs (ADR-0006).
class LlmProvider {
public:
    LlmProvider() = default;
    LlmProvider(const LlmProvider&) = delete;
    LlmProvider& operator=(const LlmProvider&) = delete;
    LlmProvider(LlmProvider&&) = delete;
    LlmProvider& operator=(LlmProvider&&) = delete;
    virtual ~LlmProvider() = default;

    [[nodiscard]] virtual ProviderKind kind() const noexcept = 0;
    [[nodiscard]] virtual Locality locality() const noexcept = 0;
    virtual common::Result<CompletionResponse> complete(const CompletionRequest& request) = 0;
};

} // namespace lcart::llm
