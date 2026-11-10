#include "snapshot.hpp"

#include <headerscope/reports.hpp>
#include <headerscope/scanner.hpp>

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs = std::filesystem;

struct Options {
    std::optional<fs::path> file, save, compare;
    fs::path root = fs::current_path();
    bool rank = false;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 2; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--rank") {
            options.rank = true;
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
    if (!fs::is_directory(options.root))
        throw std::invalid_argument("Project root must be an existing directory");
    options.root = fs::canonical(options.root);
    return options;
}

std::string display(const fs::path& file, const fs::path& root = {})
{
    if (root.empty()) return file.filename().string();
    const auto relative = file.lexically_relative(root);
    return (relative.empty() ? file : relative).generic_string();
}

void printChain(const headerscope::FileChain& chain, const fs::path& root = {})
{
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (i != 0) std::cout << " -> ";
        std::cout << display(chain[i], root);
    }
    std::cout << '\n';
}

void printCycleGroups(const headerscope::DependencyGraph& graph,
                      const std::vector<headerscope::CycleGroup>& groups, const fs::path& root = {})
{
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto chains = graph.simpleCycles(groups[i]);
        std::cout << "Group " << i + 1 << " (" << chains.size() << " cycles)\n";
        for (const auto& chain : chains) printChain(chain, root);
    }
}

void printDiff(const headerscope::DependencyDiff& diff, const headerscope::DependencyGraph& graph,
               const fs::path& root)
{
    const auto printEdges = [&](const char* title, const auto& edges) {
        std::cout << title << ": " << edges.size() << '\n';
        for (const auto& edge : edges)
            std::cout << "  " << display(edge.includer, root) << ':' << edge.line
                      << " -> " << display(edge.included, root) << '\n';
    };
    printEdges("Added includes", diff.added);
    printEdges("Removed includes", diff.removed);
    std::cout << "New or expanded cycle groups: " << diff.newCycles.size() << '\n';
    printCycleGroups(graph, diff.newCycles, root);
    std::cout << "Affected source files: " << diff.affectedSources.size() << '\n';
    for (const auto& file : diff.affectedSources)
        std::cout << "  " << display(file, root) << '\n';
}

constexpr const char* usage =
    "Usage: headerscope_inspect <compile_commands.json> [file] "
    "[--save snapshot.json] [--compare snapshot.json] [--rank] [--root directory]\n";
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc < 2 || std::string_view(argv[1]).starts_with('-')) {
        std::cerr << usage;
        return 2;
    }
    try {
        const auto options = parseOptions(argc, argv);
        // Load before saving so --compare and --save may use the same filename.
        const auto baseline = options.compare
            ? std::optional{headerscope::loadSnapshot(*options.compare, options.root)}
            : std::nullopt;
        const auto scan = headerscope::scanIncludes(argv[1]);
        for (const auto& diagnostic : scan.diagnostics) {
            if (diagnostic.file) {
                std::cerr << diagnostic.file->string();
                if (diagnostic.line) std::cerr << ':' << *diagnostic.line;
                std::cerr << ": ";
            }
            std::cerr << diagnostic.message << '\n';
        }
        if (scan.status == headerscope::ScanStatus::failed) return 1;

        std::cout << (scan.status == headerscope::ScanStatus::partial ? "Partial" : "Complete")
                  << " scan: " << scan.files.size() << " files, " << scan.edges.size()
                  << " include directives\n";
        const headerscope::DependencyGraph graph(scan);
        if (!options.rank && !baseline) {
            for (const auto& edge : scan.edges)
                std::cout << display(edge.includer) << ':' << edge.line << " -> "
                          << display(edge.included) << '\n';
            const auto cycles = graph.cycles();
            std::cout << "Cycle groups: " << cycles.size() << '\n';
            printCycleGroups(graph, cycles);
        }
        if (baseline) printDiff(headerscope::compareScans(*baseline, scan), graph, options.root);
        if (options.rank) {
            std::cout << "Header impact (most affected sources first):\n";
            std::size_t position = 0;
            for (const auto& impact : headerscope::rankHeaders(scan))
                std::cout << ++position << ". " << display(impact.header, options.root) << ": "
                          << "sources: " << impact.affectedSources.size()
                          << ", direct includers: " << impact.directIncluders << '\n';
        }
        if (options.file) {
            const auto file = fs::weakly_canonical(fs::absolute(*options.file));
            std::cout << "Dependent chains for " << display(file) << ":\n";
            for (const auto& dependent : graph.dependents(file))
                printChain(graph.shortestPath(dependent, file).value());
        }
        if (options.save) {
            headerscope::saveSnapshot(scan, *options.save, options.root);
            std::cout << "Saved snapshot: " << options.save->string() << '\n';
        }
        return scan.status == headerscope::ScanStatus::complete ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << error.what() << '\n' << usage;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
