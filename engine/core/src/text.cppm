// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Text shared by the modules that read values from text and describe them in diagnostics: ASCII
/// case and the wording of numeric bounds.
export module nx.core:text;

import std;

export namespace nx {

/// `character` in lowercase when it is an ASCII capital letter; any other byte unchanged.
[[nodiscard]] constexpr auto toLowerAscii(char character) noexcept -> char {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

/// How diagnostics word inclusive bounds: "in [0, 100]", "of at least 0" or "of at most 100"; empty
/// when there is neither bound.
template <class Number>
    requires std::is_arithmetic_v<Number>
[[nodiscard]] auto boundsText(std::optional<Number> minimum, std::optional<Number> maximum) -> std::string {
    if (minimum && maximum) {
        return std::format("in [{}, {}]", *minimum, *maximum);
    }
    if (minimum) {
        return std::format("of at least {}", *minimum);
    }
    if (maximum) {
        return std::format("of at most {}", *maximum);
    }
    return {};
}

} // namespace nx
