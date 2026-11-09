#include "snapshot.hpp"

#include <llvm/Support/Error.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace headerscope {
namespace {
namespace fs = std::filesystem;

fs::path decodePath(const llvm::json::Value& value, const fs::path& root)
{
    const auto text = value.getAsString();
    if (!text || text->empty())
        throw std::runtime_error("Snapshot contains an invalid file path");
    const fs::path path(text->str());
    if (path.is_absolute() || path.has_root_name())
        throw std::runtime_error("Snapshot paths must be project-relative");
    return (root / path).lexically_normal();
}
} // namespace

ScanResult loadSnapshot(const fs::path& filename, const fs::path& projectRoot)
{
    std::ifstream input(filename, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open snapshot: " + filename.string());
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    if (input.bad())
        throw std::runtime_error("Cannot read snapshot: " + filename.string());
    auto json = llvm::json::parse(text);
    if (!json)
        throw std::runtime_error("Invalid snapshot JSON: " + llvm::toString(json.takeError()));
    const auto* object = json->getAsObject();
    if (!object || object->getInteger("version") != 1 ||
        !object->getArray("files") || !object->getArray("edges"))
        throw std::runtime_error("Expected a HeaderScope snapshot with version 1, files and edges");

    const auto root = fs::weakly_canonical(fs::absolute(projectRoot));
    std::set<fs::path> files;
    for (const auto& file : *object->getArray("files"))
        files.insert(decodePath(file, root));

    ScanResult scan;
    scan.status = ScanStatus::complete;
    scan.files.assign(files.begin(), files.end());
    for (const auto& value : *object->getArray("edges")) {
        const auto* edge = value.getAsObject();
        if (!edge || !edge->get("includer") || !edge->get("included"))
            throw std::runtime_error("Snapshot contains an invalid include edge");
        const auto line = edge->getInteger("line");
        if (!line || *line <= 0 || *line > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Snapshot include line must be a positive 32-bit integer");
        const auto includer = decodePath(*edge->get("includer"), root);
        const auto included = decodePath(*edge->get("included"), root);
        if (!files.contains(includer) || !files.contains(included))
            throw std::runtime_error("Snapshot edge refers to a file absent from files");
        scan.edges.push_back({includer, included, static_cast<std::uint32_t>(*line)});
    }
    return scan;
}

void saveSnapshot(const ScanResult& scan, const fs::path& filename,
                  const fs::path& projectRoot)
{
    if (scan.status != ScanStatus::complete)
        throw std::runtime_error("Only complete scans can be saved as a comparison baseline");
    const auto root = fs::weakly_canonical(fs::absolute(projectRoot));
    const auto relative = [&](const fs::path& file) {
        const auto path = file.lexically_relative(root);
        if (path.empty())
            throw std::runtime_error("Snapshot files and project root must use the same filesystem root");
        return path.generic_string();
    };
    llvm::json::Array files, edges;
    for (const auto& file : scan.files)
        files.push_back(relative(file));
    for (const auto& edge : scan.edges)
        edges.push_back(llvm::json::Object{{"includer", relative(edge.includer)},
            {"included", relative(edge.included)}, {"line", edge.line}});
    const auto text = llvm::formatv("{0:2}\n", llvm::json::Value(llvm::json::Object{
        {"version", 1}, {"files", std::move(files)}, {"edges", std::move(edges)}})).str();

    // Preserve the previous baseline if writing the replacement fails.
    if (auto error = llvm::writeToOutput(fs::absolute(filename).string(),
            [&](llvm::raw_ostream& output) {
                output << text;
                return llvm::Error::success();
            }))
        throw std::runtime_error("Cannot write snapshot: " + llvm::toString(std::move(error)));
}

} // namespace headerscope
