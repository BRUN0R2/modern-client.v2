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
/// The bits of a byte below the continuations: an ASCII character's, and those a lead byte's length
/// leaves (`kAsciiBits >> length`).
constexpr std::uint32_t kAsciiBits = 0x7FU;

/// How a well-formed sequence that starts with a byte goes on: its length, and the range of its
/// second byte, which rules out overlong forms, surrogates and values past U+10FFFF.
struct SequenceStart {
    std::size_t length = 0; ///< 0 for a byte that starts none.
    std::uint8_t lowest = kLowestContinuation;
    std::uint8_t highest = kHighestContinuation;
};

/// The lead bytes of well-formed sequences, from `first` to `last`, and how theirs go on: the
/// Unicode Standard's table 3-7, row by row.
struct LeadBytes {
    std::uint8_t first = 0;
    std::uint8_t last = 0;
    SequenceStart start{};
};
constexpr std::array kWellFormedLeads{
    LeadBytes{.first = 0xC2, .last = 0xDF, .start = {.length = 2}},
    LeadBytes{.first = 0xE0, .last = 0xE0, .start = {.length = 3, .lowest = 0xA0}},
    LeadBytes{.first = 0xE1, .last = 0xEC, .start = {.length = 3}},
    LeadBytes{.first = 0xED, .last = 0xED, .start = {.length = 3, .highest = 0x9F}},
    LeadBytes{.first = 0xEE, .last = 0xEF, .start = {.length = 3}},
    LeadBytes{.first = 0xF0, .last = 0xF0, .start = {.length = 4, .lowest = 0x90}},
    LeadBytes{.first = 0xF1, .last = 0xF3, .start = {.length = 4}},
    LeadBytes{.first = 0xF4, .last = 0xF4, .start = {.length = 4, .highest = 0x8F}},
};

[[nodiscard]] constexpr auto sequenceStartedBy(std::uint8_t lead) noexcept -> SequenceStart {
    for (const LeadBytes& leads : kWellFormedLeads) {
        if (lead >= leads.first && lead <= leads.last) {
            return leads.start;
        }
    }
    return {};
}

/// The characters below `limit` that UTF-8 writes in a sequence whose lead byte has `mark`, from one
/// byte to four (the Unicode Standard, table 3-6).
struct EncodedLength {
    std::uint32_t limit = 0;
    std::uint32_t mark = 0;
};
constexpr std::array kEncodedLengths{
    EncodedLength{.limit = 0x80U, .mark = 0x00U},
    EncodedLength{.limit = 0x800U, .mark = 0xC0U},
    EncodedLength{.limit = 0x10000U, .mark = 0xE0U},
    EncodedLength{.limit = kLastScalar + 1, .mark = 0xF0U},
};

} // namespace

void appendUtf8(std::string& text, char32_t character) {
    expects(character <= kLastScalar && !isSurrogate(character), "appendUtf8: a Unicode scalar value");
    const auto code = static_cast<std::uint32_t>(character);
    const auto put = [&text](std::uint32_t value) {
        text += static_cast<char>(static_cast<std::uint8_t>(value));
    };
    // A character past the last limit, after a violation under "observe", is written in four bytes.
    std::uint32_t length = 0;
    while (length + 1 < kEncodedLengths.size() && code >= kEncodedLengths.at(length).limit) {
        ++length;
    }
    // The lead carries the highest bits after its mark, each continuation the next six.
    put(kEncodedLengths.at(length).mark | (code >> (length * kContinuationBits)));
    for (std::uint32_t continuation = length; continuation > 0; --continuation) {
        put(kContinuationMark | ((code >> ((continuation - 1) * kContinuationBits)) & kContinuationMask));
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
        std::uint32_t code = lead & (kAsciiBits >> start.length);
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
