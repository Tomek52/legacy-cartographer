#include "translation_unit_recorder.hpp"

#include "analysis/argument_filter.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DynamicRecursiveASTVisitor.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lcart::analysis {

namespace {

[[nodiscard]] std::string absolute_name(const clang::SourceManager& sources,
                                        clang::FileEntryRef file) {
    llvm::SmallString<256> absolute{file.getName()};
    sources.getFileManager().makeAbsolutePath(absolute);
    return absolute.str().str();
}

/// Builds the batch of one translation unit: numbers its files and keeps
/// the batch-local references of the functions of the current AST.
class BatchBuilder {
public:
    BatchBuilder(TranslationUnitBatch& batch, WorkerFiles& files)
        : batch_(&batch), files_(&files) {}

    [[nodiscard]] TranslationUnitBatch& batch() { return *batch_; }
    [[nodiscard]] WorkerFiles& files() { return *files_; }

    [[nodiscard]] const FactPath& fact_path(const clang::SourceManager& sources,
                                            clang::FileEntryRef file) {
        return files_->fact_path(absolute_name(sources, file));
    }

    /// The index of `file` in the batch, added on first use.
    [[nodiscard]] BatchFileIndex file(clang::SourceManager& sources, clang::FileEntryRef file) {
        const std::string name = absolute_name(sources, file);
        const auto [it, inserted] =
            indexes_.try_emplace(name, static_cast<BatchFileIndex>(batch_->files.size()));
        if (inserted) {
            const FactPath& path = files_->fact_path(name);
            batch_->files.push_back(
                {.path = path.path,
                 .is_external = path.is_external,
                 .content_sha256 = files_->content_sha256(name, sources, file)});
        }
        return it->second;
    }

    /// Starts the references of a new AST.
    void begin_ast() { functions_.clear(); }

    /// The batch-local reference of the function whose canonical
    /// declaration is `canonical`, added on first use.
    [[nodiscard]] std::uint32_t function(const clang::FunctionDecl* canonical) {
        return functions_.try_emplace(canonical, static_cast<std::uint32_t>(functions_.size()))
            .first->second;
    }

    /// The reference of `canonical` if a function event of this AST has it.
    [[nodiscard]] std::optional<std::uint32_t>
    known_function(const clang::FunctionDecl* canonical) const {
        const auto found = functions_.find(canonical);
        if (found == functions_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

private:
    TranslationUnitBatch* batch_;
    WorkerFiles* files_;
    /// Clang's absolute file name -> index in the batch.
    std::map<std::string, BatchFileIndex> indexes_;
    /// Canonical declaration -> batch-local reference. Looked up only,
    /// never iterated, so pointer order cannot reach the facts.
    std::map<const clang::FunctionDecl*, std::uint32_t> functions_;
};

/// Collects error and fatal diagnostics of one translation unit. Warnings
/// are not stored: they do not affect coverage and would dominate the
/// database on large code bases.
class CollectingDiagnosticConsumer final : public clang::DiagnosticConsumer {
public:
    /// `translation_unit` is spelled as in the facts; `files` names the
    /// diagnostics' files the same way, so they match FILE rows.
    /// `root_prefixes` (each ending in `/`) are removed from messages.
    CollectingDiagnosticConsumer(std::string translation_unit, WorkerFiles& files,
                                 const std::vector<std::string>& root_prefixes)
        : translation_unit_(std::move(translation_unit)), files_(&files),
          root_prefixes_(&root_prefixes) {}

    void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                          const clang::Diagnostic& info) override {
        DiagnosticConsumer::HandleDiagnostic(level, info);
        const auto severity = to_severity(level);
        if (!severity) {
            return;
        }
        facts::DiagnosticFact fact{.translation_unit = translation_unit_,
                                   .file = {},
                                   .line = 0,
                                   .severity = *severity,
                                   .message = {}};
        llvm::SmallString<256> text;
        info.FormatDiagnostic(text);
        fact.message = without_root(text.str().str());
        if (info.hasSourceManager() && info.getLocation().isValid()) {
            const clang::PresumedLoc presumed =
                info.getSourceManager().getPresumedLoc(info.getLocation());
            if (presumed.isValid()) {
                llvm::SmallString<256> absolute{presumed.getFilename()};
                info.getSourceManager().getFileManager().makeAbsolutePath(absolute);
                fact.file = files_->fact_path(absolute.str().str()).path;
                fact.line = presumed.getLine();
            }
        }
        diagnostics_.push_back(std::move(fact));
    }

    [[nodiscard]] std::vector<facts::DiagnosticFact> take() { return std::move(diagnostics_); }

private:
    static std::optional<facts::DiagnosticSeverity>
    to_severity(clang::DiagnosticsEngine::Level level) {
        switch (level) {
        case clang::DiagnosticsEngine::Error:
            return facts::DiagnosticSeverity::error;
        case clang::DiagnosticsEngine::Fatal:
            return facts::DiagnosticSeverity::fatal;
        case clang::DiagnosticsEngine::Ignored:
        case clang::DiagnosticsEngine::Note:
        case clang::DiagnosticsEngine::Remark:
        case clang::DiagnosticsEngine::Warning:
            return std::nullopt;
        }
        std::unreachable();
    }

    /// Clang names files in some messages by absolute path ("no such file
    /// or directory: '/home/.../gone.c'"); facts must not depend on where
    /// the code is checked out (NFR-004, ADR-0016).
    [[nodiscard]] std::string without_root(std::string message) const {
        for (const std::string& prefix : *root_prefixes_) {
            for (std::size_t at = message.find(prefix); at != std::string::npos;
                 at = message.find(prefix, at)) {
                message.erase(at, prefix.size());
            }
        }
        return message;
    }

    std::string translation_unit_;
    WorkerFiles* files_;
    const std::vector<std::string>* root_prefixes_;
    std::vector<facts::DiagnosticFact> diagnostics_;
};

/// Records the preprocessor's file events.
class FileEventCallbacks final : public clang::PPCallbacks {
public:
    FileEventCallbacks(clang::SourceManager& sources, BatchBuilder& builder)
        : sources_(&sources), builder_(&builder) {}

