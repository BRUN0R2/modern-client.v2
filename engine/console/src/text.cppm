// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Names of the console language: cvars, commands and aliases share one case-insensitive
/// namespace, as in the original (`SV_GRAVITY 600` sets sv_gravity).
export module nx.console:text;

import std;
import nx.core;

export namespace nx::console {

/// Name of a cvar or command, in the classic style of the original (mp_roundtime, +jump).
class ConsoleName {
public:
    // Implicit, so that descriptors read `{.name = "sv_gravity"}`; names built at run time (plugins)
    // convert from std::string_view.
    constexpr ConsoleName(const char* text) noexcept : m_text{text} {}
    constexpr ConsoleName(std::string_view text) noexcept : m_text{text} {}

    [[nodiscard]] constexpr auto text() const noexcept -> std::string_view { return m_text; }

private:
    std::string_view m_text;
};

/// Whitespace of the console language: every byte up to the space, control characters included
/// (tabs, carriage returns of CRLF files).
[[nodiscard]] constexpr auto isWhitespace(char character) noexcept -> bool {
    return static_cast<unsigned char>(character) <= static_cast<unsigned char>(' ');
}

[[nodiscard]] constexpr auto equalsIgnoringCase(std::string_view a, std::string_view b) noexcept -> bool {
    return std::ranges::equal(a, b, [](char x, char y) { return toLowerAscii(x) == toLowerAscii(y); });
}

/// Whether `text` starts with `prefix`, ignoring ASCII case.
[[nodiscard]] constexpr auto startsWithIgnoringCase(std::string_view text, std::string_view prefix) noexcept
    -> bool {
    return text.size() >= prefix.size() && equalsIgnoringCase(text.substr(0, prefix.size()), prefix);
}

/// Whether `text` contains `part`, ignoring ASCII case. An empty `part` is in every text.
[[nodiscard]] constexpr auto containsIgnoringCase(std::string_view text, std::string_view part) noexcept
    -> bool {
    return !std::ranges::search(text, part, [](char x, char y) { return toLowerAscii(x) == toLowerAscii(y); })
                .empty() ||
           part.empty();
}

/// Orders names without regard to ASCII case; transparent, so maps keyed by std::string are
/// searched with a std::string_view.
struct CaseInsensitiveLess {
    using is_transparent = void;

    [[nodiscard]] constexpr auto operator()(std::string_view a, std::string_view b) const noexcept -> bool {
        return std::ranges::lexicographical_compare(
            a, b, [](char x, char y) { return toLowerAscii(x) < toLowerAscii(y); });
    }
};

/// A name the console language can invoke: not empty, no whitespace, no double quote or `;` (they
/// split and group words), and not starting with `//` (a comment).
[[nodiscard]] constexpr auto isValidName(std::string_view name) noexcept -> bool {
    return !name.empty() && !name.starts_with("//") &&
           std::ranges::none_of(name, [](char c) { return isWhitespace(c) || c == '"' || c == ';'; });
}

/// Edits (insertions, deletions, substitutions) that turn `a` into `b`, ignoring ASCII case: the
/// measure behind the "did you mean" suggestions.
[[nodiscard]] auto editDistance(std::string_view a, std::string_view b) -> std::size_t;

/// `text` without whitespace at either end.
[[nodiscard]] constexpr auto trimmed(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && isWhitespace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isWhitespace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace nx::console
