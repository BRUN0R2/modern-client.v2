// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Random streams: SplitMix64's published numbers, the same numbers for the same seed and stream,
// and the ranges and evenness of units and whole numbers.

#include <gtest/gtest.h>

import std;
import nx.core;

namespace {

/// A stream whose state starts at 0, the published sequence's: the seed cancels the empty name's hash.
[[nodiscard]] constexpr auto zeroState() noexcept -> nx::Random {
    return nx::Random{nx::hash64(""), ""};
}

/// The first numbers SplitMix64 draws from a state of 0.
constexpr std::array<std::uint64_t, 4> kPublished{0xE220'A839'7B1D'CDAFULL, 0x6E78'9E6A'A1B9'65F4ULL,
                                                  0x06C4'5D18'8009'454FULL, 0xF88B'B8A8'724C'81ECULL};

} // namespace

TEST(Random, DrawsSplitMix64sNumbers) {
    nx::Random random = zeroState();
    for (const std::uint64_t expected : kPublished) {
        EXPECT_EQ(random.nextBits(), expected);
    }
    static_assert([] {
        nx::Random constant = zeroState();
        return constant.nextBits();
    }() == kPublished[0]);
}

TEST(Random, TheSameSeedAndStreamDrawTheSame) {
    nx::Random first{42, "spread"};
    nx::Random again{42, "spread"};
    nx::Random otherStream{42, "weaponAnimation"};
    nx::Random otherSeed{43, "spread"};
    const std::uint64_t drawn = first.nextBits();
    EXPECT_EQ(again.nextBits(), drawn);
    EXPECT_NE(otherStream.nextBits(), drawn);
    EXPECT_NE(otherSeed.nextBits(), drawn);
}

TEST(Random, UnitsAreFromZeroUpToOne) {
    nx::Random random{7, "units"};
    double sum = 0.0;
    constexpr int kDraws = 10'000;
    for (int draw = 0; draw < kDraws; ++draw) {
        const double unit = random.nextUnit();
        ASSERT_GE(unit, 0.0);
        ASSERT_LT(unit, 1.0);
        sum += unit;
    }
    EXPECT_NEAR(sum / kDraws, 0.5, 0.02);
}

TEST(Random, WholeNumbersBelowACountAreEachAsLikely) {
    nx::Random random{7, "whole"};
    EXPECT_EQ(random.below(0), 0U);
    EXPECT_EQ(random.below(1), 0U);
    constexpr std::uint64_t kFaces = 6;
    constexpr int kDraws = 60'000;
    std::array<int, kFaces> counts{};
    for (int draw = 0; draw < kDraws; ++draw) {
        const std::uint64_t face = random.below(kFaces);
        ASSERT_LT(face, kFaces);
        ++counts[face];
    }
    for (const int count : counts) {
        EXPECT_NEAR(count, kDraws / static_cast<int>(kFaces), kDraws / 100);
    }
}