    void FileChanged(clang::SourceLocation location, FileChangeReason reason,
                     clang::SrcMgr::CharacteristicKind /*file_type*/,
                     clang::FileID /*previous*/) override {
        // Covers a main file without any #include, which no directive reaches.
        if (reason != EnterFile || sources_->getFileID(location) != sources_->getMainFileID()) {
            return;
        }
        if (const clang::OptionalFileEntryRef main =
                sources_->getFileEntryRefForID(sources_->getMainFileID())) {
            builder_->batch().events.emplace_back(
                MainFileEvent{.file = builder_->file(*sources_, *main)});
        }
    }

    void InclusionDirective(clang::SourceLocation hash_location, const clang::Token& /*token*/,
                            llvm::StringRef spelled, bool is_angled,
                            clang::CharSourceRange /*range*/, clang::OptionalFileEntryRef file,
                            llvm::StringRef /*search_path*/, llvm::StringRef /*relative_path*/,
                            const clang::Module* /*module*/, bool /*module_imported*/,
                            clang::SrcMgr::CharacteristicKind /*file_type*/) override {
        IncludeEvent include{.includer = std::nullopt,
                             .included = std::nullopt,
                             .spelled = spelled.str(),
                             .line = 0,
                             .is_angled = is_angled};
        if (const clang::OptionalFileEntryRef includer =
                sources_->getFileEntryRefForID(sources_->getFileID(hash_location))) {
            // Directives in files outside the root are not recorded.
            if (builder_->fact_path(*sources_, *includer).is_external) {
                return;
            }
            include.includer = builder_->file(*sources_, *includer);
            include.line = sources_->getSpellingLineNumber(hash_location);
        }
        if (file) {
            include.included = builder_->file(*sources_, *file);
        }
        builder_->batch().events.emplace_back(std::move(include));
    }

private:
    clang::SourceManager* sources_;
    BatchBuilder* builder_;
};

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

/// For an indirect call, the member it calls (`ctx->backend->recv` in
/// `ctx->backend->recv(...)`); null for a plain function pointer.
[[nodiscard]] const clang::MemberExpr* called_member(const clang::Expr& callee) {
    const clang::Expr* expression = callee.IgnoreParenImpCasts();
    // `(*handler)(...)` and `(*ctx->backend->recv)(...)` call what `*` reads.
    while (const auto* dereference = llvm::dyn_cast<clang::UnaryOperator>(expression)) {
        if (dereference->getOpcode() != clang::UO_Deref) {
            break;
        }
        expression = dereference->getSubExpr()->IgnoreParenImpCasts();
    }
    return llvm::dyn_cast<clang::MemberExpr>(expression);
}

/// Clang's USR of `field` if it has function-pointer type, the key that
/// links indirect calls through it to the functions assigned to it
/// (ADR-0026); empty otherwise or if Clang cannot generate one.
[[nodiscard]] std::string function_pointer_field_usr(const clang::ValueDecl& member) {
    const auto* field = llvm::dyn_cast<clang::FieldDecl>(&member);
    if (field == nullptr || !field->getType()->isFunctionPointerType()) {
        return {};
    }
    llvm::SmallString<128> usr;
    if (clang::index::generateUSRForDecl(field, usr)) {
        return {};
    }
    return usr.str().str();
}

/// For an indirect call, the called member and the object it is read from,
/// as written: `backend.recv` for `ctx->backend->recv(...)` and
/// `local.recv` for `local.recv(...)`. Empty for a plain function pointer.
[[nodiscard]] std::string member_name(const clang::Expr& callee) {
    const clang::MemberExpr* member = called_member(callee);
    if (member == nullptr) {
        return {};
    }
    std::string name = member->getMemberDecl()->getNameAsString();
    const clang::Expr* base = member->getBase()->IgnoreParenImpCasts();
    if (const auto* outer = llvm::dyn_cast<clang::MemberExpr>(base)) {
        return outer->getMemberDecl()->getNameAsString() + '.' + name;
    }
    if (const auto* variable = llvm::dyn_cast<clang::DeclRefExpr>(base)) {
        return variable->getDecl()->getNameAsString() + '.' + name;
    }
    return name;
}

/// Visits the call expressions of one function body, including those in
/// lambdas (their call operators are not functions of their own, FR-003),
/// but not the member functions of local classes, which are recorded as
/// functions and visited on their own, nor unevaluated operands.
class CallVisitor final : public clang::DynamicRecursiveASTVisitor {
public:
    explicit CallVisitor(llvm::function_ref<void(const clang::CallExpr&)> on_call)
        : on_call_(on_call) {}

