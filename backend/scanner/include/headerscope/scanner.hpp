#pragma once

#include <headerscope/analysis.hpp>

namespace headerscope {

ScanResult scanIncludes(const std::filesystem::path& compilationDatabase);

} // namespace headerscope
