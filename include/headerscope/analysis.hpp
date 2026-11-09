#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace headerscope {

struct IncludeEdge {
    std::filesystem::path includer;
    std::filesystem::path included;
    std::uint32_t line;

    bool operator==(const IncludeEdge&) const = default;
};

enum class DiagnosticSeverity {
    warning,
    error,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    std::string message;
    std::optional<std::filesystem::path> file;
    std::optional<std::uint32_t> line;
};

enum class ScanStatus {
    complete,
    partial,
    failed,
};

struct ScanResult {
    ScanStatus status = ScanStatus::failed;
    std::vector<std::filesystem::path> files;
    std::vector<IncludeEdge> edges;
    std::vector<Diagnostic> diagnostics;
};

} // namespace headerscope
