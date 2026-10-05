#include "call_fact_collector.hpp"

#include <optional>

namespace lcart::analysis {

CallFactCollector::CallFactCollector(FileFactCollector& files,
                                     const FunctionFactCollector& functions, facts::FactWriter& out)
    : files_(&files), functions_(&functions), out_(&out) {}

void CallFactCollector::on_definition(const FunctionEvent& definition,
                                      std::span<const BatchFile> files, facts::FunctionId caller) {
    for (const CallEvent& call : definition.calls) {
        if (!status_) {
            return;
        }
        facts::CallFact fact{.caller = caller,
                             .callee = std::nullopt,
                             .kind = facts::CallKind::indirect,
                             .via = {},
                             .site = {}};
        if (call.callee) {
            fact.callee = functions_->id_of(*call.callee);
            if (!fact.callee) {
                continue; // declared outside the root, or not recorded: not a function fact
            }
            fact.kind = facts::CallKind::direct;
        } else {
            fact.via = call.via;
        }
        const std::optional<facts::FileId> file_id = files_->internal_file(files[call.file]);
        if (!file_id) {
            status_ = files_->status();
            continue;
        }
        fact.site = {.file = *file_id, .line = call.line, .column = call.column};
        if (auto written = out_->add_call(fact); !written) {
            status_ = written;
        }
    }
}

} // namespace lcart::analysis
