#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "file_fact_collector.hpp"
#include "translation_unit_batch.hpp"

namespace lcart::analysis {

/// Turns function events of translation unit batches into function facts
/// (FR-003). Like FileFactCollector, one collector on the writer serves
/// every translation unit, so a function declared in a header is written
/// once, and once more when its definition is first seen. Only functions in
/// files under the root are recorded (docs/architecture/data-model.md). A
/// second definition of a function with external linkage is a function of
/// its own and a warning (ADR-0013).
///
/// Not thread-safe, and which definition is first depends on the order of
/// the batches: replay them in translation unit order (ADR-0019).
class FunctionFactCollector {
public:
    FunctionFactCollector(FileFactCollector& files, facts::FactWriter& out);

    /// Takes over the functions already in `facts`, written by an earlier run
    /// of the same analysis (ADR-0019 resume): which USRs are recorded,
    /// with or without a definition and where.
    [[nodiscard]] common::Status restore(const facts::FactQuery& facts);

    /// Forgets the declarations of the previous AST; call before the first
    /// on_function() of each AstEvent.
    void begin_translation_unit();

    /// Call for every function event of the AST, in order; `file` is the
    /// event's file. Returns the function's id if the event is a definition
    /// written now: the calls in its body are to be recorded under that id
    /// (once per function, and per definition, ADR-0013).
    [[nodiscard]] std::optional<facts::FunctionId> on_function(const FunctionEvent& function,
                                                               const BatchFile& file);

    /// The recorded function that a call in the current AST names through
    /// the batch-local reference `canonical`, if any: the last definition in
    /// this translation unit, else the function its USR names (ADR-0013).
    [[nodiscard]] std::optional<facts::FunctionId> id_of(std::uint32_t canonical) const;

    /// Warnings since the last call, in the order they arose.
    [[nodiscard]] std::vector<std::string> take_warnings() { return std::exchange(warnings_, {}); }

    /// The first error from the fact writer, as FileFactCollector::status().
    [[nodiscard]] common::Status status() const { return status_; }

private:
    struct Recorded {
        facts::FunctionId id;
        bool has_definition;
        /// Where the definition is, once written: root-relative path, line.
        std::string definition_file;
        std::uint32_t definition_line;
    };

    void remember(std::uint32_t canonical, facts::FunctionId id, bool is_definition);

    FileFactCollector* files_;
    facts::FactWriter* out_;
    common::Status status_;
    std::vector<std::string> warnings_;
    /// USR as stored -> the function.
    std::map<std::string, Recorded> recorded_;
    /// Function of the current AST (batch-local reference) -> its id.
    std::map<std::uint32_t, facts::FunctionId> in_translation_unit_;
};

} // namespace lcart::analysis
