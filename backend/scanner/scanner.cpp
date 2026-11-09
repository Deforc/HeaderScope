#include <headerscope/scanner.hpp>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <memory>
#include <set>
#include <tuple>

namespace headerscope {
namespace {
namespace fs = std::filesystem;

fs::path normalize(fs::path path, const fs::path& base)
{
    if (path.is_relative())
        path = base / path;
    std::error_code error;
    const auto canonical = fs::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

fs::path workingDirectory(clang::FileManager& files)
{
    const auto directory = files.getVirtualFileSystem().getCurrentWorkingDirectory();
    return directory ? fs::path(*directory) : fs::path{};
}

fs::path filePath(clang::FileEntryRef file, const fs::path& base)
{
    const auto realPath = file.getFileEntry().tryGetRealPathName();
    return normalize(fs::path((realPath.empty() ? file.getName() : realPath).str()), base);
}

struct ScanState {
    ScanResult result;
    std::set<fs::path> files;
    // A source without includes is still a useful result, but a missing input is not.
    std::size_t startedSources = 0;
};

class Diagnostics final : public clang::DiagnosticConsumer {
public:
    explicit Diagnostics(ScanState& state) : state_(state) {}

    void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                          const clang::Diagnostic& info) override
    {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        if (level < clang::DiagnosticsEngine::Warning)
            return;

        llvm::SmallString<256> message;
        info.FormatDiagnostic(message);
        Diagnostic diagnostic{level == clang::DiagnosticsEngine::Warning
                                  ? DiagnosticSeverity::warning : DiagnosticSeverity::error,
                              message.str().str(), std::nullopt, std::nullopt};
        if (info.hasSourceManager() && info.getLocation().isValid()) {
            const auto& source = info.getSourceManager();
            const auto location = source.getFileLoc(info.getLocation());
            if (const auto file = source.getFileEntryRefForID(source.getFileID(location))) {
                diagnostic.file = filePath(*file, workingDirectory(source.getFileManager()));
                diagnostic.line = source.getSpellingLineNumber(location);
            }
        }
        state_.result.diagnostics.push_back(std::move(diagnostic));
    }

private:
    ScanState& state_;
};

class Includes final : public clang::PPCallbacks {
public:
    Includes(clang::SourceManager& source, ScanState& state, ScanOptions options)
        : source_(source), state_(state), options_(options),
          base_(workingDirectory(source.getFileManager())) {}

    void InclusionDirective(clang::SourceLocation hashLocation, const clang::Token&,
                            llvm::StringRef, bool, clang::CharSourceRange,
                            clang::OptionalFileEntryRef included, llvm::StringRef,
                            llvm::StringRef, const clang::Module*,
#if CLANG_VERSION_MAJOR >= 19
                            bool,
#endif
                            clang::SrcMgr::CharacteristicKind fileType) override
    {
        // Missing headers are reported by Clang. Never invent an endpoint.
        if (!included)
            return;
        const auto location = source_.getFileLoc(hashLocation);
        if (!options_.includeSystemHeaders &&
            (fileType != clang::SrcMgr::C_User || source_.isInSystemHeader(location)))
            return;
        const auto includer = source_.getFileEntryRefForID(source_.getFileID(location));
        if (!includer) // Ignore synthetic buffers such as <built-in> / command-line -include.
            return;

        IncludeEdge edge{filePath(*includer, base_), filePath(*included, base_),
                         source_.getSpellingLineNumber(location)};
        state_.files.insert(edge.includer);
        state_.files.insert(edge.included);
        state_.result.edges.push_back(std::move(edge));
    }

private:
    clang::SourceManager& source_;
    ScanState& state_;
    ScanOptions options_;
    fs::path base_;
};

class ScanAction final : public clang::SyntaxOnlyAction {
public:
    ScanAction(ScanState& state, ScanOptions options) : state_(state), options_(options) {}

