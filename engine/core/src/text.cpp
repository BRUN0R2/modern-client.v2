// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.core;

import std;

namespace nx {

namespace {

/// The bits a continuation byte carries, and the mark of one (10xxxxxx).
constexpr std::uint32_t kContinuationBits = 6U;
constexpr std::uint32_t kContinuationMask = 0x3FU;
constexpr std::uint32_t kContinuationMark = 0x80U;
constexpr std::uint8_t kLowestContinuation = 0x80;
constexpr std::uint8_t kHighestContinuation = 0xBF;
constexpr char32_t kLastScalar = 0x10FFFF;
constexpr char32_t kFirstSurrogate = 0xD800;
constexpr char32_t kLastSurrogate = 0xDFFF;

/// How a well-formed sequence that starts with a byte goes on (the Unicode Standard, table 3-7): its
/// length, and the range of its second byte, which rules out overlong forms, surrogates and values
/// past U+10FFFF.
struct SequenceStart {
    std::size_t length = 0; ///< 0 for a byte that starts none.
    std::uint8_t lowest = kLowestContinuation;
    std::uint8_t highest = kHighestContinuation;
};

[[nodiscard]] constexpr auto sequenceStartedBy(std::uint8_t lead) noexcept -> SequenceStart {
    if (lead >= 0xC2 && lead <= 0xDF) {
        return {.length = 2};
    }
    if (lead == 0xE0) {
        return {.length = 3, .lowest = 0xA0};
    }
    if (lead == 0xED) {
        return {.length = 3, .highest = 0x9F};
    }
    if (lead >= 0xE1 && lead <= 0xEF) {
        return {.length = 3};
    }
    if (lead == 0xF0) {
        return {.length = 4, .lowest = 0x90};
    }
    if (lead == 0xF4) {
        return {.length = 4, .highest = 0x8F};
    }
    if (lead >= 0xF1 && lead <= 0xF3) {
        return {.length = 4};
    }
    return {};
}

} // namespace

void appendUtf8(std::string& text, char32_t character) {
    expects(character <= kLastScalar && (character < kFirstSurrogate || character > kLastSurrogate),
            "appendUtf8: a Unicode scalar value");
    const auto code = static_cast<std::uint32_t>(character);
    const auto put = [&text](std::uint32_t value) {
        text += static_cast<char>(static_cast<std::uint8_t>(value));
    };
    const auto continuation = [](std::uint32_t value, std::uint32_t shift) {
        return kContinuationMark | ((value >> shift) & kContinuationMask);
    };
    if (code < 0x80U) {
        put(code);
    } else if (code < 0x800U) {
        put(0xC0U | (code >> kContinuationBits));
        put(continuation(code, 0));
    } else if (code < 0x10000U) {
        put(0xE0U | (code >> (2 * kContinuationBits)));
        put(continuation(code, kContinuationBits));
        put(continuation(code, 0));
    } else {
        put(0xF0U | (code >> (3 * kContinuationBits)));
        put(continuation(code, 2 * kContinuationBits));
        put(continuation(code, kContinuationBits));
        put(continuation(code, 0));
    }
}

auto decodeUtf8(std::string_view text) -> std::u32string {
    const auto byteAt = [text](std::size_t index) {
        return static_cast<std::uint8_t>(text[index]);
    };
    std::u32string decoded;
    decoded.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::uint8_t lead = byteAt(at);
        if (lead < kLowestContinuation) {
            decoded += static_cast<char32_t>(lead);
            ++at;
            continue;
        }
        const SequenceStart start = sequenceStartedBy(lead);
        if (start.length == 0) {
            decoded += kReplacementCharacter; // a byte that starts no sequence
            ++at;
            continue;
        }
        // The lead's bits, then each continuation's, as long as each is one this sequence can have;
        // a sequence cut short reads as one replacement, and the byte that cut it starts anew.
        std::uint32_t code = lead & (0x7FU >> start.length);
        std::size_t next = at + 1;
        bool isWhole = true;
        for (std::size_t index = 1; index < start.length; ++index, ++next) {
            const std::uint8_t lowest = index == 1 ? start.lowest : kLowestContinuation;
            const std::uint8_t highest = index == 1 ? start.highest : kHighestContinuation;
            if (next >= text.size() || byteAt(next) < lowest || byteAt(next) > highest) {
                isWhole = false;
                break;
            }
            code = (code << kContinuationBits) | (byteAt(next) & kContinuationMask);
        }
        decoded += isWhole ? static_cast<char32_t>(code) : kReplacementCharacter;
        at = next;
    }
    return decoded;
}

} // namespace nx
