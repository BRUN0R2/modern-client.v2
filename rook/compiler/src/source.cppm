// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

export module rook.compiler:source;

import std;

export namespace rook::compiler {

/// A byte offset into a source text. A source is at most kMaximumSourceBytes long, so it always fits.
using ByteOffset = std::uint32_t;

/// The largest source the compiler reads. A longer one is a diagnostic, not a parse: no source is that
/// long by hand, and the limit bounds what a hostile one can make the compiler allocate.
inline constexpr std::size_t kMaximumSourceBytes = std::size_t{16} * 1024 * 1024;

/// A stretch of a source, in bytes: `start` is the first and `end` the one after the last.
struct TextRange {
    ByteOffset start = 0;
    ByteOffset end = 0;

    auto operator==(const TextRange&) const -> bool = default;
};

/// Where a byte is, as a person counts: both from 1, the column in characters and not in bytes.
struct TextPosition {
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    auto operator==(const TextPosition&) const -> bool = default;
};

/// The start of every line of a source. It keeps a view of the text, which must outlive it.
class LineMap {
public:
    explicit LineMap(std::string_view text);

    /// `offset` past the text's end reads as its end.
    [[nodiscard]] auto positionOf(ByteOffset offset) const -> TextPosition;

    /// The bytes of the line holding `offset`, without its line end.
    [[nodiscard]] auto lineAround(ByteOffset offset) const -> TextRange;

private:
    /// The index in `lineStarts` of the line holding `offset`.
    [[nodiscard]] auto lineIndexOf(ByteOffset offset) const -> std::size_t;

    std::string_view text;
    std::vector<ByteOffset> lineStarts;
};

} // namespace rook::compiler

namespace rook::compiler {

/// Whether `character` is a Unicode scalar value: a code point that is no surrogate.
[[nodiscard]] constexpr auto isScalarValue(char32_t character) noexcept -> bool {
    constexpr char32_t kFirstSurrogate = 0xD800;
    constexpr char32_t kLastSurrogate = 0xDFFF;
    constexpr char32_t kLastCodePoint = 0x10FFFF;
    return character <= kLastCodePoint && (character < kFirstSurrogate || character > kLastSurrogate);
}

/// One character read from UTF-8. Invalid bytes read as one character that is not valid, of one byte.
struct DecodedCharacter {
    char32_t codePoint = 0;
    ByteOffset length = 1;
    bool isValid = false;
};

/// Reads the character at `offset`, which must be inside `text`. Overlong forms, surrogates and values
/// past U+10FFFF are not valid, as the Unicode standard says.
[[nodiscard]] auto decodeCharacter(std::string_view text, ByteOffset offset) noexcept -> DecodedCharacter;

} // namespace rook::compiler
