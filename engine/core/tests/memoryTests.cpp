// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

namespace {

constexpr std::size_t kArenaBytes = 256;

struct alignas(32) WideAligned {
    std::array<float, 8> lanes;
};

[[nodiscard]] auto isAligned(const void* pointer, std::size_t alignment) -> bool {
    return std::bit_cast<std::uintptr_t>(pointer) % alignment == 0;
}

} // namespace

TEST(FrameArena, HandsOutValueInitializedObjects) {
    nx::FrameArena arena{kArenaBytes};

    const auto values = arena.allocate<std::int32_t>(4);

    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->size(), 4U);
    EXPECT_TRUE(std::ranges::all_of(*values, [](std::int32_t value) { return value == 0; }));
    EXPECT_EQ(arena.used(), 4 * sizeof(std::int32_t));
}

TEST(FrameArena, RespectsTheAlignmentOfTheType) {
    nx::FrameArena arena{kArenaBytes};
    std::ignore = arena.allocate<std::uint8_t>(1);

    const auto wide = arena.allocate<WideAligned>(2);

    ASSERT_TRUE(wide.has_value());
    EXPECT_TRUE(isAligned(wide->data(), alignof(WideAligned)));
}

TEST(FrameArena, AllocationsDoNotOverlap) {
    nx::FrameArena arena{kArenaBytes};

    auto first = arena.allocate<std::uint8_t>(16).value();
    auto second = arena.allocate<std::uint8_t>(16).value();
    std::ranges::fill(first, std::uint8_t{0xAA});
    std::ranges::fill(second, std::uint8_t{0x55});

    EXPECT_TRUE(std::ranges::all_of(first, [](std::uint8_t value) { return value == 0xAA; }));
    EXPECT_GE(second.data(), first.data() + first.size());
}

TEST(FrameArena, ReportsExhaustionAsATypedFailure) {
    nx::FrameArena arena{kArenaBytes};

    const auto tooLarge = arena.allocate<std::uint64_t>(kArenaBytes);

    ASSERT_FALSE(tooLarge.has_value());
    EXPECT_EQ(tooLarge.error().code, nx::ArenaError::Exhausted);
    EXPECT_TRUE(tooLarge.error().detail.contains("256 of 256 bytes left"));
    EXPECT_EQ(arena.used(), 0U);
}

TEST(FrameArena, ResetReleasesEverything) {
    nx::FrameArena arena{kArenaBytes};
    std::ignore = arena.allocate<std::uint8_t>(kArenaBytes);
    ASSERT_EQ(arena.remaining(), 0U);

    arena.reset();

    EXPECT_EQ(arena.remaining(), kArenaBytes);
    EXPECT_TRUE(arena.allocate<std::uint8_t>(kArenaBytes).has_value());
}

TEST(FrameArena, EmptyRequestsAllocateNothing) {
    nx::FrameArena arena{kArenaBytes};

    EXPECT_TRUE(arena.allocate<std::uint32_t>(0).value().empty());
    EXPECT_TRUE(arena.allocateBytes(0, 1).value().empty());
    EXPECT_EQ(arena.used(), 0U);
}

TEST(FrameArena, RejectsAlignmentsThatAreNotPowersOfTwo) {
    const nx_test::ContractRecorder recorder;
    nx::FrameArena arena{kArenaBytes};

    const auto result = arena.allocateBytes(8, 3);

    EXPECT_EQ(recorder.count(), 1);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, nx::ArenaError::InvalidAlignment);
}

TEST(FrameArena, MovingLeavesTheSourceEmpty) {
    nx::FrameArena source{kArenaBytes};
    std::ignore = source.allocate<std::uint8_t>(10);

    nx::FrameArena target = std::move(source);

    EXPECT_EQ(target.capacity(), kArenaBytes);
    EXPECT_EQ(target.used(), 10U);
    EXPECT_EQ(source.capacity(), 0U); // NOLINT(bugprone-use-after-move): verifying the moved-from state
    EXPECT_FALSE(source.allocate<std::uint8_t>(1).has_value());
}
