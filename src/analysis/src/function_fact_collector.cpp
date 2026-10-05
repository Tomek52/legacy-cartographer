#include "function_fact_collector.hpp"

#include <optional>

namespace lcart::analysis {

FunctionFactCollector::FunctionFactCollector(FileFactCollector& files, facts::FactWriter& out)
    : files_(&files), out_(&out) {}

common::Status FunctionFactCollector::restore(const facts::FactQuery& facts) {
    auto files = facts.files();
    if (!files) {
        return std::unexpected(files.error());
    }
    std::map<facts::FileId, std::string> paths;
    for (const facts::FileFact& file : *files) {
        paths.emplace(file.id, file.path);
    }
    auto functions = facts.functions();
    if (!functions) {
        return std::unexpected(functions.error());
    }
    // A function row records what on_function() remembered when it wrote
    // it: the definition's file and first line once there is one.
    for (const facts::FunctionFact& function : *functions) {
        const auto path = paths.find(function.file);
        recorded_.insert_or_assign(
            function.usr,
            Recorded{.id = function.id,
                     .has_definition = function.is_definition,
                     .definition_file = function.is_definition && path != paths.end()
                                            ? path->second
                                            : std::string{},
                     .definition_line = function.is_definition ? function.line_begin : 0});
    }
    return {};
}

void FunctionFactCollector::begin_translation_unit() {
    in_translation_unit_.clear();
}

std::optional<facts::FunctionId> FunctionFactCollector::id_of(std::uint32_t canonical) const {
    const auto found = in_translation_unit_.find(canonical);
    if (found == in_translation_unit_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void FunctionFactCollector::remember(std::uint32_t canonical, facts::FunctionId id,
                                     bool is_definition) {
    // A definition decides which function the TU's calls name; a later
    // redeclaration does not change it (ADR-0013).
    if (is_definition) {
        in_translation_unit_.insert_or_assign(canonical, id);
    } else {
        in_translation_unit_.emplace(canonical, id);
    }
}

std::optional<facts::FunctionId> FunctionFactCollector::on_function(const FunctionEvent& function,
                                                                    const BatchFile& file) {
    if (!status_) {
        return std::nullopt;
    }
    const std::optional<facts::FileId> file_id = files_->internal_file(file);
    if (!file_id) {
        status_ = files_->status();
        return std::nullopt;
    }
    if (!function.clang_usr) {
        return std::nullopt;
    }
    const bool is_internal = function.is_internal;
    std::string usr = *function.clang_usr;
    if (is_internal) {
        // Clang's USR for internal linkage names only the base name of the
        // file of the first declaration (`c:util.c@F@helper`), so `static`
        // functions in a/util.c and b/util.c, or two definitions of a
        // `static` declared in one shared header, would be one function
        // (FR-003 criterion 1). Prefix the path of the file of this TU's
        // definition, or of the first declaration if the TU has none
        // (ADR-0012); the worker found that file.
        usr.insert(0, function.internal_prefix + "#");
    }

    const bool is_definition = function.is_definition;
    const std::uint32_t line_begin = function.line_begin;
    const std::string& path = file.path;

    auto known = recorded_.find(usr);
    if (!is_internal && known != recorded_.end() && known->second.has_definition && is_definition &&
        (known->second.definition_file != path || known->second.definition_line != line_begin)) {
        // A second body for one external name: two programs in one
        // compilation database (or an ODR violation). Each body keeps its
        // own calls, keyed by its file like internal linkage (ADR-0013).
        // Warn once: a later definition in a header is reached again.
        const std::string first_at =
            known->second.definition_file + ':' + std::to_string(known->second.definition_line);
        usr.insert(0, path + "#");
        known = recorded_.find(usr);
        if (known == recorded_.end()) {
            warnings_.push_back(path + ':' + std::to_string(line_begin) + ": function '" +
                                function.name + "' with external linkage is also defined at " +
                                first_at + "; recorded as a separate function (ADR-0013)");
        }
    }
    if (known != recorded_.end() && (known->second.has_definition || !is_definition)) {
        remember(function.canonical, known->second.id, is_definition);
        return std::nullopt;
    }

    auto written = out_->add_function({
        .id = {},
        .usr = usr,
        .name = function.name,
        .qualified_name = function.qualified_name,
        .signature = function.signature,
        .file = *file_id,
        .line_begin = line_begin,
        .line_end = function.line_end,
        .is_definition = is_definition,
        .linkage = is_internal ? facts::Linkage::internal : facts::Linkage::external,
    });
    if (!written) {
        status_ = std::unexpected(written.error());
        return std::nullopt;
    }
    remember(function.canonical, *written, is_definition);
    recorded_.insert_or_assign(std::move(usr),
                               Recorded{.id = *written,
                                        .has_definition = is_definition,
                                        .definition_file = is_definition ? path : "",
                                        .definition_line = is_definition ? line_begin : 0});
    if (!is_definition) {
        return std::nullopt;
    }
    return *written;
}

} // namespace lcart::analysis
