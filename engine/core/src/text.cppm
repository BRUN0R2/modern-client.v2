// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Text shared by the modules that read values from text and describe them in diagnostics: ASCII
/// case and the wording of numeric bounds; and UTF-8, the encoding of all the engine's text.
export module nx.core:text;

import std;
import :contracts;

export namespace nx {

/// `character` in lowercase when it is an ASCII capital letter; any other byte unchanged.
[[nodiscard]] constexpr auto toLowerAscii(char character) noexcept -> char {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

/// The ASCII characters, from 0 to 127; DEL, the last, a control character.
inline constexpr std::size_t kAsciiCount = 128;
inline constexpr char kAsciiDelete = '\x7F';

/// Whether `character` is an ASCII control byte: below the space, or DEL.
[[nodiscard]] constexpr auto isAsciiControl(char character) noexcept -> bool {
    return static_cast<unsigned char>(character) < static_cast<unsigned char>(' ') ||
           character == kAsciiDelete;
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

/// The code units UTF-16 writes a character past the first plane with: a high surrogate, then a low
/// one. Neither is a character alone.
inline constexpr char32_t kFirstHighSurrogate = 0xD800;
inline constexpr char32_t kFirstLowSurrogate = 0xDC00;
inline constexpr char32_t kSurrogatesEnd = 0xE000;

[[nodiscard]] constexpr auto isHighSurrogate(char32_t unit) noexcept -> bool {
    return unit >= kFirstHighSurrogate && unit < kFirstLowSurrogate;
}
[[nodiscard]] constexpr auto isLowSurrogate(char32_t unit) noexcept -> bool {
    return unit >= kFirstLowSurrogate && unit < kSurrogatesEnd;
}
[[nodiscard]] constexpr auto isSurrogate(char32_t unit) noexcept -> bool {
    return unit >= kFirstHighSurrogate && unit < kSurrogatesEnd;
}

/// The character the surrogates `high` and `low` write: past the first plane, each of them giving
/// ten of its bits.
[[nodiscard]] constexpr auto characterOfSurrogates(char32_t high, char32_t low) noexcept -> char32_t {
    constexpr char32_t kFirstPastFirstPlane = 0x10000;
    constexpr unsigned kBitsOfEach = 10;
    expects(isHighSurrogate(high) && isLowSurrogate(low),
            "characterOfSurrogates: a high, then a low surrogate");
    return kFirstPastFirstPlane + ((high - kFirstHighSurrogate) << kBitsOfEach) + (low - kFirstLowSurrogate);
}

/// Appends to `text` the UTF-8 of `character`, a Unicode scalar value (not a surrogate, at most
/// U+10FFFF).
void appendUtf8(std::string& text, char32_t character);

/// The characters of the UTF-8 `text`. What is not UTF-8 reads as kReplacementCharacter, one for each
/// maximal part of a sequence that could have begun one, as the Unicode Standard recommends
/// (section 3.9): a stray byte, a sequence cut short, an overlong form, a surrogate, a value past
/// U+10FFFF.
[[nodiscard]] auto decodeUtf8(std::string_view text) -> std::u32string;

} // namespace nx
