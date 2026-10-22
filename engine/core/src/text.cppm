// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Text shared by the modules that read values from text and describe them in diagnostics: ASCII
/// case and the wording of numeric bounds; and UTF-8, the encoding of all the engine's text.
export module nx.core:text;

import std;

export namespace nx {

/// `character` in lowercase when it is an ASCII capital letter; any other byte unchanged.
[[nodiscard]] constexpr auto toLowerAscii(char character) noexcept -> char {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

/// Whether `character` is an ASCII control byte: below the space, or DEL.
[[nodiscard]] constexpr auto isAsciiControl(char character) noexcept -> bool {
    constexpr unsigned char kFirstPrintable = 0x20; // the space
    constexpr unsigned char kDelete = 0x7F;
    const auto byte = static_cast<unsigned char>(character);
    return byte < kFirstPrintable || byte == kDelete;
}

/// Whether `a` and `b` are the same text, ignoring ASCII case.
[[nodiscard]] constexpr auto equalsIgnoringCase(std::string_view a, std::string_view b) noexcept -> bool {
    return std::ranges::equal(a, b, [](char x, char y) { return toLowerAscii(x) == toLowerAscii(y); });
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

/// What a character of text that is not UTF-8 reads as: U+FFFD REPLACEMENT CHARACTER.
inline constexpr char32_t kReplacementCharacter = U'\uFFFD';

/// Appends to `text` the UTF-8 of `character`, a Unicode scalar value (not a surrogate, at most
/// U+10FFFF).
void appendUtf8(std::string& text, char32_t character);

/// The characters of the UTF-8 `text`. What is not UTF-8 reads as kReplacementCharacter, one for each
/// maximal part of a sequence that could have begun one, as the Unicode Standard recommends
/// (section 3.9): a stray byte, a sequence cut short, an overlong form, a surrogate, a value past
/// U+10FFFF.
[[nodiscard]] auto decodeUtf8(std::string_view text) -> std::u32string;

} // namespace nx
