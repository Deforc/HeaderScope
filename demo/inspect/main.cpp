#include <headerscope/graph.hpp>
#include <headerscope/scanner.hpp>

#include <iostream>

namespace {

void printChain(const headerscope::FileChain& chain)
{
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (i != 0) {
            std::cout << " -> ";
        }
        std::cout << chain[i].filename().string();
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: headerscope_inspect <compile_commands.json> [file]\n";
        return 2;
    }

    const auto scan = headerscope::scanIncludes(argv[1]);
    for (const auto& diagnostic : scan.diagnostics) {
        if (diagnostic.file) {
            std::cerr << diagnostic.file->string();
            if (diagnostic.line) {
                std::cerr << ':' << *diagnostic.line;
            }
            std::cerr << ": ";
        }
        std::cerr << diagnostic.message << '\n';
    }
    if (scan.status == headerscope::ScanStatus::failed) {
        return 1;
    }

    std::cout << (scan.status == headerscope::ScanStatus::partial ? "Partial" : "Complete")
              << " scan: " << scan.files.size() << " files, " << scan.edges.size()
              << " include directives\n";
    for (const auto& edge : scan.edges) {
        std::cout << edge.includer.filename().string() << ':' << edge.line << " -> "
                  << edge.included.filename().string() << '\n';
    }

    const headerscope::DependencyGraph graph(scan);
    const auto cycles = graph.cycles();
    std::cout << "Cycle groups: " << cycles.size() << '\n';
    for (const auto& group : cycles) {
        printChain(group.example);
    }
    if (argc == 3) {
        const auto file = std::filesystem::absolute(argv[2]).lexically_normal();
        std::cout << "Dependent chains for " << file.filename().string() << ":\n";
        for (const auto& dependent : graph.dependents(file)) {
            printChain(graph.shortestPath(dependent, file).value());
        }
    }
    return 0;
}
