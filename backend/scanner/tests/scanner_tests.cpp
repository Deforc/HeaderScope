#include <headerscope/scanner.hpp>
#include <headerscope/demo/fixture.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace headerscope;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Project {
    fs::path root;

    Project()
    {
        static unsigned sequence = 0;
        root = fs::temp_directory_path() / ("headerscope test " +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence++));
        fs::create_directories(root);
        root = fs::canonical(root);
    }
    ~Project()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
    void write(const fs::path& name, const std::string& content)
    {
        fs::create_directories((root / name).parent_path());
        std::ofstream output(root / name);
        output << content;
        require(output.good(), "Cannot write test file");
    }
    static std::string quote(const std::string& text)
    {
        std::ostringstream output;
        output << std::quoted(text);
        return output.str();
    }
    std::string command(const fs::path& source,
                        const std::vector<std::string>& flags = {}) const
    {
        std::string text = "{\"directory\":" + quote(root.string()) +
            ",\"file\":" + quote(source.string()) +
            ",\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\"";
        for (const auto& flag : flags) text += "," + quote(flag);
        return text + "," + quote(source.string()) + "]}";
    }
    void database(const std::vector<std::string>& commands)
    {
        std::string text = "[";
        for (const auto& command : commands) {
            if (text.size() > 1) text += ',';
            text += command;
        }
        write("compile_commands.json", text + "]");
    }
    ScanResult scan() const { return scanIncludes(root); }
};

bool hasEdge(const ScanResult& result, const fs::path& from, const fs::path& to,
             std::uint32_t line)
{
    return std::find(result.edges.begin(), result.edges.end(),
        IncludeEdge{fs::weakly_canonical(from), fs::weakly_canonical(to), line})
        != result.edges.end();
}

void checkInvariants(const ScanResult& result)
{
    require(std::is_sorted(result.files.begin(), result.files.end()), "Unsorted files");
    for (const auto& file : result.files)
        require(file.is_absolute() && file == fs::weakly_canonical(file), "Noncanonical file");
    for (const auto& edge : result.edges) {
        require(edge.line > 0, "Zero-based line");
        for (const auto& file : {edge.includer, edge.included})
            require(std::binary_search(result.files.begin(), result.files.end(), file),
                    "Edge endpoint absent from files");
    }
}

void checkDemo(bool extra)
{
    Project project;
    const fs::path directory = HEADERSCOPE_DEMO_DIRECTORY;
    project.database({project.command(directory / "main.cpp",
        extra ? std::vector<std::string>{"-DHEADERSCOPE_DEMO_EXTRA"} : std::vector<std::string>{})});
    const auto result = extra ? project.scan()
                              : scanIncludes(project.root / "compile_commands.json");
    const auto expected = headerscope::demo::makeIncludeChain(directory, extra);
    require(result.status == ScanStatus::complete, "Demo scan failed");
    require(result.diagnostics.empty(), "Unexpected demo diagnostic");
    require(result.edges.size() == expected.edges.size(), "Wrong number of demo edges");
    for (const auto& edge : expected.edges)
        require(hasEdge(result, edge.includer, edge.included, edge.line), "Wrong demo edge/line");
    checkInvariants(result);
}

void includePathsAndMacros()
{
    Project project;
    project.write("include dir/a.h", "#pragma once\n#include \"b.h\"\n");
    project.write("include dir/b.h", "#pragma once\n");
    project.write("main.cpp", "#define HEADER <a.h>\n#include HEADER\n#include HEADER\n"
                              "#if 0\n#include \"nonexistent.h\"\n#endif\n");
    project.database({project.command("main.cpp", {"-Iinclude dir"})});
    const auto cwd = fs::current_path();
    const auto result = project.scan();
    require(fs::current_path() == cwd, "Scan changed process working directory");
    require(result.status == ScanStatus::complete, "Relative -I/macro include failed");
    require(result.edges.size() == 3, "Repeated include lines must survive header guards");
    require(hasEdge(result, project.root / "main.cpp", project.root / "include dir/a.h", 2),
            "Macro include missing");
    require(hasEdge(result, project.root / "main.cpp", project.root / "include dir/a.h", 3),
            "Second directive lost");
    checkInvariants(result);
}

void multipleSourcesAndVariants()
{
    Project project;
    project.write("a.h", "#pragma once\n#include \"b.h\"\n");
    project.write("b.h", "#pragma once\n");
    project.write("extra.h", "#pragma once\n");
    project.write("main.cpp", "#include \"a.h\"\n#ifdef EXTRA\n#include \"extra.h\"\n#endif\n");
    project.write("second.cpp", "#include \"a.h\"\n");
    project.write("empty.cpp", "int empty;\n");
    project.database({project.command("main.cpp"), project.command("main.cpp", {"-DEXTRA"}),
                      project.command("second.cpp"), project.command("empty.cpp")});
    const auto result = project.scan();
    require(result.status == ScanStatus::complete, "Multi-TU analysis failed");
    require(result.edges.size() == 4, "Shared edges not deduplicated, or variant lost");
    require(hasEdge(result, project.root / "main.cpp", project.root / "extra.h", 3),
            "Compile-command variant not analyzed");
    require(std::find(result.files.begin(), result.files.end(), project.root / "empty.cpp")
            != result.files.end(), "Source with no includes lost");
    checkInvariants(result);
}

