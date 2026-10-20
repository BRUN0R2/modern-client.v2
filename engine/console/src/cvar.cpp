// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

namespace {

/// `text` without surrounding whitespace and without one leading '+' ("+5" is 5, as atof reads it).
[[nodiscard]] auto numberText(std::string_view text) noexcept -> std::string_view {
    text = trimmed(text);
    if (text.starts_with('+') && !text.substr(1).starts_with('-')) {
        text.remove_prefix(1);
    }
    return text;
}

/// Parses the whole of `text` as a `Number`; nullopt if anything is left over.
template <class Number>
[[nodiscard]] auto parseWhole(std::string_view text) noexcept -> std::optional<Number> {
    Number value{};
    const char* const end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace

auto detail::parseBool(std::string_view text) noexcept -> std::optional<bool> {
    const std::string_view value = trimmed(text);
    if (value == "0") {
        return false;
    }
    if (value == "1") {
        return true;
    }
    return std::nullopt;
}

auto detail::parseInteger(std::string_view text) noexcept -> std::optional<std::int32_t> {
    const std::string_view number = numberText(text);
    if (const auto whole = parseWhole<std::int32_t>(number)) {
        return whole;
    }
    // A whole number written as a real one ("10.0", "1e3"), as configs of the original often do.
    const auto real = parseWhole<double>(number);
    if (!real || !std::isfinite(*real) || std::trunc(*real) != *real ||
        *real < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        *real > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*real);
}

auto detail::parseFloat(std::string_view text) noexcept -> std::optional<float> {
    const auto value = parseWhole<float>(numberText(text));
    if (!value || !std::isfinite(*value)) {
        return std::nullopt;
    }
    return value;
}

auto detail::isWritableText(std::string_view text) noexcept -> bool {
    return std::ranges::none_of(text, [](char c) { return c == '"' || c == '\n' || c == '\r'; });
}

} // namespace nx::console
