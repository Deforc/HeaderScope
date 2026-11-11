#include "snapshot.hpp"

#include <headerscope/export.hpp>
#include <headerscope/reports.hpp>
#include <headerscope/scanner.hpp>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs = std::filesystem;

struct Options {
    std::optional<fs::path> file, save, compare;
    fs::path root = fs::current_path();
    bool rank = false;
    bool cyclesOnly = false;
    std::optional<std::string> exportFormat;
    std::optional<fs::path> exportFile;
};

bool sameFile(const fs::path& first, const fs::path& second)
{
    std::error_code error;
    return fs::equivalent(first, second, error) ||
        fs::weakly_canonical(fs::absolute(first)) == fs::weakly_canonical(fs::absolute(second));
}

void requireDistinctOutputs(const fs::path& graph, const fs::path& snapshot)
{
    if (sameFile(graph, snapshot))
        throw std::invalid_argument("Graph export and snapshot must use different files");
}

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 2; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--export") {
            if (options.exportFormat) throw std::invalid_argument("--export may only be specified once");
            if (i + 2 >= argc || std::string_view(argv[i + 1]).starts_with("--") || std::string_view(argv[i + 2]).starts_with("--"))
                throw std::invalid_argument("--export requires a format and output file");
            options.exportFormat = argv[++i]; options.exportFile = argv[++i];
            if (*options.exportFormat != "json" && *options.exportFormat != "mermaid")
                throw std::invalid_argument("Export format must be json or mermaid");
        } else if (argument == "--rank") {
            options.rank = true;
        } else if (argument == "--cycles-only") {
            options.cyclesOnly = true;
        } else if (argument == "--save" || argument == "--compare" || argument == "--root") {
            if (++i == argc || std::string_view(argv[i]).starts_with("--"))
                throw std::invalid_argument("Missing value for " + std::string(argument));
            if (argument == "--save") options.save = argv[i];
            else if (argument == "--compare") options.compare = argv[i];
            else options.root = argv[i];
        } else if (argument.starts_with('-') || options.file) {
            throw std::invalid_argument("Unexpected argument: " + std::string(argument));
        } else {
            options.file = argv[i];
        }
    }
    if (options.cyclesOnly && (options.rank || options.file))
        throw std::invalid_argument("--cycles-only cannot be combined with --rank or a file query");
    if (!fs::is_directory(options.root))
        throw std::invalid_argument("Project root must be an existing directory");
    options.root = fs::canonical(options.root);
    if (options.exportFile) {
        const auto& output = *options.exportFile;
        auto database = fs::path(argv[1]);
        std::error_code error;
        if (fs::is_directory(database, error))
            database /= "compile_commands.json";
        if (sameFile(output, database) || (options.compare && sameFile(output, *options.compare)))
            throw std::invalid_argument("Graph export must not overwrite an input file");
        if (options.save)
            requireDistinctOutputs(output, *options.save);
    }
    return options;
}

std::string display(const fs::path& file, const fs::path& root)
{
    const auto relative = file.lexically_relative(root);
    return (relative.empty() ? file : relative).generic_string();
}

void printSection(std::string_view title)
{
    std::cout << "\n== " << title << " ==\n";
}

void printIncludeGraph(const headerscope::ScanResult& scan, const fs::path& root)
{
    using DirectIncludes = std::map<fs::path, std::vector<std::uint32_t>>;
    std::map<fs::path, DirectIncludes> files;
    for (const auto& file : scan.files) files.try_emplace(file);
    for (const auto& edge : scan.edges) {
        files[edge.includer][edge.included].push_back(edge.line);
        files.try_emplace(edge.included);
    }

    printSection("Include graph");
    std::vector<std::string> withoutIncludes;
    for (const auto& [file, includes] : files) {
        if (includes.empty()) {
            withoutIncludes.push_back(display(file, root));
            continue;
        }
        std::cout << "  " << display(file, root) << '\n';
        for (const auto& [header, lines] : includes) {
            std::cout << "    -> " << display(header, root) << (lines.size() == 1 ? " (line " : " (lines ");
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i != 0) std::cout << ", ";
                std::cout << lines[i];
            }
            std::cout << ")\n";
        }
    }
    if (!withoutIncludes.empty()) {
        std::cout << "\n  Files without direct includes (" << withoutIncludes.size() << ")\n";
        for (const auto& file : withoutIncludes) std::cout << "    " << file << '\n';
    }
}

