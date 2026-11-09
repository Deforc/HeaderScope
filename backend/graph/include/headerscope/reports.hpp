#pragma once

#include <headerscope/graph.hpp>

namespace headerscope {

struct HeaderImpact {
    std::filesystem::path header;
    std::size_t directIncluders;
    FileChain affectedSources;
};

// Ranks included non-source files by the number of dependent C/C++ sources.
std::vector<HeaderImpact> rankHeaders(const ScanResult& scan);

struct DependencyDiff {
    std::vector<IncludeEdge> added;
    std::vector<IncludeEdge> removed;
    // New or enlarged strongly connected groups, not every possible cycle.
    std::vector<CycleGroup> newCycles;
    FileChain affectedSources;
};

// Compares file pairs, ignoring duplicate directives and changes in line numbers.
// Requires two complete scans to avoid reporting missing data as removed edges.
DependencyDiff compareScans(const ScanResult& before, const ScanResult& after);

} // namespace headerscope
