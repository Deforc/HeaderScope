#pragma once

#include <headerscope/analysis.hpp>

namespace headerscope::demo {

ScanResult makeIncludeChain(const std::filesystem::path& demoDirectory,
                            bool extraEnabled = false);

} // namespace headerscope::demo