    bool VisitCallExpr(clang::CallExpr* call) override {
        on_call_(*call);
        return true;
    }

    bool TraverseCXXRecordDecl(clang::CXXRecordDecl* /*record*/) override { return true; }

    /// The operand of `sizeof` or `alignof` is not evaluated: a call in it
    /// never happens.
    bool TraverseUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* /*operand*/) override {
        return true;
    }

private:
    llvm::function_ref<void(const clang::CallExpr&)> on_call_;
};

/// The calls in the body of `definition`, in source order. A call whose
/// callee no function event of the AST names (declared outside the root, a
/// builtin) is left out, as is one located outside the root.
[[nodiscard]] std::vector<CallEvent> calls_of(const clang::FunctionDecl& definition,
                                              clang::SourceManager& sources,
                                              BatchBuilder& builder) {
    std::vector<CallEvent> calls;
    clang::Stmt* body = definition.getBody();
    if (body == nullptr) {
        return calls;
    }
    // Named: the visitor keeps only a reference to it.
    auto on_call = [&](const clang::CallExpr& call) {
        if (call.isInstantiationDependent()) {
            // Calls in a template are resolved only in its instantiations,
            // which are not recorded (FR-003).
            return;
        }
        const clang::Expr* callee_expression = call.getCallee();
        if (callee_expression == nullptr ||
            llvm::isa<clang::CXXPseudoDestructorExpr>(callee_expression->IgnoreParenImpCasts())) {
            return;
        }
        CallEvent event{
            .callee = std::nullopt, .via = {}, .field = {}, .file = {}, .line = 0, .column = 0};
        if (const clang::FunctionDecl* callee = call.getDirectCallee()) {
            event.callee = builder.known_function(callee->getCanonicalDecl());
            if (!event.callee) {
                return;
            }
        } else {
            event.via = member_name(*callee_expression);
            // FR-015 resolves a call through a field of function-pointer
            // type with the functions assigned to that field.
            if (const clang::MemberExpr* member = called_member(*callee_expression)) {
                event.field = function_pointer_field_usr(*member->getMemberDecl());
            }
        }

        // A call written in a macro is located at the macro's use (FR-004
        // criterion 3), like a function written by a macro (FR-003).
        const clang::SourceLocation site = sources.getExpansionLoc(call.getBeginLoc());
        const clang::OptionalFileEntryRef file =
            sources.getFileEntryRefForID(sources.getFileID(site));
        if (!file || builder.fact_path(sources, *file).is_external) {
            return;
        }
        event.file = builder.file(sources, *file);
        event.line = sources.getExpansionLineNumber(site);
        event.column = sources.getExpansionColumnNumber(site);
        calls.push_back(std::move(event));
    };
    CallVisitor{on_call}.TraverseStmt(body);
    return calls;
}

/// The function an initializer names, as `rtu_send` or `&rtu_send`, in
/// parentheses or cast; null for anything else (a null pointer, a variable).
[[nodiscard]] const clang::FunctionDecl* named_function(const clang::Expr& value) {
    const clang::Expr* expression = value.IgnoreParenCasts();
    if (const auto* address = llvm::dyn_cast<clang::UnaryOperator>(expression);
        address != nullptr && address->getOpcode() == clang::UO_AddrOf) {
        expression = address->getSubExpr()->IgnoreParenCasts();
    }
    const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(expression);
    return reference == nullptr ? nullptr
                                : llvm::dyn_cast<clang::FunctionDecl>(reference->getDecl());
}

using FieldInitializerVisitor =
    llvm::function_ref<void(const clang::FieldDecl& field, const clang::Expr& value)>;

/// `expression` as the semantic form of an initializer list; null if it is
/// none. The semantic form has one initializer per base class, then per
/// field in declaration order without unnamed bit-fields, one for the
/// initialized field of a union, and no designators, whether the source is
/// positional or designated (InitListExpr in clang/AST/Expr.h; checked with
/// -ast-dump of clang-20).
[[nodiscard]] const clang::InitListExpr* semantic_list(const clang::Expr& expression) {
    const auto* list = llvm::dyn_cast<clang::InitListExpr>(expression.IgnoreParenImpCasts());
    if (list == nullptr) {
        return nullptr;
    }
    const clang::InitListExpr* semantic = list->getSemanticForm();
    return semantic != nullptr ? semantic : list;
}

/// The initializers in the semantic `list` of a `record`, each with the
/// field it initializes, or null for a base class.
[[nodiscard]] std::vector<std::pair<const clang::FieldDecl*, const clang::Expr*>>
member_initializers(const clang::InitListExpr& list, const clang::RecordDecl& record) {
    std::vector<std::pair<const clang::FieldDecl*, const clang::Expr*>> members;
    unsigned index = 0;
    if (const auto* with_bases = llvm::dyn_cast<clang::CXXRecordDecl>(&record);
        with_bases != nullptr && with_bases->hasDefinition()) {
        index = std::min(with_bases->getNumBases(), list.getNumInits());
        for (unsigned base = 0; base < index; ++base) {
            members.emplace_back(nullptr, list.getInit(base));
        }
    }
    for (const clang::FieldDecl* field : record.fields()) {
        if (index >= list.getNumInits()) {
            break;
        }
        if (field->isUnnamedBitField() ||
            (record.isUnion() &&
             !clang::declaresSameEntity(list.getInitializedFieldInUnion(), field))) {
            continue;
        }
        members.emplace_back(field, list.getInit(index++));
    }
    return members;
}

/// Calls `on_field` with every field of function-pointer type that the
/// initializer list `initializer` initializes and the expression it gets,
/// in nested structs, base classes and array elements too.
void visit_function_pointer_fields(const clang::Expr& initializer,
                                   FieldInitializerVisitor on_field) {
    // A work list, not recursion: nesting depth is up to the analysed code.
    std::vector<const clang::Expr*> pending{&initializer};
    for (std::size_t next = 0; next < pending.size(); ++next) {
        const clang::InitListExpr* list =
            pending[next] == nullptr ? nullptr : semantic_list(*pending[next]);
        if (list == nullptr) {
            continue;
        }
        const clang::RecordDecl* record = list->getType()->getAsRecordDecl();
        if (record == nullptr) { // an array: its elements
            std::ranges::copy(list->inits(), std::back_inserter(pending));
            continue;
        }
        for (const auto& [field, value] : member_initializers(*list, *record)) {
            if (field != nullptr && value != nullptr && field->getType()->isFunctionPointerType()) {
                on_field(*field, *value);
            } else {
                pending.push_back(value);
            }
        }
    }
}

/// The functions that the initializer of `variable` assigns to struct
/// fields of function-pointer type (FR-015), located under the root and
/// declared in this AST (`builder` has their references).
[[nodiscard]] std::vector<FieldAssignmentEvent> field_assignments_of(const clang::VarDecl& variable,
                                                                     clang::SourceManager& sources,
                                                                     BatchBuilder& builder) {
    std::vector<FieldAssignmentEvent> assignments;
    const clang::Expr* initializer = variable.getInit();
    // In a template, the initializer is resolved only in instantiations,
    // which are not recorded (as calls, FR-003).
    if (initializer == nullptr || initializer->isInstantiationDependent()) {
        return assignments;
    }
    // Named: the visitor keeps only a reference to it.
    auto on_field = [&](const clang::FieldDecl& field, const clang::Expr& value) {
        const clang::FunctionDecl* function = named_function(value);
        if (function == nullptr) {
            return;
        }
        const std::optional<std::uint32_t> reference =
            builder.known_function(function->getCanonicalDecl());
        const clang::OptionalFileEntryRef file = file_of(sources, value.getBeginLoc());
        if (!reference || !file || builder.fact_path(sources, *file).is_external) {
            return;
        }
        std::string usr = function_pointer_field_usr(field);
        if (!usr.empty()) {
            assignments.push_back({.field = std::move(usr), .function = *reference});
        }
    };
    visit_function_pointer_fields(*initializer, on_field);
    return assignments;
}

/// Records every function declaration of a parsed translation unit located
/// under the root. Template instantiations and implicit code are not
/// visited (the visitor's defaults). The dynamic visitor, not the
/// RecursiveASTVisitor template: inlining that template trips GCC's
/// -Wnull-dereference in Clang's own headers in release builds.
class FunctionVisitor final : public clang::DynamicRecursiveASTVisitor {
public:
    FunctionVisitor(clang::ASTContext& context, BatchBuilder& builder, AstEvent& ast,
                    std::vector<std::pair<std::size_t, const clang::FunctionDecl*>>& definitions,
                    std::vector<const clang::VarDecl*>& static_variables)
        : context_(&context), builder_(&builder), ast_(&ast), definitions_(&definitions),
          static_variables_(&static_variables) {}

