// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

constexpr char kLineFeed = '\n';
constexpr char kCarriageReturn = '\r';

constexpr unsigned kContinuationMask = 0xC0U;
constexpr unsigned kContinuationMark = 0x80U;
constexpr unsigned kContinuationValueMask = 0x3FU;
constexpr unsigned kContinuationBits = 6;

/// The forms of a UTF-8 sequence by its length: the bits of the first byte and the smallest value that
/// needs this length, under which it would be an overlong form.
struct SequenceForm {
    unsigned leadMask;
    unsigned leadMark;
    char32_t smallestValue;
    ByteOffset length;
};

constexpr std::array kSequenceForms{
    SequenceForm{.leadMask = 0xE0U, .leadMark = 0xC0U, .smallestValue = 0x80, .length = 2},
    SequenceForm{.leadMask = 0xF0U, .leadMark = 0xE0U, .smallestValue = 0x800, .length = 3},
    SequenceForm{.leadMask = 0xF8U, .leadMark = 0xF0U, .smallestValue = 0x10000, .length = 4},
};

constexpr unsigned kAsciiLimit = 0x80U;

[[nodiscard]] auto byteAt(std::string_view text, std::size_t offset) noexcept -> unsigned {
    return static_cast<unsigned char>(text[offset]);
}

} // namespace

LineMap::LineMap(std::string_view sourceText) : text{sourceText} {
    lineStarts.push_back(0);
    for (std::size_t offset = sourceText.find(kLineFeed); offset != std::string_view::npos;
         offset = sourceText.find(kLineFeed, offset + 1)) {
        lineStarts.push_back(static_cast<ByteOffset>(offset + 1));
    }
}

auto LineMap::lineIndexOf(ByteOffset offset) const -> std::size_t {
    const auto after = std::ranges::upper_bound(lineStarts, offset);
    return static_cast<std::size_t>(after - lineStarts.begin()) - 1;
}

auto LineMap::positionOf(ByteOffset offset) const -> TextPosition {
    const std::size_t lineIndex = lineIndexOf(offset);
    const auto target = std::min<std::size_t>(offset, text.size());
    std::uint32_t column = 1;
    for (std::size_t cursor = lineStarts[lineIndex]; cursor < target;) {
        cursor += decodeCharacter(text, static_cast<ByteOffset>(cursor)).length;
        ++column;
    }
    return TextPosition{.line = static_cast<std::uint32_t>(lineIndex + 1), .column = column};
}

auto LineMap::lineAround(ByteOffset offset) const -> TextRange {
    const std::size_t lineIndex = lineIndexOf(offset);
    const ByteOffset start = lineStarts[lineIndex];
    std::size_t end = lineIndex + 1 < lineStarts.size() ? lineStarts[lineIndex + 1] - 1 : text.size();
    if (end > start && text[end - 1] == kCarriageReturn) {
        --end;
    }
    return TextRange{.start = start, .end = static_cast<ByteOffset>(end)};
}

auto decodeCharacter(std::string_view text, ByteOffset offset) noexcept -> DecodedCharacter {
    const unsigned lead = byteAt(text, offset);
    if (lead < kAsciiLimit) {
        return DecodedCharacter{.codePoint = static_cast<char32_t>(lead), .length = 1, .isValid = true};
    }
    for (const SequenceForm& form : kSequenceForms) {
        if ((lead & form.leadMask) != form.leadMark) {
            continue;
        }
        if (offset + std::size_t{form.length} > text.size()) {
            return DecodedCharacter{};
        }
        auto value = static_cast<char32_t>(lead & ~form.leadMask);
        for (ByteOffset index = 1; index < form.length; ++index) {
            const unsigned byte = byteAt(text, std::size_t{offset} + index);
            if ((byte & kContinuationMask) != kContinuationMark) {
                return DecodedCharacter{};
            }
            value = (value << kContinuationBits) | static_cast<char32_t>(byte & kContinuationValueMask);
        }
        if (value < form.smallestValue || !isScalarValue(value)) {
            return DecodedCharacter{};
        }
        return DecodedCharacter{.codePoint = value, .length = form.length, .isValid = true};
    }
    return DecodedCharacter{};
}

} // namespace rook::compiler