void printChain(const headerscope::FileChain& chain, const fs::path& root)
{
    std::cout << "    ";
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (i != 0) std::cout << " -> ";
        std::cout << display(chain[i], root);
    }
    std::cout << '\n';
}

void printCycleGroups(const headerscope::DependencyGraph& graph,
                      const std::vector<headerscope::CycleGroup>& groups, const fs::path& root)
{
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto chains = graph.simpleCycles(groups[i]);
        const auto fileCount = groups[i].members.size();
        std::cout << "  Group " << i + 1 << " (" << fileCount << (fileCount == 1 ? " file, " : " files, ")
                  << chains.size() << (chains.size() == 1 ? " cycle)\n" : " cycles)\n");
        for (const auto& chain : chains) printChain(chain, root);
    }
}

void printDiff(const headerscope::DependencyDiff& diff, const headerscope::DependencyGraph& graph,
               const fs::path& root, bool cyclesOnly)
{
    printSection("Snapshot comparison");
    const auto printEdges = [&](const char* title, const auto& edges) {
        std::cout << "  " << title << ": " << edges.size() << '\n';
        for (const auto& edge : edges)
            std::cout << "    " << display(edge.includer, root) << ':' << edge.line
                      << " -> " << display(edge.included, root) << '\n';
    };
    if (!cyclesOnly) {
        printEdges("Added includes", diff.added);
        printEdges("Removed includes", diff.removed);
    }
    std::cout << "  New or expanded cycle groups: " << diff.newCycles.size() << '\n';
    printCycleGroups(graph, diff.newCycles, root);
    if (cyclesOnly) return;
    std::cout << "  Affected source files: " << diff.affectedSources.size() << '\n';
    for (const auto& file : diff.affectedSources)
        std::cout << "    " << display(file, root) << '\n';
}

constexpr std::string_view usage =
    "Usage: headerscope_inspect <database> [file] [options]\n"
    "       headerscope_inspect --help | --version\n";
constexpr std::string_view helpHint = "Run headerscope_inspect --help for options and examples.\n";
constexpr std::string_view help = R"(
Analyze C and C++ #include dependencies using a compilation database.
Pass compile_commands.json or its directory. Pass a file to show the
shortest include chains from dependent files.

With no report or export option, prints the full graph and its cycles.
Paths in reports are relative to the current directory unless --root is set.

Options:
  -h, --help                     Show this help and exit.
  --version                      Print the HeaderScope version and exit.
  --cycles-only                  Show cycles without listing graph edges.
                                 With --compare, show only new or expanded cycle groups.
                                 Incompatible with --rank and a file query.
  --rank                         Rank headers by the number of dependent sources.
  --compare <snapshot>           Compare against a saved dependency snapshot.
  --save <snapshot>              Save a complete scan for later comparison.
  --root <directory>             Set the root for paths in reports, snapshots and exports.
  --export <json|mermaid> <file>  Write the graph to a file.

Examples:
  headerscope_inspect build/compile_commands.json
  headerscope_inspect build/compile_commands.json include/config.h --root .
  headerscope_inspect build --cycles-only
  headerscope_inspect build --compare before.json --cycles-only
  headerscope_inspect build --export mermaid graph.mmd