    /// Variables with static storage duration and an initializer, whose
    /// field assignments FR-015 records (ADR-0026: not those of automatic
    /// variables, which are not static initializers).
    bool VisitVarDecl(clang::VarDecl* variable) override {
        if (variable->hasGlobalStorage() && variable->hasInit() && !variable->isInvalidDecl()) {
            static_variables_->push_back(variable);
        }
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* function) override {
        if (!is_recorded_kind(*function)) {
            return true;
        }
        clang::SourceManager& sources = context_->getSourceManager();
        // A function written by a macro is located at the macro's use.
        const clang::OptionalFileEntryRef file = file_of(sources, function->getLocation());
        if (!file || builder_->fact_path(sources, *file).is_external) {
            return true;
        }
        FunctionEvent event;
        event.file = builder_->file(sources, *file);
        llvm::SmallString<128> clang_usr;
        if (!clang::index::generateUSRForDecl(function, clang_usr)) {
            event.clang_usr = clang_usr.str().str();
        }
        event.is_internal = !function->isExternallyVisible();
        if (event.is_internal) {
            const clang::FunctionDecl* definition = function->getDefinition();
            const clang::FunctionDecl* named =
                definition != nullptr ? definition : function->getCanonicalDecl();
            const clang::OptionalFileEntryRef named_file = file_of(sources, named->getLocation());
            event.internal_prefix =
                named_file ? builder_->fact_path(sources, *named_file).path : std::string{};
        }
        event.is_definition = function->isThisDeclarationADefinition();
        const clang::CharSourceRange range = sources.getExpansionRange(function->getSourceRange());
        event.line_begin = sources.getSpellingLineNumber(range.getBegin());
        event.line_end = sources.getSpellingLineNumber(range.getEnd());
        event.name = function->getNameAsString();
        event.qualified_name = function->getQualifiedNameAsString();
        event.signature = function->getType().getAsString(context_->getPrintingPolicy());
        event.canonical = builder_->function(function->getCanonicalDecl());
        if (event.is_definition && event.clang_usr) {
            definitions_->emplace_back(ast_->functions.size(), function);
        }
        ast_->functions.push_back(std::move(event));
        return true;
    }

private:
    clang::ASTContext* context_;
    BatchBuilder* builder_;
    AstEvent* ast_;
    std::vector<std::pair<std::size_t, const clang::FunctionDecl*>>* definitions_;
    std::vector<const clang::VarDecl*>* static_variables_;
};

/// Records functions, then the calls of every definition and the field
/// assignments of static initializers: a call or an initializer may name a
/// function declared later in the translation unit (a C++ member defined
/// later in its class), and every function named must already have its
/// reference.
class FunctionAndCallConsumer final : public clang::ASTConsumer {
public:
    explicit FunctionAndCallConsumer(BatchBuilder& builder) : builder_(&builder) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        builder_->begin_ast();
        AstEvent ast;
        std::vector<std::pair<std::size_t, const clang::FunctionDecl*>> definitions;
        std::vector<const clang::VarDecl*> static_variables;
        FunctionVisitor{context, *builder_, ast, definitions, static_variables}.TraverseAST(
            context);
        for (const auto& [index, definition] : definitions) {
            ast.functions[index].calls =
                calls_of(*definition, context.getSourceManager(), *builder_);
        }
        for (const clang::VarDecl* variable : static_variables) {
            std::ranges::move(
                field_assignments_of(*variable, context.getSourceManager(), *builder_),
                std::back_inserter(ast.field_assignments));
        }
        builder_->batch().events.emplace_back(std::move(ast));
    }

private:
    BatchBuilder* builder_;
};

