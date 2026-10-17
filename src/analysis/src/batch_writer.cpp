#include "batch_writer.hpp"

#include <iterator>
#include <optional>
#include <utility>
#include <variant>

namespace lcart::analysis {

namespace {

template <typename... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

} // namespace

BatchWriter::BatchWriter(ModuleRules rules, facts::FactWriter& out)
    : out_(&out), files_(std::move(rules), out), functions_(files_, out),
      calls_(files_, functions_, out) {}

common::Status BatchWriter::restore(const facts::FactQuery& facts) {
    if (auto restored = files_.restore(facts); !restored) {
        return restored;
    }
    return functions_.restore(facts);
}

void BatchWriter::apply(const AstEvent& ast, const TranslationUnitBatch& batch) {
    functions_.begin_translation_unit();
    // Functions first, then calls: a call may name a function declared
    // later in the translation unit, and every callee needs its id.
    std::vector<std::pair<const FunctionEvent*, facts::FunctionId>> written;
    for (const FunctionEvent& function : ast.functions) {
        if (const auto id = functions_.on_function(function, batch.files[function.file])) {
            written.emplace_back(&function, *id);
        }
    }
    for (const auto& [definition, id] : written) {
        calls_.on_definition(*definition, batch.files, id);
    }
    calls_.on_field_assignments(ast.field_assignments);
}

common::Result<std::uint32_t> BatchWriter::apply(const TranslationUnitBatch& batch,
                                                 std::vector<std::string>& warnings) {
    warnings.insert(warnings.end(), batch.warnings.begin(), batch.warnings.end());
    for (const BatchEvent& event : batch.events) {
        std::visit(Overloaded{
                       [this, &batch](const MainFileEvent& main) {
                           files_.on_main_file(batch.files[main.file]);
                       },
                       [this, &batch](const IncludeEvent& include) {
                           files_.on_include(include, batch.files);
                       },
                       [this, &batch](const AstEvent& ast) { apply(ast, batch); },
                   },
                   event);
    }
    for (const common::Status& collected :
         {files_.status(), functions_.status(), calls_.status()}) {
        if (!collected) {
            return std::unexpected(collected.error());
        }
    }
    std::ranges::move(functions_.take_warnings(), std::back_inserter(warnings));
    std::uint32_t diagnostics = 0;
    for (const facts::DiagnosticFact& diagnostic : batch.diagnostics) {
        if (auto written = out_->add_diagnostic(diagnostic); !written) {
            return std::unexpected(written.error());
        }
        ++diagnostics;
    }
    return diagnostics;
}

} // namespace lcart::analysis
