#include <headerscope/scanner.hpp>

#include <iostream>
#include <string_view>

namespace {
void writeQuoted(std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\')
            std::cout << '\\' << c;
        else if (c < 0x20)
            std::cout << "\\u00" << hex[c >> 4] << hex[c & 15];
        else
            std::cout << c;
    }
    std::cout << '"';
}

const char* statusName(headerscope::ScanStatus status)
{
    switch (status) {
    case headerscope::ScanStatus::complete: return "complete";
    case headerscope::ScanStatus::partial: return "partial";
    case headerscope::ScanStatus::failed: return "failed";
    }
    return "failed";
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3 ||
        (argc == 3 && std::string_view(argv[2]) != "--include-system-headers")) {
        std::cerr << "Usage: headerscope_scan <compile_commands.json|directory> "
                     "[--include-system-headers]\n";
        return 2;
    }

    const auto result = headerscope::scanProject(argv[1], {argc == 3});
    std::cout << "{\"status\":\"" << statusName(result.status) << "\",\"files\":[";
    bool first = true;
    for (const auto& file : result.files) {
        if (!first) std::cout << ',';
        first = false;
        writeQuoted(file.generic_string());
    }
    std::cout << "],\"edges\":[";
    first = true;
    for (const auto& edge : result.edges) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"includer\":";
        writeQuoted(edge.includer.generic_string());
        std::cout << ",\"included\":";
        writeQuoted(edge.included.generic_string());
        std::cout << ",\"line\":" << edge.line << '}';
    }
    std::cout << "],\"diagnostics\":[";
    first = true;
    for (const auto& diagnostic : result.diagnostics) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"severity\":\""
                  << (diagnostic.severity == headerscope::DiagnosticSeverity::error
                          ? "error" : "warning") << "\",\"message\":";
        writeQuoted(diagnostic.message);
        std::cout << ",\"file\":";
        if (diagnostic.file) writeQuoted(diagnostic.file->generic_string());
        else std::cout << "null";
        std::cout << ",\"line\":";
        if (diagnostic.line) std::cout << *diagnostic.line;
        else std::cout << "null";
        std::cout << '}';
    }
    std::cout << "]}\n";
    return result.status == headerscope::ScanStatus::complete ? 0 : 1;
}