    bool BeginSourceFileAction(clang::CompilerInstance& compiler) override
    {
        if (!clang::SyntaxOnlyAction::BeginSourceFileAction(compiler))
            return false;
        auto& source = compiler.getSourceManager();
        const auto file = source.getFileEntryRefForID(source.getMainFileID());
        if (!file)
            return false;
        ++state_.startedSources;
        state_.files.insert(filePath(*file, workingDirectory(compiler.getFileManager())));
        compiler.getPreprocessor().addPPCallbacks(
            std::make_unique<Includes>(source, state_, options_));
        return true;
    }

private:
    ScanState& state_;
    ScanOptions options_;
};

class ScanFactory final : public clang::tooling::FrontendActionFactory {
public:
    ScanFactory(ScanState& state, ScanOptions options) : state_(state), options_(options) {}
    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<ScanAction>(state_, options_);
    }

private:
    ScanState& state_;
    ScanOptions options_;
};

void addError(ScanState& state, std::string message)
{
    state.result.diagnostics.push_back(
        {DiagnosticSeverity::error, std::move(message), std::nullopt, std::nullopt});
}
} // namespace

ScanResult scanProject(const std::filesystem::path& compilationDatabase,
                       const ScanOptions& options)
{
    ScanState state;
    std::error_code error;
    const auto cwd = fs::current_path(error);
    if (error || compilationDatabase.empty()) {
        addError(state, "Cannot resolve compilation database path");
        return state.result;
    }
    auto path = normalize(compilationDatabase, cwd);
    if (fs::is_directory(path, error))
        path /= "compile_commands.json";

    std::string loadError;
    auto database = clang::tooling::JSONCompilationDatabase::loadFromFile(
        path.string(), loadError, clang::tooling::JSONCommandLineSyntax::AutoDetect);
    if (!database) {
        addError(state, "Cannot load " + path.string() + ": " + loadError);
        return state.result;
    }

    auto sources = database->getAllFiles();
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    if (sources.empty()) {
        addError(state, "Compilation database contains no source files");
        return state.result;
    }

    Diagnostics diagnostics(state);
    ScanFactory factory(state, options);
    // A private filesystem keeps per-command working directories out of the
    // application's process cwd. In particular, relative -I paths remain valid.
    clang::tooling::ClangTool tool(*database, sources,
        std::make_shared<clang::PCHContainerOperations>(), llvm::vfs::createPhysicalFileSystem());
    tool.appendArgumentsAdjuster([](const clang::tooling::CommandLineArguments& arguments,
                                    llvm::StringRef) {
        const bool specified = std::any_of(arguments.begin(), arguments.end(),
            [](const std::string& argument) {
                return argument == "-resource-dir" || argument.starts_with("-resource-dir=");
            });
        auto adjusted = arguments;
        if (!specified && !adjusted.empty())
            adjusted.insert(adjusted.begin() + 1,
                            "-resource-dir=" HEADERSCOPE_CLANG_RESOURCE_DIR);
        return adjusted;
    });
    tool.setDiagnosticConsumer(&diagnostics);
    tool.setPrintErrorMessage(false);
    const int exitCode = tool.run(&factory);
    const bool hasErrors = std::any_of(state.result.diagnostics.begin(),
        state.result.diagnostics.end(), [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
    if (exitCode != 0 && !hasErrors)
        addError(state, "Clang analysis failed (exit code " + std::to_string(exitCode) + ")");

    auto& edges = state.result.edges;
    std::sort(edges.begin(), edges.end(), [](const IncludeEdge& left, const IncludeEdge& right) {
        return std::tie(left.includer, left.included, left.line) <
               std::tie(right.includer, right.included, right.line);
    });
    // Keep distinct line numbers, but avoid duplicating shared headers per TU.
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    state.result.files.assign(state.files.begin(), state.files.end());
    state.result.status = state.startedSources == 0 ? ScanStatus::failed
        : (exitCode != 0 || hasErrors ? ScanStatus::partial : ScanStatus::complete);
    return std::move(state.result);
}

ScanResult scanIncludes(const std::filesystem::path& compilationDatabase)
{
    return scanProject(compilationDatabase);
}

} // namespace headerscope
