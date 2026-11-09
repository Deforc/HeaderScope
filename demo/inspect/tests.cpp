#include "snapshot.hpp"
#include <headerscope/reports.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
namespace fs = std::filesystem;
using namespace headerscope;
fs::path root;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Function> void rejects(Function function)
{
    try { function(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("Expected an error");
}

ScanResult scan(std::initializer_list<std::tuple<const char*, const char*, unsigned>> edges)
{
    ScanResult result;
    result.status = ScanStatus::complete;
    std::set<fs::path> files;
    for (const auto& [from, to, line] : edges) {
        result.edges.push_back({root / from, root / to, line});
        files.insert(root / from);
        files.insert(root / to);
    }
    result.files.assign(files.begin(), files.end());
    return result;
}

void ranking()
{
    const auto result = rankHeaders(scan({{"main.cpp", "a.h", 1}, {"main.cpp", "b.h", 2},
        {"a.h", "shared.h", 3}, {"b.h", "shared.h", 4}, {"a.h", "shared.h", 8},
        {"second.c", "shared.h", 1}}));
    require(result.size() == 3 && result[0].header == root / "shared.h", "Wrong top header");
    require(result[0].directIncluders == 3 && result[0].affectedSources.size() == 2,
            "Diamond paths or duplicate directives inflated counts");
    require(result[0].affectedSources == FileChain{root / "main.cpp", root / "second.c"},
            "Wrong affected sources");
    require(result[1].header == root / "a.h" && result[2].header == root / "b.h",
            "Tied rankings must use stable path order");
    require(rankHeaders(scan({})).empty(), "Empty graph ranking should be empty");
}

void rankingCycles()
{
    const auto result = rankHeaders(scan({{"main.cpp", "a.h", 1}, {"a.h", "b.h", 2},
        {"b.h", "a.h", 2}, {"second.MM", "b.h", 1}, {"main.cpp", "config", 3}}));
    require(result.size() == 3, "Extensionless include must be ranked");
    require(result[0].affectedSources.size() == 2 && result[1].affectedSources.size() == 2,
            "Cycles must not inflate dependent counts");
    require(result.back().header == root / "config", "Wrong extensionless header rank");
}

void changedDependencies()
{
    const auto before = scan({{"main.cpp", "a.h", 1}, {"other.c", "a.h", 1},
                              {"a.h", "old.h", 3}});
    const auto after = scan({{"main.cpp", "a.h", 1}, {"other.c", "a.h", 1},
                             {"a.h", "new.h", 7}});
    const auto diff = compareScans(before, after);
    require(diff.added.size() == 1 && diff.added[0].included == root / "new.h" &&
            diff.added[0].line == 7, "Added include/location missing");
    require(diff.removed.size() == 1 && diff.removed[0].included == root / "old.h",
            "Removed include missing");
    require(diff.affectedSources == FileChain{root / "main.cpp", root / "other.c"},
            "Transitive impact missing");
    require(diff.newCycles.empty(), "Acyclic changes reported as cycles");
    const auto removed = compareScans(before, scan({}));
    require(removed.affectedSources == diff.affectedSources, "Removed sources lost from impact");
}

void lineShifts()
{
    const auto diff = compareScans(scan({{"main.cpp", "a.h", 1}}),
        scan({{"main.cpp", "a.h", 30}, {"main.cpp", "a.h", 40}}));
    require(diff.added.empty() && diff.removed.empty() && diff.affectedSources.empty(),
            "Line shifts and repeated includes are not dependency changes");
}

void cycleChanges()
{
    const auto chain = scan({{"main.cpp", "a.h", 1}, {"a.h", "b.h", 3}});
    const auto cycle = scan({{"main.cpp", "a.h", 1}, {"a.h", "b.h", 3}, {"b.h", "a.h", 4}});
    const auto expanded = scan({{"main.cpp", "a.h", 1}, {"a.h", "b.h", 3},
        {"b.h", "a.h", 4}, {"b.h", "c.h", 5}, {"c.h", "a.h", 2}});
    const auto diff = compareScans(chain, cycle);
    require(diff.newCycles.size() == 1 && diff.newCycles[0].members.size() == 2,
            "New cycle not detected");
    require(diff.affectedSources == FileChain{root / "main.cpp"}, "Cycle impact missing");
    require(compareScans(cycle, cycle).newCycles.empty(), "Existing cycle reported as new");
    require(compareScans(cycle, expanded).newCycles.size() == 1, "Expanded cycle group missing");
    require(compareScans(expanded, cycle).newCycles.empty(), "Shrinking group is not a new cycle");
    require(compareScans(scan({}), scan({{"self.h", "self.h", 1}})).newCycles.size() == 1,
            "Self-include cycle missing");
}

void incompleteScans()
{
    auto partial = scan({});
    partial.status = ScanStatus::partial;
    rejects([&] { compareScans(partial, scan({})); });
    rejects([&] { compareScans(scan({}), partial); });
    partial.status = ScanStatus::failed;
    rejects([&] { compareScans(scan({}), partial); });
}

void snapshotRoundtrip()
{
    const auto original = scan({{"main.cpp", "dir/header \"quoted\".h", 2},
                                {"dir/header \"quoted\".h", "кириллица.h", 3}});
    const auto filename = root / "before.json";
    saveSnapshot(original, filename, root);
    const auto restored = loadSnapshot(filename, root);
    require(restored.files == original.files && restored.edges == original.edges &&
            restored.status == ScanStatus::complete, "Snapshot roundtrip failed");
    require(compareScans(original, restored).added.empty(), "Roundtrip introduced changes");

    const auto relocated = loadSnapshot(filename, root / "relocated");
    require(relocated.edges[0].includer == root / "relocated/main.cpp" &&
            relocated.edges[0].included == root / "relocated/dir/header \"quoted\".h",
            "Snapshot paths must be relative to the chosen project root");
}

void snapshotValidation()
{
    const auto filename = root / "invalid.json";
    rejects([&] { loadSnapshot(filename, root); });
    for (const auto* json : {"not JSON", "{}",
            R"({"version":2,"files":[],"edges":[]})",
            R"({"version":1,"files":[10],"edges":[]})",
            R"({"version":1,"files":["a.h"],"edges":[{"includer":"a.h","included":"a.h","line":0}]})",
            R"({"version":1,"files":["a.h"],"edges":[{"includer":"a.h","included":"missing.h","line":1}]})"}) {
        { std::ofstream output(filename); output << json; }
        rejects([&] { loadSnapshot(filename, root); });
    }
    auto valid = scan({{"main.cpp", "a.h", 1}});
    saveSnapshot(valid, filename, root);
    auto partial = valid;
    partial.status = ScanStatus::partial;
    rejects([&] { saveSnapshot(partial, filename, root); });
    require(loadSnapshot(filename, root).edges == valid.edges, "Partial scan damaged baseline");
    rejects([&] { saveSnapshot(valid, root / "missing-dir/out.json", root); });
}
} // namespace

int main()
{
    struct TemporaryDirectory {
        fs::path path = fs::temp_directory_path() / ("headerscope reports " +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
    } directory;
    fs::create_directories(directory.path);
    root = fs::canonical(directory.path);
    int failures = 0;
    for (const auto& [name, test] : std::initializer_list<std::pair<const char*, void(*)()>>{
            {"ranking with shared dependencies", ranking}, {"ranking cycles", rankingCycles},
            {"dependency changes and impact", changedDependencies}, {"line shifts", lineShifts},
            {"new and expanded cycles", cycleChanges}, {"partial scan protection", incompleteScans},
            {"snapshot roundtrip and relocation", snapshotRoundtrip},
            {"snapshot validation and preservation", snapshotValidation}}) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
