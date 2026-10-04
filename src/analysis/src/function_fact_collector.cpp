#include "function_fact_collector.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Index/USRGeneration.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Casting.h>

#include <optional>

namespace lcart::analysis {

namespace {

/// Skips declarations FR-003 does not mean by "function": compiler-generated
/// ones (implicit builtins, special members), lambda call operators and C++17
/// deduction guides.
[[nodiscard]] bool is_recorded_kind(const clang::FunctionDecl& function) {
    if (function.isImplicit() || function.isInvalidDecl() ||
        llvm::isa<clang::CXXDeductionGuideDecl>(function)) {
        return false;
    }
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
    const clang::CXXRecordDecl* parent = method == nullptr ? nullptr : method->getParent();
    return parent == nullptr || !parent->isLambda();
}

[[nodiscard]] clang::OptionalFileEntryRef file_of(const clang::SourceManager& sources,
                                                  clang::SourceLocation location) {
    return sources.getFileEntryRefForID(sources.getFileID(sources.getExpansionLoc(location)));
}

} // namespace

FunctionFactCollector::FunctionFactCollector(FileFactCollector& files, facts::FactWriter& out)
    : files_(&files), out_(&out) {}

void FunctionFactCollector::begin_translation_unit() {
    in_translation_unit_.clear();
}

std::optional<facts::FunctionId>
FunctionFactCollector::id_of(const clang::FunctionDecl& function) const {
    const auto found = in_translation_unit_.find(function.getCanonicalDecl());
    if (found == in_translation_unit_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::string FunctionFactCollector::internal_prefix(const clang::SourceManager& sources,
                                                   const clang::FunctionDecl& function) {
    // Clang's USR for internal linkage names only the base name of the
    // file of the first declaration (`c:util.c@F@helper`), so `static`
    // functions in a/util.c and b/util.c, or two definitions of a
    // `static` declared in one shared header, would be one function
    // (FR-003 criterion 1). Prefix the path of the file of this TU's
    // definition, or of the first declaration if the TU has none (ADR-0012).
    const clang::FunctionDecl* definition = function.getDefinition();
    const clang::FunctionDecl* named =
        definition != nullptr ? definition : function.getCanonicalDecl();
    const clang::OptionalFileEntryRef named_file = file_of(sources, named->getLocation());
    return (named_file ? files_->fact_path(sources, *named_file).path : std::string{}) + "#";
}

void FunctionFactCollector::remember(const clang::FunctionDecl* canonical, facts::FunctionId id,
                                     bool is_definition) {
    // A definition decides which function the TU's calls name; a later
    // redeclaration does not change it (ADR-0013).
    if (is_definition) {
        in_translation_unit_.insert_or_assign(canonical, id);
    } else {
        in_translation_unit_.emplace(canonical, id);
    }
}

std::optional<facts::FunctionId>
FunctionFactCollector::on_function(clang::ASTContext& context,
                                   const clang::FunctionDecl& function) {
    if (!status_ || !is_recorded_kind(function)) {
        return std::nullopt;
    }
    clang::SourceManager& sources = context.getSourceManager();
    // A function written by a macro is located at the macro's use.
    const clang::OptionalFileEntryRef file = file_of(sources, function.getLocation());
    if (!file) {
        return std::nullopt;
    }
    const std::optional<facts::FileId> file_id = files_->internal_file(sources, *file);
    if (!file_id) {
        status_ = files_->status();
        return std::nullopt;
    }

    llvm::SmallString<128> clang_usr;
    if (clang::index::generateUSRForDecl(&function, clang_usr)) {
        return std::nullopt;
    }
    const bool is_internal = !function.isExternallyVisible();
    std::string usr = clang_usr.str().str();
    if (is_internal) {
        usr.insert(0, internal_prefix(sources, function));
    }

    const bool is_definition = function.isThisDeclarationADefinition();
    const clang::CharSourceRange range = sources.getExpansionRange(function.getSourceRange());
    const std::uint32_t line_begin = sources.getSpellingLineNumber(range.getBegin());
    const std::string& path = files_->fact_path(sources, *file).path;

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
                                function.getNameAsString() +
                                "' with external linkage is also defined at " + first_at +
                                "; recorded as a separate function (ADR-0013)");
        }
    }
    const clang::FunctionDecl* canonical = function.getCanonicalDecl();
    if (known != recorded_.end() && (known->second.has_definition || !is_definition)) {
        remember(canonical, known->second.id, is_definition);
        return std::nullopt;
    }

    auto written = out_->add_function({
        .id = {},
        .usr = usr,
        .name = function.getNameAsString(),
        .qualified_name = function.getQualifiedNameAsString(),
        .signature = function.getType().getAsString(context.getPrintingPolicy()),
        .file = *file_id,
        .line_begin = line_begin,
        .line_end = sources.getSpellingLineNumber(range.getEnd()),
        .is_definition = is_definition,
        .linkage = is_internal ? facts::Linkage::internal : facts::Linkage::external,
    });
    if (!written) {
        status_ = std::unexpected(written.error());
        return std::nullopt;
    }
    remember(canonical, *written, is_definition);
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
