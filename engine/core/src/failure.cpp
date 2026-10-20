module nx.core;

import std;

namespace nx::detail {

namespace {

/// "C:/src/engine/assets/bsp.cpp" -> "bsp.cpp": full paths add noise to every message.
[[nodiscard]] auto fileNameOf(std::string_view path) noexcept -> std::string_view {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

} // namespace

void appendFailureLine(std::string& out, std::string_view domain, std::string_view code, std::int64_t value,
                       std::string_view context, const std::source_location& where) {
    auto output = std::back_inserter(out);
    if (code.empty()) {
        std::format_to(output, "{}::#{}", domain, value);
    } else {
        std::format_to(output, "{}::{}", domain, code);
    }
    if (!context.empty()) {
        std::format_to(output, ": {}", context);
    }
    std::format_to(output, " [{}:{}]", fileNameOf(where.file_name()), where.line());
}

} // namespace nx::detail
