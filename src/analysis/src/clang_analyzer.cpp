#include "analysis/clang_analyzer.hpp"

#include "analysis/argument_filter.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/DynamicRecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "call_fact_collector.hpp"
#include "file_fact_collector.hpp"
#include "function_fact_collector.hpp"

namespace lcart::analysis {

namespace {

/// Collects error and fatal diagnostics of one translation unit. Warnings
/// are not stored: they do not affect coverage and would dominate the
/// database on large code bases.
class CollectingDiagnosticConsumer final : public clang::DiagnosticConsumer {
public:
    explicit CollectingDiagnosticConsumer(std::string translation_unit)
        : translation_unit_(std::move(translation_unit)) {}

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
        fact.message = text.str().str();
        if (info.hasSourceManager() && info.getLocation().isValid()) {
            const clang::PresumedLoc presumed =
                info.getSourceManager().getPresumedLoc(info.getLocation());
            if (presumed.isValid()) {
                fact.file = presumed.getFilename();
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

    std::string translation_unit_;
    std::vector<facts::DiagnosticFact> diagnostics_;
};

/// Forwards the preprocessor's file events to the collector.
class FileFactCallbacks final : public clang::PPCallbacks {
public:
    FileFactCallbacks(clang::SourceManager& sources, FileFactCollector& collector)
        : sources_(&sources), collector_(&collector) {}

    void FileChanged(clang::SourceLocation location, FileChangeReason reason,
                     clang::SrcMgr::CharacteristicKind /*file_type*/,
                     clang::FileID /*previous*/) override {
        // Covers a main file without any #include, which no directive reaches.
        if (reason == EnterFile && sources_->getFileID(location) == sources_->getMainFileID()) {
            collector_->on_main_file(*sources_);
        }
    }

    void InclusionDirective(clang::SourceLocation hash_location, const clang::Token& /*token*/,
                            llvm::StringRef spelled, bool is_angled,
                            clang::CharSourceRange /*range*/, clang::OptionalFileEntryRef file,
                            llvm::StringRef /*search_path*/, llvm::StringRef /*relative_path*/,
                            const clang::Module* /*module*/, bool /*module_imported*/,
                            clang::SrcMgr::CharacteristicKind /*file_type*/) override {
        collector_->on_include(*sources_, hash_location, spelled, is_angled, file);
    }

private:
    clang::SourceManager* sources_;
    FileFactCollector* collector_;
};

/// Hands every function declaration of a parsed translation unit to the
/// collector, and keeps the definitions whose calls are to be recorded.
/// Template instantiations and implicit code are not visited (the visitor's
/// defaults). The dynamic visitor, not the RecursiveASTVisitor template:
/// inlining that template trips GCC's -Wnull-dereference in Clang's own
/// headers in release builds.
class FunctionVisitor final : public clang::DynamicRecursiveASTVisitor {
public:
    FunctionVisitor(clang::ASTContext& context, FunctionFactCollector& collector,
                    std::vector<const clang::FunctionDecl*>& new_definitions)
        : context_(&context), collector_(&collector), new_definitions_(&new_definitions) {}

    bool VisitFunctionDecl(clang::FunctionDecl* function) override {
        if (collector_->on_function(*context_, *function)) {
            new_definitions_->push_back(function);
        }
        return true;
    }

private:
    clang::ASTContext* context_;
    FunctionFactCollector* collector_;
    std::vector<const clang::FunctionDecl*>* new_definitions_;
};

/// The collectors of one analysis.
struct Collectors {
    FileFactCollector* files;
    FunctionFactCollector* functions;
    CallFactCollector* calls;
};

/// Records functions, then calls: a call may name a function declared later
/// in the translation unit (a C++ member defined later in its class), and
/// every callee must already have its id.
class FunctionFactConsumer final : public clang::ASTConsumer {
public:
    explicit FunctionFactConsumer(Collectors collectors) : collectors_(collectors) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        collectors_.functions->begin_translation_unit();
        std::vector<const clang::FunctionDecl*> new_definitions;
        FunctionVisitor{context, *collectors_.functions, new_definitions}.TraverseAST(context);
        for (const clang::FunctionDecl* definition : new_definitions) {
            collectors_.calls->on_definition(context, *definition);
        }
    }

private:
    Collectors collectors_;
};

/// Syntax-only parse that also feeds file, include and function facts to
/// the collectors.
class FactAction final : public clang::SyntaxOnlyAction {
public:
    explicit FactAction(Collectors collectors) : collectors_(collectors) {}

protected:
    bool BeginSourceFileAction(clang::CompilerInstance& compiler) override {
        compiler.getPreprocessor().addPPCallbacks(
            std::make_unique<FileFactCallbacks>(compiler.getSourceManager(), *collectors_.files));
        return SyntaxOnlyAction::BeginSourceFileAction(compiler);
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& /*compiler*/,
                                                          llvm::StringRef /*file*/) override {
        return std::make_unique<FunctionFactConsumer>(collectors_);
    }

private:
    Collectors collectors_;
};

class FactActionFactory final : public clang::tooling::FrontendActionFactory {
public:
    explicit FactActionFactory(Collectors collectors) : collectors_(collectors) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<FactAction>(collectors_);
    }

private:
    Collectors collectors_;
};

[[nodiscard]] common::Result<std::unique_ptr<clang::tooling::CompilationDatabase>>
load_compilation_database(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::io_error,
                          .message = "compilation database not found: " + path.string()});
    }
    std::string message;
    std::unique_ptr<clang::tooling::CompilationDatabase> database =
        clang::tooling::JSONCompilationDatabase::loadFromFile(
            path.string(), message, clang::tooling::JSONCommandLineSyntax::AutoDetect);
    if (!database) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::parse_error, .message = message});
    }
    // Expand @file arguments before filtering, so a response file cannot
    // smuggle plugin options past remove_code_loading_arguments().
    return clang::tooling::expandResponseFiles(std::move(database), llvm::vfs::getRealFileSystem());
}

} // namespace

