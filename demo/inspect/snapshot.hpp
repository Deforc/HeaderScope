#pragma once

#include <headerscope/analysis.hpp>

namespace headerscope {

// Snapshots contain project-relative paths; use the same project root on load.
// Throws std::runtime_error for invalid snapshots or I/O failures.
ScanResult loadSnapshot(const std::filesystem::path& filename,
                        const std::filesystem::path& projectRoot);
void saveSnapshot(const ScanResult& scan, const std::filesystem::path& filename,
                  const std::filesystem::path& projectRoot);

} // namespace headerscope