void missingHeader()
{
    Project project;
    project.write("a.h", "#pragma once\n");
    project.write("main.cpp", "#include \"a.h\"\n#include \"missing.h\"\n");
    project.database({project.command("main.cpp")});
    const auto result = project.scan();
    require(result.status == ScanStatus::partial, "Missing header must produce partial graph");
    require(result.edges.size() == 1, "Missing header produced a fictitious edge");
    const auto diagnostic = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& d) { return d.severity == DiagnosticSeverity::error &&
            d.message.find("missing.h") != std::string::npos; });
    require(diagnostic != result.diagnostics.end(), "Clang diagnostic lost");
    require(diagnostic->file == project.root / "main.cpp" && diagnostic->line == 2,
            "Clang diagnostic location lost");
    checkInvariants(result);
}

void warningsAndSyntaxErrors()
{
    Project project;
    project.write("main.cpp", "#warning scanner-warning\nint value;\n");
    project.database({project.command("main.cpp")});
    auto result = project.scan();
    require(result.status == ScanStatus::complete && !result.diagnostics.empty() &&
            result.diagnostics.front().severity == DiagnosticSeverity::warning,
            "Warning should not fail scan");
    project.write("main.cpp", "int = ;\n");
    result = project.scan();
    require(result.status == ScanStatus::partial && !result.diagnostics.empty(),
            "Syntax error should preserve the analyzed source and report error");
}

void systemHeaders()
{
    Project project;
    project.write("sys/system.h", "#pragma once\n#include \"nested.h\"\n");
    project.write("sys/nested.h", "#pragma once\n");
    project.write("main.cpp", "#include <system.h>\n");
    project.database({project.command("main.cpp", {"-isystem", "sys"})});
    const auto normal = project.scan();
    require(normal.status == ScanStatus::complete && normal.edges.empty() && normal.files.size() == 1,
            "System headers must be excluded by default");
    checkInvariants(normal);
}

void physicalLinesAndCycles()
{
    Project project;
    project.write("a.h", "#pragma once\n#include \"b.h\"\n");
    project.write("b.h", "#pragma once\n#include \"a.h\"\n");
    project.write("main.cpp", "#line 500 \"virtual.cpp\"\n#include \"a.h\"\n");
    project.database({project.command("main.cpp")});
    const auto result = project.scan();
    require(result.status == ScanStatus::complete && result.edges.size() == 3,
            "Guarded include cycle should remain visible");
    require(hasEdge(result, project.root / "main.cpp", project.root / "a.h", 2),
            "#line must not replace physical source location");
    require(hasEdge(result, project.root / "b.h", project.root / "a.h", 2),
            "Skipped include closing the cycle lost");
}

void commandString()
{
    Project project;
    project.write("main.cpp", "int value;\n");
    project.database({"{\"directory\":" + Project::quote(project.root.string()) +
        ",\"file\":\"main.cpp\",\"command\":\"clang++ -std=c++20 -c main.cpp -o main.o\"}"});
    const auto result = project.scan();
    require(result.status == ScanStatus::complete && result.files.size() == 1,
            "Compilation database command string not supported");
    require(!fs::exists(project.root / "main.o"), "Scanner must not generate object files");
}

void builtinHeaders()
{
    Project project;
    project.write("main.cpp", "#include <stddef.h>\nsize_t count = 0;\n");
    project.database({project.command("main.cpp")});
    const auto result = project.scan();
    if (result.status != ScanStatus::complete) {
        for (const auto& diagnostic : result.diagnostics)
            std::cerr << diagnostic.message << '\n';
    }
    require(result.status == ScanStatus::complete, "Clang builtin headers not found");
}

void failedInputs()
{
    Project project;
    const auto check = [](const ScanResult& result) {
        require(result.status == ScanStatus::failed && !result.diagnostics.empty() &&
                result.files.empty() && result.edges.empty(), "Invalid input must fail with diagnostic");
    };
    check(scanIncludes({}));
    check(project.scan());
    project.write("compile_commands.json", "not JSON");
    check(project.scan());
    project.database({});
    check(project.scan());
    project.database({project.command("missing.cpp")});
    check(project.scan());
}
} // namespace

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"demo baseline", [] { checkDemo(false); }},
        {"demo conditional -D", [] { checkDemo(true); }},
        {"relative paths, macros, duplicate lines", includePathsAndMacros},
        {"multiple TUs, variants, empty source", multipleSourcesAndVariants},
        {"missing header and diagnostic location", missingHeader},
        {"warnings and syntax diagnostics", warningsAndSyntaxErrors},
        {"system-header filtering", systemHeaders},
        {"physical lines and guarded cycles", physicalLinesAndCycles},
        {"command string and no compilation", commandString},
        {"Clang builtin resource headers", builtinHeaders},
        {"invalid database and missing source", failedInputs},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