common::Result<AnalysisSummary> ClangAnalyzer::analyze(const AnalysisRequest& request,
                                                       facts::FactWriter& out) {
    // TODO(B-11): apply request.exclude_globs, with the glob syntax of the module rules.
    auto database = load_compilation_database(request.compilation_database);
    if (!database) {
        return std::unexpected(database.error());
    }

    std::vector<std::string> files = (*database)->getAllFiles();
    std::ranges::sort(files); // deterministic order (NFR-004)

    std::error_code ec;
    std::filesystem::path root;
    if (!request.root.empty() && std::filesystem::is_directory(request.root, ec)) {
        const std::filesystem::path absolute = std::filesystem::absolute(request.root, ec);
        if (!ec) {
            root = std::filesystem::weakly_canonical(absolute, ec);
        }
    }
    if (root.empty() || ec) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::invalid_argument,
                          .message = "analysis root is not a directory: " + request.root.string()});
    }
    FileFactCollector file_collector{std::move(root), out};
    FunctionFactCollector function_collector{file_collector, out};
    CallFactCollector call_collector{file_collector, function_collector, out};
    FactActionFactory action_factory{Collectors{
        .files = &file_collector, .functions = &function_collector, .calls = &call_collector}};

    AnalysisSummary summary;
    summary.translation_units_total = static_cast<std::uint32_t>(files.size());

    for (const std::string& file : files) {
        clang::tooling::ClangTool tool{**database, {file}};
        tool.setPrintErrorMessage(false);

        tool.appendArgumentsAdjuster(
            [&summary](const clang::tooling::CommandLineArguments& arguments,
                       llvm::StringRef filename) {
                FilteredArguments filtered = remove_code_loading_arguments(arguments);
                for (const std::string& removed : filtered.removed) {
                    summary.warnings.push_back(filename.str() + ": removed '" + removed +
                                               "' (loads code into the analyser)");
                }
                return std::move(filtered.arguments);
            });
        tool.appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster("-resource-dir=" LCART_CLANG_RESOURCE_DIR,
                                                      clang::tooling::ArgumentInsertPosition::END));

        CollectingDiagnosticConsumer consumer{file};
        tool.setDiagnosticConsumer(&consumer);

        const int status = tool.run(&action_factory);
        for (const common::Status& collected :
             {file_collector.status(), function_collector.status(), call_collector.status()}) {
            if (!collected) {
                return std::unexpected(collected.error());
            }
        }
        std::ranges::move(function_collector.take_warnings(), std::back_inserter(summary.warnings));
        if (status == 0) {
            ++summary.translation_units_parsed;
        }
        for (const facts::DiagnosticFact& diagnostic : consumer.take()) {
            if (auto written = out.add_diagnostic(diagnostic); !written) {
                return std::unexpected(written.error());
            }
            ++summary.diagnostics;
        }
    }

    if (auto written =
            out.set_coverage({.translation_units_total = summary.translation_units_total,
                              .translation_units_parsed = summary.translation_units_parsed});
        !written) {
        return std::unexpected(written.error());
    }
    return summary;
}

} // namespace lcart::analysis
