#pragma once

#include <headerscope/analysis.hpp>
#include <string>

namespace headerscope {
// Export a complete or partial scan with UTF-8 paths.
// Use a project root consistent with the scan paths, or pass {} to export the full paths.
std::string exportGraphJson(const ScanResult& scan, const std::filesystem::path& projectRoot);
std::string exportGraphMermaid(const ScanResult& scan, const std::filesystem::path& projectRoot);
} // namespace headerscope
