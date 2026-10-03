#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lcart::claims {

/// Identifier of the claim JSON Schema this model corresponds to
/// (docs/specs/claim-schema.md).
inline constexpr std::string_view claim_schema_id = "lcart:claim:v1";

/// Kinds of structural statements that can be checked against the fact
/// database. Anything else the LLM writes is prose and is not verified.
enum class ClaimKind : std::uint8_t {
    calls,      ///< function -> function, direct call
    includes,   ///< file -> file, direct #include
    depends_on, ///< module -> module
    defines,    ///< file -> function definition
    belongs_to, ///< file or function -> module
};

enum class EntityType : std::uint8_t { function, file, module };

/// `negated`: the claim states that the relation does NOT hold
/// ("modbus-tcp does not depend on modbus-rtu").
enum class Polarity : std::uint8_t { affirmed, negated };

struct EntityRef {
    EntityType type{EntityType::function};
    std::string name;                ///< Function name, file path or module name.
    std::optional<std::string> file; ///< Disambiguates functions with the same name.
};

struct ClaimSource {
    std::string document; ///< Generated document the claim belongs to.
    std::string section;
};

struct Claim {
    std::string id;
    ClaimKind kind{ClaimKind::calls};
    EntityRef subject;
    EntityRef object;
    Polarity polarity{Polarity::affirmed};
    ClaimSource source;
    std::string text; ///< The sentence in the generated prose that states the claim.
};

} // namespace lcart::claims
