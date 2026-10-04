#include "function_fact_collector.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Index/USRGeneration.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Casting.h>

#include <optional>

namespace lcart::analysis {

namespace {

/// Declarations FR-003 does not mean by "function": compiler-generated ones
/// (implicit builtins, special members), lambda call operators and C++17
/// deduction guides.
[[nodiscard]] bool is_recorded_kind(const clang::FunctionDecl& function) {
    if (function.isImplicit() || function.isInvalidDecl() ||
        llvm::isa<clang::CXXDeductionGuideDecl>(function)) {
        return false;
    }
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
    return method == nullptr || !method->getParent()->isLambda();
}

[[nodiscard]] clang::OptionalFileEntryRef file_of(const clang::SourceManager& sources,
                                                  clang::SourceLocation location) {
    return sources.getFileEntryRefForID(sources.getFileID(sources.getExpansionLoc(location)));
}

} // namespace

FunctionFactCollector::FunctionFactCollector(FileFactCollector& files, facts::FactWriter& out)
    : files_(&files), out_(&out) {}

void FunctionFactCollector::on_function(clang::ASTContext& context,
                                        const clang::FunctionDecl& function) {
    if (!status_ || !is_recorded_kind(function)) {
        return;
    }
    clang::SourceManager& sources = context.getSourceManager();
    // A function written by a macro is located at the macro's use.
    const clang::OptionalFileEntryRef file = file_of(sources, function.getLocation());
    if (!file) {
        return;
    }
    const std::optional<facts::FileId> file_id = files_->internal_file(sources, *file);
    if (!file_id) {
        status_ = files_->status();
        return;
    }

    llvm::SmallString<128> clang_usr;
    if (clang::index::generateUSRForDecl(&function, clang_usr)) {
        return;
    }
    const bool is_internal = !function.isExternallyVisible();
    std::string usr = clang_usr.str().str();
    if (is_internal) {
        // Clang's USR for internal linkage names only the base name of the
        // file of the first declaration (`c:util.c@F@helper`), so `static`
        // functions in a/util.c and b/util.c would be one function
        // (FR-003 criterion 1). Prefix the path of that file.
        const clang::OptionalFileEntryRef first =
            file_of(sources, function.getCanonicalDecl()->getLocation());
        usr.insert(0, (first ? files_->fact_path(sources, *first).path : std::string{}) + "#");
    }

    const bool is_definition = function.isThisDeclarationADefinition();
    const auto known = has_definition_.find(usr);
    if (known != has_definition_.end() && (known->second || !is_definition)) {
        return;
    }

    const clang::CharSourceRange range = sources.getExpansionRange(function.getSourceRange());
    auto written = out_->add_function({
        .id = {},
        .usr = usr,
        .name = function.getNameAsString(),
        .qualified_name = function.getQualifiedNameAsString(),
        .signature = function.getType().getAsString(context.getPrintingPolicy()),
        .file = *file_id,
        .line_begin = sources.getSpellingLineNumber(range.getBegin()),
        .line_end = sources.getSpellingLineNumber(range.getEnd()),
        .is_definition = is_definition,
        .linkage = is_internal ? facts::Linkage::internal : facts::Linkage::external,
    });
    if (!written) {
        status_ = std::unexpected(written.error());
        return;
    }
    has_definition_.insert_or_assign(std::move(usr), is_definition);
}

} // namespace lcart::analysis