/// Syntax-only parse that also records file, include, function and call
/// events.
class RecordingAction final : public clang::SyntaxOnlyAction {
public:
    explicit RecordingAction(BatchBuilder& builder) : builder_(&builder) {}

protected:
    bool BeginSourceFileAction(clang::CompilerInstance& compiler) override {
        compiler.getPreprocessor().addPPCallbacks(
            std::make_unique<FileEventCallbacks>(compiler.getSourceManager(), *builder_));
        return SyntaxOnlyAction::BeginSourceFileAction(compiler);
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& /*compiler*/,
                                                          llvm::StringRef /*file*/) override {
        return std::make_unique<FunctionAndCallConsumer>(*builder_);
    }

private:
    BatchBuilder* builder_;
};

class RecordingActionFactory final : public clang::tooling::FrontendActionFactory {
public:
    explicit RecordingActionFactory(BatchBuilder& builder) : builder_(&builder) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<RecordingAction>(*builder_);
    }

private:
    BatchBuilder* builder_;
};

/// The compile commands of one translation unit, fetched by the main
/// thread, as the compilation database a worker's ClangTool reads.
class FetchedCommands final : public clang::tooling::CompilationDatabase {
public:
    explicit FetchedCommands(const std::vector<clang::tooling::CompileCommand>& commands)
        : commands_(&commands) {}

