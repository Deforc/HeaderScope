#include <headerscope/demo/fixture.hpp>

namespace headerscope::demo {

ScanResult makeIncludeChain(const std::filesystem::path& demoDirectory,
                            bool extraEnabled)
{
    const auto root = std::filesystem::absolute(demoDirectory).lexically_normal();
    const auto main = root / "main.cpp";
    const auto a = root / "a.h";
    const auto b = root / "b.h";

    ScanResult result;
    result.status = ScanStatus::complete;
    result.files = {main, a, b};
    result.edges = {{main, a, 1}, {a, b, 3}};

    if (extraEnabled) {
        const auto extra = root / "extra.h";
        result.files.push_back(extra);
        result.edges.push_back({a, extra, 6});
    }

    return result;
}

} // namespace headerscope::demo
