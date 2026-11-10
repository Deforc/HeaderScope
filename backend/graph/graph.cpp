#include <headerscope/graph.hpp>

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

namespace headerscope {
namespace {

using Path = std::filesystem::path;
using Adjacency = std::map<Path, FileChain>;

struct BreadthFirst {
    FileChain order;
    std::map<Path, Path> predecessor;
};

BreadthFirst traverse(const Adjacency& graph, const Path& start,
                      const Path* stop = nullptr)
{
    if (!graph.contains(start)) {
        return {};
    }

    BreadthFirst visit{{start}, {{start, start}}};
    for (std::size_t head = 0; head < visit.order.size(); ++head) {
        const auto current = visit.order[head];
        if (stop && current == *stop) {
            break;
        }
        for (const auto& next : graph.at(current)) {
            if (visit.predecessor.try_emplace(next, current).second) {
                visit.order.push_back(next);
            }
        }
    }
    return visit;
}

void sortAndDeduplicate(Adjacency& graph)
{
    for (auto& [file, neighbors] : graph) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
}

} // namespace

DependencyGraph::DependencyGraph(const ScanResult& scan)
{
    for (const auto& file : scan.files) {
        forward_.try_emplace(file);
        reverse_.try_emplace(file);
    }
    for (const auto& edge : scan.edges) {
        forward_[edge.includer].push_back(edge.included);
        reverse_[edge.included].push_back(edge.includer);
        forward_.try_emplace(edge.included);
        reverse_.try_emplace(edge.includer);
    }
    sortAndDeduplicate(forward_);
    sortAndDeduplicate(reverse_);
}

FileChain DependencyGraph::includes(const Path& file) const
{
    const auto found = forward_.find(file);
    return found == forward_.end() ? FileChain{} : found->second;
}

FileChain DependencyGraph::dependents(const Path& file) const
{
    auto order = traverse(reverse_, file).order;
    if (!order.empty()) {
        order.erase(order.begin());
    }
    return order;
}

std::optional<FileChain> DependencyGraph::shortestPath(const Path& from,
                                                       const Path& to) const
{
    const auto visit = traverse(forward_, from, &to);
    if (!visit.predecessor.contains(to)) {
        return std::nullopt;
    }
    FileChain chain{to};
    while (chain.back() != from) {
        chain.push_back(visit.predecessor.at(chain.back()));
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

std::vector<CycleGroup> DependencyGraph::cycles() const
{
    struct Visit {
        std::size_t index;
        std::size_t low;
        bool onStack = true;
    };
    std::map<Path, Visit> visits;
    FileChain stack;
    std::vector<CycleGroup> groups;

    const std::function<void(const Path&)> tarjan = [&](const Path& file) {
        const auto index = visits.size();
        auto& visit = visits.emplace(file, Visit{index, index}).first->second;
        stack.push_back(file);

        for (const auto& next : forward_.at(file)) {
            const auto found = visits.find(next);
            if (found == visits.end()) {
                tarjan(next);
                visit.low = std::min(visit.low, visits.at(next).low);
            } else if (found->second.onStack) {
                visit.low = std::min(visit.low, found->second.index);
            }
        }
        if (visit.low != visit.index) {
            return;
        }

        FileChain members;
        do {
            visits.at(stack.back()).onStack = false;
            members.push_back(std::move(stack.back()));
            stack.pop_back();
        } while (members.back() != file);
        if (members.size() == 1 && !std::ranges::binary_search(forward_.at(file), file)) {
            return;
        }

        std::ranges::sort(members);
        const auto& first = members.front();
        const auto& neighbors = forward_.at(first);
        const auto neighbor = std::ranges::find_if(neighbors, [&](const Path& next) {
            return (members.size() == 1 || next != first) &&
                std::ranges::binary_search(members, next);
        });
        // Any path between members stays inside their strongly connected component.
        auto example = shortestPath(*neighbor, first).value();
        example.insert(example.begin(), first);
        groups.push_back({std::move(members), std::move(example)});
    };

    for (const auto& [file, neighbors] : forward_) {
        if (!visits.contains(file)) {
            tarjan(file);
        }
    }
    return groups;
}

std::vector<FileChain> DependencyGraph::simpleCycles(const CycleGroup& group) const
{
    std::vector<FileChain> cycles;
    std::set<Path> available(group.members.begin(), group.members.end());
    // Start each directed cycle at its smallest path so rotations appear only once.
    for (const auto& start : group.members) {
        available.erase(start);
        FileChain path{start};
        const auto enumerate = [&](auto&& self) -> void {
            const auto& neighbors = forward_.at(path.back());
            if (std::ranges::binary_search(neighbors, start)) {
                cycles.push_back(path);
                cycles.back().push_back(start);
            }
            if (std::ranges::none_of(neighbors, [&](const Path& next) { return available.contains(next); })) {
                return;
            }

            // Only extend the current path through unused files that can reach its start.
            std::set<Path> canClose;
            FileChain pending{start};
            for (std::size_t head = 0; head < pending.size(); ++head) {
                for (const auto& previous : reverse_.at(pending[head])) {
                    if (available.contains(previous) && canClose.insert(previous).second) {
                        pending.push_back(previous);
                    }
                }
            }
            for (const auto& next : neighbors) {
                if (canClose.contains(next)) {
                    available.erase(next);
                    path.push_back(next);
                    self(self);
                    path.pop_back();
                    available.insert(next);
                }
            }
        };
        enumerate(enumerate);
    }
    return cycles;
}

} // namespace headerscope
