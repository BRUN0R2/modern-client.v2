/// Helpers for source locations shown to people (failures, logs, contract reports).
export module nx.core:source;

import std;

export namespace nx {

/// File name of a source location without its directories: "C:/src/engine/bsp.cpp" -> "bsp.cpp".
[[nodiscard]] constexpr auto sourceFileName(const std::source_location& where) noexcept -> std::string_view {
    const std::string_view path = where.file_name();
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

} // namespace nx
