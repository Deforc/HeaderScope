#pragma once

#include <headerscope/analysis.hpp>

namespace headerscope {

struct ScanOptions {
    // By default the graph contains only non-system includes.
    bool includeSystemHeaders = false;
};

// Accepts compile_commands.json or the directory containing it. Does not build
// the project or execute compiler commands. Edges are the union of active
// directives in all translation units/configurations recorded in the database.
// Paths are canonical absolute paths; lines are physical, one-based lines.
// Load/driver/preprocessor/parser errors are returned in diagnostics.
ScanResult scanProject(const std::filesystem::path& compilationDatabase,
                       const ScanOptions& options = {});

// Entry point used by the console prototype and existing application callers.
ScanResult scanIncludes(const std::filesystem::path& compilationDatabase);

} // namespace headerscope
