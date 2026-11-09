#include <headerscope/reports.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace headerscope {
namespace {
using Path = std::filesystem::path;
using Connections = std::map<std::pair<Path, Path>, IncludeEdge>;

bool isSource(const Path& path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
    return extension == ".c" || extension == ".cc" ||
           extension == ".cpp" || extension == ".cxx" || extension == ".c++" ||
           extension == ".m" || extension == ".mm";
}

Connections connections(const ScanResult& scan)
{
    Connections result;
    for (const auto& edge : scan.edges) {
        const auto [entry, inserted] = result.emplace(
            std::pair{edge.includer, edge.included}, edge);
        if (!inserted)
            entry->second.line = std::min(entry->second.line, edge.line);
    }
    return result;
}
} // namespace

std::vector<HeaderImpact> rankHeaders(const ScanResult& scan)
{
    std::map<Path, std::set<Path>> direct;
    for (const auto& edge : scan.edges)
        if (!isSource(edge.included))
            direct[edge.included].insert(edge.includer);

    const DependencyGraph graph(scan);
    std::vector<HeaderImpact> ranking;
    for (const auto& [header, includers] : direct) {
        FileChain sources;
        for (const auto& file : graph.dependents(header))
            if (isSource(file))
                sources.push_back(file);
        std::ranges::sort(sources);
        ranking.push_back({header, includers.size(), std::move(sources)});
    }
    std::ranges::sort(ranking, [](const HeaderImpact& left, const HeaderImpact& right) {
        if (left.affectedSources.size() != right.affectedSources.size())
            return left.affectedSources.size() > right.affectedSources.size();
        if (left.directIncluders != right.directIncluders)
            return left.directIncluders > right.directIncluders;
        return left.header < right.header;
    });
    return ranking;
}

DependencyDiff compareScans(const ScanResult& before, const ScanResult& after)
{
    if (before.status != ScanStatus::complete || after.status != ScanStatus::complete)
        throw std::runtime_error("Dependency comparison requires two complete scans; fix scan errors first");

    DependencyDiff diff;
    const auto oldConnections = connections(before);
    const auto newConnections = connections(after);
    for (const auto& [pair, edge] : newConnections)
        if (!oldConnections.contains(pair))
            diff.added.push_back(edge);
    for (const auto& [pair, edge] : oldConnections)
        if (!newConnections.contains(pair))
            diff.removed.push_back(edge);

    const DependencyGraph oldGraph(before), newGraph(after);
    const auto oldCycles = oldGraph.cycles();
    for (const auto& group : newGraph.cycles()) {
        const bool alreadyCyclic = std::ranges::any_of(oldCycles, [&](const CycleGroup& old) {
            return std::ranges::includes(old.members, group.members);
        });
        if (!alreadyCyclic)
            diff.newCycles.push_back(group);
    }

    std::set<Path> affected;
    const auto collect = [&](const IncludeEdge& edge) {
        if (isSource(edge.includer))
            affected.insert(edge.includer);
        // Removed dependencies must also use the old graph for impact analysis.
        for (const auto* graph : {&oldGraph, &newGraph})
            for (const auto& file : graph->dependents(edge.includer))
                if (isSource(file))
                    affected.insert(file);
    };
    for (const auto& edge : diff.added) collect(edge);
    for (const auto& edge : diff.removed) collect(edge);
    diff.affectedSources.assign(affected.begin(), affected.end());
    return diff;
}

} // namespace headerscope