    [[nodiscard]] std::vector<clang::tooling::CompileCommand>
    getCompileCommands(llvm::StringRef /*file*/) const override {
        return *commands_;
    }

private:
    const std::vector<clang::tooling::CompileCommand>* commands_;
};

} // namespace

WorkerFiles::Known& WorkerFiles::known(const std::string& absolute_name) {
    const auto [it, inserted] = known_.try_emplace(absolute_name);
    if (inserted) {
        it->second.path = fact_path_of(absolute_name, root_);
    }
    return it->second;
}

const FactPath& WorkerFiles::fact_path(const std::string& absolute_name) {
    return known(absolute_name).path;
}

const std::optional<std::string>& WorkerFiles::content_sha256(const std::string& absolute_name,
                                                              clang::SourceManager& sources,
                                                              clang::FileEntryRef file) {
    Known& entry = known(absolute_name);
    if (!entry.hashed) {
        entry.hashed = true;
        if (const std::optional<llvm::MemoryBufferRef> content =
                sources.getMemoryBufferForFileOrNone(file)) {
            entry.content_sha256 =
                llvm::toHex(llvm::SHA256::hash(llvm::arrayRefFromStringRef(content->getBuffer())),
                            /*LowerCase=*/true);
        }
    }
    return entry.content_sha256;
}

