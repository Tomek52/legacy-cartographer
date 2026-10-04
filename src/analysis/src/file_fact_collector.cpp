#include "file_fact_collector.hpp"

#include "analysis/module_rules.hpp"

#include <clang/Basic/FileManager.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/SHA256.h>

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace lcart::analysis {

namespace {

/// Extensions of files compiled on their own; everything else reached by an
/// `#include` (`.h`, `.inc`, extensionless C++ headers, ...) is a header.
[[nodiscard]] bool is_source_extension(const std::string& extension) {
    static constexpr std::array source_extensions{".c",   ".C",   ".cc", ".cpp",
                                                  ".cxx", ".c++", ".m",  ".mm"};
    return std::ranges::find(source_extensions, extension) != source_extensions.end();
}

[[nodiscard]] std::string sha256_hex(llvm::StringRef content) {
    return llvm::toHex(llvm::SHA256::hash(llvm::arrayRefFromStringRef(content)),
                       /*LowerCase=*/true);
}

} // namespace

FileFactCollector::FileFactCollector(std::filesystem::path root, ModuleRules rules,
                                     facts::FactWriter& out)
    : root_(std::move(root)), rules_(std::move(rules)), out_(&out) {}

const FileFactCollector::FactPath& FileFactCollector::fact_path(const clang::SourceManager& sources,
                                                                clang::FileEntryRef file) {
    llvm::SmallString<256> absolute{file.getName()};
    sources.getFileManager().makeAbsolutePath(absolute);
    return fact_path(absolute.str().str());
}

const FileFactCollector::FactPath& FileFactCollector::fact_path(const std::string& absolute_name) {
    const auto [it, inserted] = fact_paths_.try_emplace(absolute_name);
    if (!inserted) {
        return it->second;
    }
    // Canonical, so a file reached through a symlink or `..` compares
    // equal to the canonical root.
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(it->first, ec);
    if (ec) {
        canonical = std::filesystem::path{it->first}.lexically_normal();
    }
    const std::filesystem::path relative = canonical.lexically_relative(root_);
    const bool is_external =
        relative.empty() || relative == "." || *relative.begin() == std::filesystem::path{".."};
    it->second = {.path = (is_external ? canonical : relative).generic_string(),
                  .is_external = is_external};
    return it->second;
}

common::Result<facts::FileId> FileFactCollector::record_file(clang::SourceManager& sources,
                                                             clang::FileEntryRef file,
                                                             const FactPath& path) {
    if (const auto known = file_ids_.find(path.path); known != file_ids_.end()) {
        return known->second;
    }
    const std::optional<llvm::MemoryBufferRef> content = sources.getMemoryBufferForFileOrNone(file);
    if (!content) {
        return std::unexpected(common::Error{.code = common::ErrorCode::io_error,
                                             .message = "cannot read " + path.path});
    }
    facts::FileFact fact{.id = {},
                         .path = path.path,
                         .content_sha256 = sha256_hex(content->getBuffer()),
                         .is_header =
                             !is_source_extension(std::filesystem::path{path.path}.extension()),
                         .is_external = path.is_external,
                         .module = std::nullopt};
    if (!path.is_external) {
        auto module = out_->add_module({.id = {}, .name = module_of(path.path, rules_)});
        if (!module) {
            return std::unexpected(module.error());
        }
        fact.module = *module;
    }
    auto id = out_->add_file(fact);
    if (id) {
        file_ids_.emplace(path.path, *id);
    }
    return id;
}

std::optional<facts::FileId> FileFactCollector::internal_file(clang::SourceManager& sources,
                                                              clang::FileEntryRef file) {
    if (!status_) {
        return std::nullopt;
    }
    const FactPath& path = fact_path(sources, file);
    if (path.is_external) {
        return std::nullopt;
    }
    auto id = record_file(sources, file, path);
    if (!id) {
        status_ = std::unexpected(id.error());
        return std::nullopt;
    }
    return *id;
}

void FileFactCollector::on_main_file(clang::SourceManager& sources) {
    if (const clang::OptionalFileEntryRef main =
            sources.getFileEntryRefForID(sources.getMainFileID())) {
        static_cast<void>(internal_file(sources, *main));
    }
}

void FileFactCollector::on_include(clang::SourceManager& sources,
                                   clang::SourceLocation hash_location, llvm::StringRef spelled,
                                   bool is_angled, clang::OptionalFileEntryRef file) {
    if (!status_) {
        return;
    }
    const clang::OptionalFileEntryRef includer_file =
        sources.getFileEntryRefForID(sources.getFileID(hash_location));
    if (!includer_file) {
        // A directive in Clang's predefines buffer, i.e. `-include file`:
        // no includer to record an edge for, but the file is still reached.
        if (file) {
            static_cast<void>(internal_file(sources, *file));
        }
        return;
    }
    const FactPath& includer_path = fact_path(sources, *includer_file);
    if (includer_path.is_external) {
        return;
    }
    auto includer = record_file(sources, *includer_file, includer_path);
    if (!includer) {
        status_ = std::unexpected(includer.error());
        return;
    }

    facts::IncludeFact include{.includer = *includer,
                               .included = std::nullopt,
                               .spelled = spelled.str(),
                               .line = sources.getSpellingLineNumber(hash_location),
                               .is_system = is_angled};
    if (file) {
        auto included = record_file(sources, *file, fact_path(sources, *file));
        if (!included) {
            status_ = std::unexpected(included.error());
            return;
        }
        include.included = *included;
    }

    const auto key = std::tuple{include.includer.value, include.line, include.spelled,
                                include.included ? include.included->value : -1};
    if (!includes_.insert(key).second) {
        return;
    }
    status_ = out_->add_include(include);
}

} // namespace lcart::analysis
