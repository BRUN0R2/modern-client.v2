// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Set of enumerators of a counted enum, stored as a bitset sized by Count
/// (e.g. nx::Flags<Button> for the buttons held in a user command).
export module nx.core:flags;

import std;
import :contracts;
import :introspection;

export namespace nx {

/// A set of the enumerators of a counted enum, one bit each, for any number of enumerators. A flag
/// outside [0, Count) is a contract violation.
template <CountedEnum Enum>
class Flags {
    using Word = std::uint64_t;

    static constexpr std::size_t kBitsPerWord = std::numeric_limits<Word>::digits;
    static constexpr std::size_t kWordCount = (enumCount<Enum>() + kBitsPerWord - 1) / kBitsPerWord;

public:
    static_assert(enumCount<Enum>() > 0, "Flags needs at least one enumerator before Count");

    constexpr Flags() noexcept = default;

    constexpr Flags(std::initializer_list<Enum> flags) noexcept {
        for (const auto flag : flags) {
            set(flag);
        }
    }

    /// Every enumerator before Count.
    [[nodiscard]] static constexpr auto all() noexcept -> Flags { return ~Flags{}; }

    constexpr auto set(Enum flag) noexcept -> Flags& {
        const auto [word, mask] = locate(flag);
        m_words[word] |= mask;
        return *this;
    }

    constexpr auto reset(Enum flag) noexcept -> Flags& {
        const auto [word, mask] = locate(flag);
        m_words[word] &= ~mask;
        return *this;
    }

    constexpr auto toggle(Enum flag) noexcept -> Flags& {
        const auto [word, mask] = locate(flag);
        m_words[word] ^= mask;
        return *this;
    }

    [[nodiscard]] constexpr auto test(Enum flag) const noexcept -> bool {
        const auto [word, mask] = locate(flag);
        return (m_words[word] & mask) != 0;
    }

    [[nodiscard]] constexpr auto any() const noexcept -> bool {
        return std::ranges::any_of(m_words, [](Word word) { return word != 0; });
    }

    [[nodiscard]] constexpr auto none() const noexcept -> bool { return !any(); }

    [[nodiscard]] constexpr auto count() const noexcept -> std::size_t {
        std::size_t total = 0;
        for (const auto word : m_words) {
            total += static_cast<std::size_t>(std::popcount(word));
        }
        return total;
    }

    [[nodiscard]] constexpr auto containsAll(Flags other) const noexcept -> bool {
        return (*this & other) == other;
    }
    [[nodiscard]] constexpr auto containsAny(Flags other) const noexcept -> bool {
        return (*this & other).any();
    }

    [[nodiscard]] friend constexpr auto operator|(Flags a, Flags b) noexcept -> Flags {
        return combine(a, b, [](Word x, Word y) { return x | y; });
    }

    [[nodiscard]] friend constexpr auto operator&(Flags a, Flags b) noexcept -> Flags {
        return combine(a, b, [](Word x, Word y) { return x & y; });
    }

    [[nodiscard]] friend constexpr auto operator^(Flags a, Flags b) noexcept -> Flags {
        return combine(a, b, [](Word x, Word y) { return x ^ y; });
    }

    /// Complement within the valid enumerators: bits at or after Count stay clear.
    [[nodiscard]] friend constexpr auto operator~(Flags flags) noexcept -> Flags {
        for (std::size_t word = 0; word < kWordCount; ++word) {
            flags.m_words[word] = ~flags.m_words[word] & validBits(word);
        }
        return flags;
    }

    [[nodiscard]] friend constexpr auto operator==(const Flags&, const Flags&) noexcept -> bool = default;

private:
    struct Location {
        std::size_t word;
        Word mask;
    };

    /// A flag outside [0, Count) is a contract violation. If the handler lets execution
    /// continue ("observe"), the operation targets no bit.
    [[nodiscard]] static constexpr auto locate(Enum flag) noexcept -> Location {
        const auto raw = std::to_underlying(flag);
        if (std::cmp_less(raw, 0) || std::cmp_greater_equal(raw, enumCount<Enum>())) {
            expects(false, "Flags: flag outside [0, Count)");
            return Location{.word = 0, .mask = 0};
        }
        const auto bit = static_cast<std::size_t>(raw);
        return Location{.word = bit / kBitsPerWord, .mask = Word{1} << (bit % kBitsPerWord)};
    }

    [[nodiscard]] static constexpr auto validBits(std::size_t word) noexcept -> Word {
        const auto bitsBefore = word * kBitsPerWord;
        const auto bitsInWord = std::min(kBitsPerWord, enumCount<Enum>() - bitsBefore);
        return bitsInWord == kBitsPerWord ? ~Word{0} : (Word{1} << bitsInWord) - 1;
    }

    template <class Operation>
    [[nodiscard]] static constexpr auto combine(Flags a, Flags b, Operation operation) noexcept -> Flags {
        for (std::size_t word = 0; word < kWordCount; ++word) {
            a.m_words[word] = operation(a.m_words[word], b.m_words[word]);
        }
        return a;
    }

    std::array<Word, kWordCount> m_words{};
};

} // namespace nx