TranslationUnitRecorder::TranslationUnitRecorder(std::filesystem::path root,
                                                 std::vector<std::string> root_prefixes)
    : files_(std::move(root)), root_prefixes_(std::move(root_prefixes)) {}

TranslationUnitBatch TranslationUnitRecorder::record(const ParseJob& job) {
    TranslationUnitBatch batch;
    batch.index = job.index;
    BatchBuilder builder{batch, files_};

    // Not the real file system: its working directory is the process's, and
    // ClangTool changes it to each command's directory (ADR-0019).
    const FetchedCommands commands{job.commands};
    const llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> file_system{
        llvm::vfs::createPhysicalFileSystem().release()};
    clang::tooling::ClangTool tool{
        commands, {job.file}, std::make_shared<clang::PCHContainerOperations>(), file_system};
    tool.setPrintErrorMessage(false);
    tool.appendArgumentsAdjuster(
        [&batch](const clang::tooling::CommandLineArguments& arguments, llvm::StringRef filename) {
            FilteredArguments filtered = remove_code_loading_arguments(arguments);
            for (std::size_t i = 0; i < filtered.removed.size(); ++i) {
                batch.warnings.push_back(filename.str() + ": removed '" + filtered.removed[i] +
                                         "' (" + filtered.reasons[i] + ")");
            }
            return std::move(filtered.arguments);
        });
    tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
        "-resource-dir=" LCART_CLANG_RESOURCE_DIR, clang::tooling::ArgumentInsertPosition::END));
    // Stop the driver from reading default configuration files (clang.cfg,
    // <triple>-clang.cfg) next to the program named by the command: their
    // arguments would bypass remove_code_loading_arguments() (docs/security.md
    // T5, ADR-0023). Inserted right after the program name (position BEGIN of
    // the argument list). --no-default-config is accepted in clang-cl mode too
    // (verified on clang-20 20.1.2). A `-cc1` command line must start with
    // `-cc1` right after the program name, so the flag is not inserted there;
    // ClangTool drives such commands anyway, and an explicit --config is still
    // removed by the filter above.
    //
    // -fno-delayed-template-parsing goes in the same place: for an MSVC
    // target before C++20 the driver defaults to delayed template parsing,
    // which leaves the calls of templates a unit never instantiates out of
    // the facts (ADR-0027). Placed before the command's own arguments, so a
    // -fdelayed-template-parsing, /Zc:twoPhase- or /permissive in the command
    // still wins (the driver takes the last of the pair). Accepted by the
    // `clang` driver and in clang-cl mode, and a no-op for other targets and
    // for C++20 (verified on clang-20 20.1.2; for C under an MSVC target it
    // drops a flag that has no effect there); cc1 does not accept it.
    tool.appendArgumentsAdjuster([](const clang::tooling::CommandLineArguments& arguments,
                                    llvm::StringRef /*filename*/) {
        clang::tooling::CommandLineArguments adjusted = arguments;
        if (adjusted.size() >= 2 && adjusted[1] == "-cc1") {
            return adjusted;
        }
        const auto after_program = adjusted.begin() + (adjusted.empty() ? 0 : 1);
        adjusted.insert(after_program, {"--no-default-config", "-fno-delayed-template-parsing"});
        return adjusted;
    });

    CollectingDiagnosticConsumer consumer{job.translation_unit, files_, root_prefixes_};
    tool.setDiagnosticConsumer(&consumer);
    RecordingActionFactory factory{builder};
    batch.parsed = tool.run(&factory) == 0;
    batch.diagnostics = consumer.take();
    return batch;
}

} // namespace lcart::analysis
