#include <algorithm>

#include <headerscope/export.hpp>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <stdexcept>

namespace {
void check(bool condition, int line) {
    if (!condition) {
        std::cerr << "Check failed at line " << line << '\n';
        std::exit(1);
    }
}
#define CHECK(value) check((value), __LINE__)
struct GroupedNumbers : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};
}
int main() {
    using namespace headerscope;
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute("project");
    const auto a = root / "a" / "same.h", b = root / "b" / "same.h";
    const auto dangerous = root / "x\"#<\n`.h";
    ScanResult scan{ScanStatus::partial, {a, dangerous}, {{a, b, 7}, {a, b, 3}, {a, b, 3}, {b, b, 1}, {b, a, 2}}, {}};
    const auto json = exportGraphJson(scan, root);
    CHECK(json.find("\"version\": 1") != std::string::npos);
    CHECK(json.find("\"status\": \"partial\"") != std::string::npos);
    CHECK(json.find("a/same.h") != std::string::npos && json.find("b/same.h") != std::string::npos);
    CHECK(json.find("[3, 7]") != std::string::npos);
    CHECK(json.find("\\\"#<\\u000a") != std::string::npos);
    const auto mermaid = exportGraphMermaid(scan, root);
    CHECK(mermaid.find("%% HeaderScope scan: partial\nflowchart LR") == 0);
    CHECK(mermaid.find("#34;#35;#60;#10;#96;") != std::string::npos);
    CHECK(mermaid.find("n1 --> n1") != std::string::npos);
    auto reordered = scan;
    std::reverse(reordered.files.begin(), reordered.files.end());
    std::reverse(reordered.edges.begin(), reordered.edges.end());
    CHECK(exportGraphJson(reordered, root) == json);
    CHECK(exportGraphMermaid(reordered, root) == mermaid);
    ScanResult many{ScanStatus::complete, {}, {}, {}};
    for (int i = 0; i < 1001; ++i) many.files.push_back(root / std::to_string(i));
    many.edges.push_back({many.files.front(), many.files.back(), 1024});
    const auto originalLocale = std::locale();
    std::locale::global(std::locale(originalLocale, new GroupedNumbers));
    const auto groupedJson = exportGraphJson(many, root);
    const auto groupedMermaid = exportGraphMermaid(many, root);
    std::locale::global(originalLocale);
    CHECK(groupedJson.find("[1024]") != std::string::npos);
    CHECK(groupedJson.find("\"n1000\"") != std::string::npos);
    CHECK(groupedMermaid.find("n1000[\"") != std::string::npos);
#ifndef _WIN32
    ScanResult invalid{ScanStatus::complete, {root / std::string("bad\xff", 4)}, {}, {}};
    try { (void)exportGraphJson(invalid, root); CHECK(false); } catch (const std::runtime_error&) {}
    try { (void)exportGraphMermaid(invalid, root); CHECK(false); } catch (const std::runtime_error&) {}
#endif
    scan.status = ScanStatus::failed;
    try { (void)exportGraphJson(scan, root); CHECK(false); } catch (const std::invalid_argument&) {}
}
