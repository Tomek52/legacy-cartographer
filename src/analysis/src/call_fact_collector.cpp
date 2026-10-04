#include "call_fact_collector.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DynamicRecursiveASTVisitor.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/Support/Casting.h>

#include <optional>
#include <string>

namespace lcart::analysis {

namespace {

/// For an indirect call, the called member and the object it is read from,
/// as written: `backend.recv` for `ctx->backend->recv(...)` and
/// `local.recv` for `local.recv(...)`. FR-015 resolves the member through
/// the tables assigned to it. Empty for a plain function pointer.
[[nodiscard]] std::string member_name(const clang::Expr& callee) {
    const clang::Expr* expression = callee.IgnoreParenImpCasts();
    // `(*handler)(...)` and `(*ctx->backend->recv)(...)` call what `*` reads.
    while (const auto* dereference = llvm::dyn_cast<clang::UnaryOperator>(expression)) {
        if (dereference->getOpcode() != clang::UO_Deref) {
            break;
        }
        expression = dereference->getSubExpr()->IgnoreParenImpCasts();
    }
    const auto* member = llvm::dyn_cast<clang::MemberExpr>(expression);
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

} // namespace

CallFactCollector::CallFactCollector(FileFactCollector& files,
                                     const FunctionFactCollector& functions, facts::FactWriter& out)
    : files_(&files), functions_(&functions), out_(&out) {}

void CallFactCollector::on_definition(clang::ASTContext& context,
                                      const clang::FunctionDecl& definition,
                                      facts::FunctionId caller) {
    clang::Stmt* body = definition.getBody();
    if (!status_ || body == nullptr) {
        return;
    }
    clang::SourceManager& sources = context.getSourceManager();

    // Named: the visitor keeps only a reference to it.
    auto on_call = [&](const clang::CallExpr& call) {
        if (!status_ || call.isInstantiationDependent()) {
            // Calls in a template are resolved only in its instantiations,
            // which are not recorded (FR-003).
            return;
        }
        const clang::Expr* callee_expression = call.getCallee();
        if (callee_expression == nullptr ||
            llvm::isa<clang::CXXPseudoDestructorExpr>(callee_expression->IgnoreParenImpCasts())) {
            return;
        }
        facts::CallFact fact{.caller = caller,
                             .callee = std::nullopt,
                             .kind = facts::CallKind::indirect,
                             .via = {},
                             .site = {}};
        if (const clang::FunctionDecl* callee = call.getDirectCallee()) {
            fact.callee = functions_->id_of(*callee);
            if (!fact.callee) {
                return; // declared outside the root, or a builtin: not a function fact
            }
            fact.kind = facts::CallKind::direct;
        } else {
            fact.via = member_name(*callee_expression);
        }

        // A call written in a macro is located at the macro's use (FR-004
        // criterion 3), like a function written by a macro (FR-003).
        const clang::SourceLocation site = sources.getExpansionLoc(call.getBeginLoc());
        const clang::OptionalFileEntryRef file =
            sources.getFileEntryRefForID(sources.getFileID(site));
        if (!file) {
            return;
        }
        const std::optional<facts::FileId> file_id = files_->internal_file(sources, *file);
        if (!file_id) {
            status_ = files_->status();
            return;
        }
        fact.site = {.file = *file_id,
                     .line = sources.getExpansionLineNumber(site),
                     .column = sources.getExpansionColumnNumber(site)};
        if (auto written = out_->add_call(fact); !written) {
            status_ = written;
        }
    };
    CallVisitor{on_call}.TraverseStmt(body);
}

} // namespace lcart::analysis
