// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

[[nodiscard]] constexpr auto isSpace(char character) noexcept -> bool {
    return static_cast<unsigned char>(character) <= static_cast<unsigned char>(' ');
}

} // namespace

Words::Words(std::string_view text) noexcept : rest{text} {}

auto Words::next() noexcept -> std::optional<std::string_view> {
    for (;;) {
        const auto start = std::ranges::find_if_not(rest, isSpace);
        rest = rest.substr(narrow<std::size_t>(start - rest.begin()));
        if (!rest.starts_with("//")) {
            break;
        }
        const std::size_t lineEnd = rest.find('\n');
        rest = lineEnd == std::string_view::npos ? std::string_view{} : rest.substr(lineEnd);
    }
    if (rest.empty()) {
        return std::nullopt;
    }
    if (rest.front() == '"') {
        const std::size_t close = rest.find('"', 1);
        const std::string_view word = rest.substr(1, close == std::string_view::npos ? close : close - 1);
        rest = close == std::string_view::npos ? std::string_view{} : rest.substr(close + 1);
        return word;
    }
    const auto end = std::ranges::find_if(rest, isSpace);
    const auto length = narrow<std::size_t>(end - rest.begin());
    const std::string_view word = rest.substr(0, length);
    rest = rest.substr(length);
    return word;
}

auto wholeNumberOf(std::string_view word) noexcept -> std::optional<std::uint32_t> {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    return error == std::errc{} && end == word.data() + word.size() ? std::optional{value} : std::nullopt;
}

} // namespace nx::assets
