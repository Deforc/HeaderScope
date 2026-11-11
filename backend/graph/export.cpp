#include <headerscope/export.hpp>

#include <algorithm>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace headerscope {
namespace {
namespace fs = std::filesystem;

struct Node {
    fs::path path;
    std::string label;
};

struct Graph {
    ScanStatus status;
    std::vector<Node> nodes;
    std::map<std::pair<std::size_t, std::size_t>, std::set<std::uint32_t>> edges;
};

bool validUtf8(const std::string& text)
{
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        const std::size_t length = c >= 0xc2 && c <= 0xdf ? 2
            : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!length || i + length > text.size()) return false;
        for (std::size_t j = 1; j < length; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return false;
        if (length >= 3) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) ||
                (c == 0xf0 && next < 0x90) || (c == 0xf4 && next >= 0x90)) return false;
        }
        i += length;
    }
    return true;
}

std::string pathLabel(const fs::path& path)
{
    const auto bytes = path.generic_u8string();
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!validUtf8(text)) throw std::runtime_error("Graph export requires UTF-8 file paths");
    return text;
}

Graph normalize(const ScanResult& scan, const fs::path& root)
{
    if (scan.status == ScanStatus::failed) throw std::invalid_argument("Cannot export a failed scan");
    std::set<fs::path> paths(scan.files.begin(), scan.files.end());
    for (const auto& edge : scan.edges) {
        paths.insert(edge.includer);
        paths.insert(edge.included);
    }
    Graph graph{scan.status, {}, {}};
    for (const auto& path : paths) {
        const auto relative = root.empty() ? fs::path{} : path.lexically_relative(root);
        graph.nodes.push_back({path, pathLabel(relative.empty() ? path : relative)});
    }
    std::sort(graph.nodes.begin(), graph.nodes.end(), [](const Node& a, const Node& b) {
        return a.label == b.label ? a.path < b.path : a.label < b.label;
    });
    std::map<fs::path, std::size_t> ids;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) ids.emplace(graph.nodes[i].path, i);
    for (const auto& edge : scan.edges)
        graph.edges[{ids.at(edge.includer), ids.at(edge.included)}].insert(edge.line);
    return graph;
}

std::string jsonString(const std::string& value)
{
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 0x20) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + '"';
}

std::string mermaidLabel(const std::string& value)
{
    std::string result;
    // Encode syntax, HTML, Markdown and control characters as Mermaid decimal entities.
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f || c == '"' || c == '&' || c == '#' || c == '<' ||
            c == '>' || c == '%' || c == '\\' || c == ';' || c == '[' || c == ']' || c == '`')
            result += "#" + std::to_string(c) + ";";
        else
            result += static_cast<char>(c);
    }
    return result;
}

const char* status(ScanStatus value)
{
    return value == ScanStatus::partial ? "partial" : "complete";
}
} // namespace

std::string exportGraphJson(const ScanResult& scan, const fs::path& root)
{
    const auto graph = normalize(scan, root);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n  \"version\": 1,\n  \"status\": \"" << status(graph.status) << "\",\n  \"nodes\": [";
    for (std::size_t i = 0; i < graph.nodes.size(); ++i)
        out << (i ? "," : "") << "\n    {\"id\": \"n" << i << "\", \"path\": "
            << jsonString(graph.nodes[i].label) << "}";
    out << "\n  ],\n  \"edges\": [";
    bool first = true;
    for (const auto& [pair, lines] : graph.edges) {
        out << (first ? "" : ",") << "\n    {\"source\": \"n" << pair.first
            << "\", \"target\": \"n" << pair.second << "\", \"lines\": [";
        first = false;
        bool firstLine = true;
        for (auto line : lines) {
            out << (firstLine ? "" : ", ") << line;
            firstLine = false;
        }
        out << "]}";
    }
    return out.str() + "\n  ]\n}\n";
}

std::string exportGraphMermaid(const ScanResult& scan, const fs::path& root)
{
    const auto graph = normalize(scan, root);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "%% HeaderScope scan: " << status(graph.status) << "\nflowchart LR\n";
    for (std::size_t i = 0; i < graph.nodes.size(); ++i)
        out << "    n" << i << "[\"" << mermaidLabel(graph.nodes[i].label) << "\"]\n";
    for (const auto& [pair, lines] : graph.edges) {
        (void)lines;
        out << "    n" << pair.first << " --> n" << pair.second << "\n";
    }
    return out.str();
}
} // namespace headerscope
