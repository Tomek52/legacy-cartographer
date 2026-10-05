#include "file_fact_collector.hpp"

#include "analysis/module_rules.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>
#include <vector>

namespace lcart::analysis {

namespace {

/// Extensions of files compiled on their own; everything else reached by an
/// `#include` (`.h`, `.inc`, extensionless C++ headers, ...) is a header.
[[nodiscard]] bool is_source_extension(const std::string& extension) {
    static constexpr std::array source_extensions{".c",   ".C",   ".cc", ".cpp",
                                                  ".cxx", ".c++", ".m",  ".mm"};
    return std::ranges::find(source_extensions, extension) != source_extensions.end();
}

} // namespace

FileFactCollector::FileFactCollector(ModuleRules rules, facts::FactWriter& out)
    : rules_(std::move(rules)), out_(&out) {}

common::Status FileFactCollector::restore(const facts::FactQuery& facts) {
    auto files = facts.files();
    if (!files) {
        return std::unexpected(files.error());
    }
    for (const facts::FileFact& file : *files) {
        file_ids_.insert_or_assign(file.path, file.id);
    }
    auto includes = facts.includes();
    if (!includes) {
        return std::unexpected(includes.error());
    }
    for (const facts::IncludeFact& include : *includes) {
        includes_.emplace(include.includer.value, include.line, include.spelled,
                          include.included ? include.included->value : -1);
    }
    return {};
}

common::Result<facts::FileId> FileFactCollector::record_file(const BatchFile& file) {
    if (const auto known = file_ids_.find(file.path); known != file_ids_.end()) {
        return known->second;
    }
    if (!file.content_sha256) {
        return std::unexpected(common::Error{.code = common::ErrorCode::io_error,
                                             .message = "cannot read " + file.path});
    }
    facts::FileFact fact{.id = {},
                         .path = file.path,
                         .content_sha256 = *file.content_sha256,
                         .is_header =
                             !is_source_extension(std::filesystem::path{file.path}.extension()),
                         .is_external = file.is_external,
                         .module = std::nullopt};
    if (!file.is_external) {
        auto module = out_->add_module({.id = {}, .name = module_of(file.path, rules_)});
        if (!module) {
            return std::unexpected(module.error());
        }
        fact.module = *module;
    }
    auto id = out_->add_file(fact);
    if (id) {
        file_ids_.emplace(file.path, *id);
    }
    return id;
}

std::optional<facts::FileId> FileFactCollector::internal_file(const BatchFile& file) {
    if (!status_ || file.is_external) {
        return std::nullopt;
    }
    auto id = record_file(file);
    if (!id) {
        status_ = std::unexpected(id.error());
        return std::nullopt;
    }
    return *id;
}

void FileFactCollector::on_main_file(const BatchFile& file) {
    static_cast<void>(internal_file(file));
}

void FileFactCollector::on_include(const IncludeEvent& include, std::span<const BatchFile> files) {
    if (!status_) {
        return;
    }
    if (!include.includer) {
        // A directive in Clang's predefines buffer, i.e. `-include file`:
        // no includer to record an edge for, but the file is still reached.
        if (include.included) {
            static_cast<void>(internal_file(files[*include.included]));
        }
        return;
    }
    const BatchFile& includer_file = files[*include.includer];
    if (includer_file.is_external) {
        return;
    }
    auto includer = record_file(includer_file);
    if (!includer) {
        status_ = std::unexpected(includer.error());
        return;
    }

    facts::IncludeFact fact{.includer = *includer,
                            .included = std::nullopt,
                            .spelled = include.spelled,
                            .line = include.line,
                            .is_system = include.is_angled};
    if (include.included) {
        auto included = record_file(files[*include.included]);
        if (!included) {
            status_ = std::unexpected(included.error());
            return;
        }
        fact.included = *included;
    }

    const auto key = std::tuple{fact.includer.value, fact.line, fact.spelled,
                                fact.included ? fact.included->value : -1};
    if (!includes_.insert(key).second) {
        return;
    }
    status_ = out_->add_include(fact);
}

} // namespace lcart::analysis
