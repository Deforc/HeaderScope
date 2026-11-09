#pragma once

#include <headerscope/analysis.hpp>

namespace headerscope {

// Accepts compile_commands.json or its directory. Returns active, non-system
// include directives with canonical paths and physical one-based line numbers.
ScanResult scanIncludes(const std::filesystem::path& compilationDatabase);

} // namespace headerscope
