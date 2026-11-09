#include <headerscope/scanner.hpp>

#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>

#include <exception>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>

namespace headerscope {
namespace {

using Path = std::filesystem::path;
using Edges = std::set<std::tuple<Path, Path, std::uint32_t>>;

template <typename Handle, auto Dispose>
using ClangHandle = std::unique_ptr<std::remove_pointer_t<Handle>, decltype(Dispose)>;

std::string takeString(CXString value)
{
    const auto* text = clang_getCString(value);
    std::string result = text ? text : "";
    clang_disposeString(value);
    return result;
}

Path resolve(const Path& file, const Path& directory)
{
    return (directory / file).lexically_normal();
}

Path filePath(CXFile file, const Path& directory)
{
    if (!file) {
        return {};
    }
    const auto name = takeString(clang_getFileName(file));
    return name.empty() || name.front() == '<' ? Path{} : resolve(name, directory);
}

void addError(ScanResult& result, std::string message, const Path& file)
{
    result.diagnostics.push_back({DiagnosticSeverity::error, std::move(message),
                                  file, std::nullopt});
}

struct IncludeVisitor {
    const Path& directory;
    Edges& edges;
    std::exception_ptr error;
};

CXChildVisitResult collectInclude(CXCursor cursor, CXCursor, CXClientData data)
{
    if (clang_getCursorKind(cursor) != CXCursor_InclusionDirective) {
        return CXChildVisit_Continue;
    }
    auto& visitor = *static_cast<IncludeVisitor*>(data);
    try {
        CXFile sourceFile = nullptr;
        unsigned line = 0;
        clang_getFileLocation(clang_getCursorLocation(cursor), &sourceFile,
                              &line, nullptr, nullptr);
        const auto source = filePath(sourceFile, visitor.directory);
        const auto included = filePath(clang_getIncludedFile(cursor), visitor.directory);
        if (!source.empty() && !included.empty() && line != 0) {
            visitor.edges.emplace(source, included, line);
        }
        return CXChildVisit_Continue;
    } catch (...) {
        visitor.error = std::current_exception();
        return CXChildVisit_Break;
    }
}

bool collectDiagnostics(CXTranslationUnit unit, const Path& directory, ScanResult& result)
{
    bool hasErrors = false;
    const auto count = clang_getNumDiagnostics(unit);
    for (unsigned i = 0; i < count; ++i) {
        const ClangHandle<CXDiagnostic, clang_disposeDiagnostic> diagnostic(
            clang_getDiagnostic(unit, i), clang_disposeDiagnostic);
        const auto severity = clang_getDiagnosticSeverity(diagnostic.get());
        if (severity < CXDiagnostic_Warning) {
            continue;
        }
        const bool isError = severity >= CXDiagnostic_Error;
        hasErrors |= isError;
        CXFile file = nullptr;
        unsigned line = 0;
        clang_getFileLocation(clang_getDiagnosticLocation(diagnostic.get()),
                              &file, &line, nullptr, nullptr);
        const auto path = filePath(file, directory);
        result.diagnostics.push_back({
            isError ? DiagnosticSeverity::error : DiagnosticSeverity::warning,
            takeString(clang_getDiagnosticSpelling(diagnostic.get())),
            path.empty() ? std::nullopt : std::optional{path},
            line == 0 ? std::nullopt : std::optional<std::uint32_t>{line},
        });
    }
    return hasErrors;
}

} // namespace

ScanResult scanIncludes(const Path& compilationDatabase)
{
    ScanResult result;
    std::error_code error;
    const auto databasePath = std::filesystem::absolute(compilationDatabase, error).lexically_normal();
    if (error || databasePath.filename() != "compile_commands.json" ||
        !std::filesystem::is_regular_file(databasePath, error)) {
        addError(result, "Expected an existing compile_commands.json", compilationDatabase);
        return result;
    }

    const auto databaseDirectory = databasePath.parent_path();
    CXCompilationDatabase_Error databaseError;
    const ClangHandle<CXCompilationDatabase, clang_CompilationDatabase_dispose> database(
        clang_CompilationDatabase_fromDirectory(databaseDirectory.string().c_str(), &databaseError),
        clang_CompilationDatabase_dispose);
    if (!database || databaseError != CXCompilationDatabase_NoError) {
        addError(result, "Could not read the compilation database", databasePath);
        return result;
    }
    const ClangHandle<CXCompileCommands, clang_CompileCommands_dispose> commands(
        clang_CompilationDatabase_getAllCompileCommands(database.get()),
        clang_CompileCommands_dispose);
    const auto count = commands ? clang_CompileCommands_getSize(commands.get()) : 0;
    if (count == 0) {
        addError(result, "The compilation database contains no commands", databasePath);
        return result;
    }

    const ClangHandle<CXIndex, clang_disposeIndex> index(clang_createIndex(0, 0),
                                                       clang_disposeIndex);
    std::set<Path> files;
    Edges edges;
    unsigned parsedCount = 0;
    bool hasErrors = false;
    for (unsigned i = 0; i < count; ++i) {
        const auto command = clang_CompileCommands_getCommand(commands.get(), i);
        const auto directory = resolve(takeString(clang_CompileCommand_getDirectory(command)),
                                       databaseDirectory);
        const auto source = resolve(takeString(clang_CompileCommand_getFilename(command)),
                                    directory);
        const auto argc = clang_CompileCommand_getNumArgs(command);
        if (argc == 0) {
            addError(result, "The compilation command has no arguments", source);
            hasErrors = true;
            continue;
        }

        std::vector<std::string> args;
        args.reserve(argc + 1);
        args.push_back(takeString(clang_CompileCommand_getArg(command, 0)));
        args.push_back("-working-directory=" + directory.string());
        for (unsigned j = 1; j < argc; ++j) {
            args.push_back(takeString(clang_CompileCommand_getArg(command, j)));
        }
        std::vector<const char*> argv;
        argv.reserve(args.size());
        for (const auto& arg : args) {
            argv.push_back(arg.c_str());
        }

        CXTranslationUnit rawUnit = nullptr;
        const auto status = clang_parseTranslationUnit2FullArgv(
            index.get(), nullptr, argv.data(), static_cast<int>(argv.size()), nullptr, 0,
            CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_KeepGoing,
            &rawUnit);
        const ClangHandle<CXTranslationUnit, clang_disposeTranslationUnit> unit(
            rawUnit, clang_disposeTranslationUnit);
        if (status != CXError_Success || !unit) {
            addError(result, "Clang could not parse this compilation command (error " +
                                  std::to_string(status) + ")", source);
            hasErrors = true;
            continue;
        }

        ++parsedCount;
        files.insert(source);
        hasErrors |= collectDiagnostics(unit.get(), directory, result);
        IncludeVisitor visitor{directory, edges, {}};
        clang_visitChildren(clang_getTranslationUnitCursor(unit.get()), collectInclude, &visitor);
        if (visitor.error) {
            std::rethrow_exception(visitor.error);
        }
    }

    for (const auto& [source, included, line] : edges) {
        files.insert(source);
        files.insert(included);
        result.edges.push_back({source, included, line});
    }
    result.files.assign(files.begin(), files.end());
    if (parsedCount > 0) {
        result.status = hasErrors ? ScanStatus::partial : ScanStatus::complete;
    }
    return result;
}

} // namespace headerscope