Exit codes: 0 success, 1 failed or partial scan, 2 invalid arguments.
)";
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2) {
        const std::string_view argument = argv[1];
        if (argument == "--help" || argument == "-h") {
            std::cout << usage << help;
            return 0;
        }
        if (argument == "--version") {
            std::cout << "headerscope_inspect " HEADERSCOPE_VERSION "\n";
            return 0;
        }
    }
    if (argc < 2 || std::string_view(argv[1]).starts_with('-')) {
        std::cerr << usage << helpHint;
        return 2;
    }
    try {
        const auto options = parseOptions(argc, argv);
        // Load before saving so --compare and --save may use the same filename.
        const auto baseline = options.compare
            ? std::optional{headerscope::loadSnapshot(*options.compare, options.root)}
            : std::nullopt;
        const auto scan = headerscope::scanIncludes(argv[1]);
        if (!scan.diagnostics.empty()) std::cerr << "== Diagnostics ==\n";
        for (const auto& diagnostic : scan.diagnostics) {
            if (diagnostic.file) {
                std::cerr << diagnostic.file->string();
                if (diagnostic.line) std::cerr << ':' << *diagnostic.line;
                std::cerr << ": ";
            }
            std::cerr << diagnostic.message << '\n';
        }
        if (scan.status == headerscope::ScanStatus::failed) return 1;

        std::cout << "== Analysis ==\n  "
                  << (scan.status == headerscope::ScanStatus::partial ? "Partial" : "Complete")
                  << " scan: " << scan.files.size() << " files, " << scan.edges.size()
                  << " include directives\n  Root: " << options.root.generic_string() << '\n';
        const headerscope::DependencyGraph graph(scan);
        if (baseline) {
            printDiff(headerscope::compareScans(*baseline, scan), graph, options.root, options.cyclesOnly);
        } else if (options.cyclesOnly || (!options.rank && !options.exportFormat)) {
            if (!options.cyclesOnly) printIncludeGraph(scan, options.root);
            printSection("Cycles");
            const auto cycles = graph.cycles();
            std::cout << "  Cycle groups: " << cycles.size() << '\n';
            printCycleGroups(graph, cycles, options.root);
        }
        if (options.rank) {
            printSection("Header impact (most affected sources first)");
            std::size_t position = 0;
            for (const auto& impact : headerscope::rankHeaders(scan))
                std::cout << "  " << ++position << ". " << display(impact.header, options.root) << ": "
                          << "sources: " << impact.affectedSources.size()
                          << ", direct includers: " << impact.directIncluders << '\n';
        }
        if (options.file) {
            const auto file = fs::weakly_canonical(fs::absolute(*options.file));
            printSection("Dependent chains");
            std::cout << "  For " << display(file, options.root) << '\n';
            const auto dependents = graph.dependents(file);
            if (dependents.empty()) std::cout << "    (none)\n";
            for (const auto& dependent : dependents)
                printChain(graph.shortestPath(dependent, file).value(), options.root);
        }
        if (options.exportFormat) {
            const auto text = *options.exportFormat == "json"
                ? headerscope::exportGraphJson(scan, options.root)
                : headerscope::exportGraphMermaid(scan, options.root);
            const auto outputPath = fs::absolute(*options.exportFile).u8string();
            const std::string target(reinterpret_cast<const char*>(outputPath.data()), outputPath.size());
            if (auto error = llvm::writeToOutput(target, [&](llvm::raw_ostream& output) -> llvm::Error {
                    output << text;
                    output.flush();
                    // writeToOutput uses raw_fd_ostream for absolute paths, except /dev/null.
                    if (target != "/dev/null") {
                        auto& file = static_cast<llvm::raw_fd_ostream&>(output);
                        if (file.has_error()) {
                            const auto errorCode = file.error();
                            file.clear_error();
                            return llvm::errorCodeToError(errorCode);
                        }
                    }
                    return llvm::Error::success();
                }))
                throw std::runtime_error("Cannot write graph export: " + llvm::toString(std::move(error)));
            std::cout << "Exported graph: " << options.exportFile->string() << '\n';
        }
        if (options.save) {
            // Case-insensitive or Unicode-equivalent names can alias once the export exists.
            if (options.exportFile) requireDistinctOutputs(*options.exportFile, *options.save);
            headerscope::saveSnapshot(scan, *options.save, options.root);
            std::cout << "Saved snapshot: " << options.save->string() << '\n';
        }
        return scan.status == headerscope::ScanStatus::complete ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << error.what() << '\n' << usage << helpHint;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
