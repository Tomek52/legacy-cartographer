#include "analysis/clang_analyzer.hpp"

#include "analysis/argument_filter.hpp"

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    // TODO(B-08): apply request.exclude_globs and store paths relative to request.root.
    auto database = load_compilation_database(request.compilation_database);
    if (!database) {
        return std::unexpected(database.error());
    }

    std::vector<std::string> files = (*database)->getAllFiles();
    std::ranges::sort(files); // deterministic order (NFR-004)

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

        const int status =
            tool.run(clang::tooling::newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
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
