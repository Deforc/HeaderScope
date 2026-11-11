#pragma once

#include <headerscope/analysis.hpp>

#include <map>

namespace headerscope {

using FileChain = std::vector<std::filesystem::path>;

struct CycleGroup {
    FileChain members;
    FileChain example;
};

class DependencyGraph {
public:
    explicit DependencyGraph(const ScanResult& scan);

    FileChain includes(const std::filesystem::path& file) const;
    FileChain dependents(const std::filesystem::path& file) const;
    std::optional<FileChain> shortestPath(const std::filesystem::path& from,
                                          const std::filesystem::path& to) const;
    std::vector<CycleGroup> cycles() const;
    std::vector<FileChain> simpleCycles(const CycleGroup& group) const;

private:
    using Adjacency = std::map<std::filesystem::path, FileChain>;
    Adjacency forward_;
    Adjacency reverse_;
};

} // namespace headerscope
