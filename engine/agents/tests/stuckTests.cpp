// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.math;
import nx.agents;
#include "testAgents.hpp"

namespace {
constexpr nx::Seconds kTick{0.01F};
/// A running player's full speed.
constexpr nx::UnitsPerSecond kSpeed{250.0F};

auto watch(nx::agents::StuckRules rules = nx::test::stuckRules()) -> nx::agents::StuckWatch {
    return nx::test::stuckWatch(rules);
}
/// An intent along +x, as a decision moving that way makes it.
auto alongX() -> nx::agents::Intent {
    return {.direction = {.x = 1.0F}, .speedShare = 0.5F, .ahead = {.x = 1.0F}};
}
/// How many ticks `watched`, its body's feet held at `feet`, takes to be stuck, at most `most`.
auto ticksToStuck(nx::agents::StuckWatch& watched, nx::Vec3 feet, int most) -> int {
    for (int tick = 0; tick < most; ++tick) {
        nx::agents::Intent intent = alongX();
        if (watched.watch(intent, feet, kSpeed, true, kTick) == nx::agents::Progress::Stuck) {
            return tick;
        }
    }
    return most;
}

TEST(StuckRules, AreValidWithPositiveTimesSpeedsAndDistancesAndOrderedRanges) {
    EXPECT_TRUE(nx::agents::isValid(nx::test::stuckRules()));
    auto rules = nx::test::stuckRules();
    rules.samples = 0;
    EXPECT_FALSE(nx::agents::isValid(rules));
    rules = nx::test::stuckRules();
    rules.wiggle.shortest = nx::Seconds{2.0F};
    EXPECT_FALSE(nx::agents::isValid(rules)) << "a wiggle's shortest no longer than its longest";
    rules = nx::test::stuckRules();
    rules.firstJump = nx::Seconds{};
    EXPECT_TRUE(nx::agents::isValid(rules)) << "a first jump at once";
    rules.firstJump = nx::Seconds{-1.0F};
    EXPECT_FALSE(nx::agents::isValid(rules));
    rules = nx::test::stuckRules();
    rules.stuckSpeed = nx::UnitsPerSecond{std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(nx::agents::isValid(rules));
}

TEST(StuckWatch, ABodyMakingItsWayIsNotStuckAndItsIntentIsLeftAsItIs) {
    auto watched = watch();
    nx::Vec3 feet{};
    for (int tick = 0; tick < 1000; ++tick) {
        nx::agents::Intent intent = alongX();
        ASSERT_EQ(watched.watch(intent, feet, kSpeed, true, kTick), nx::agents::Progress::Moving)
            << "tick " << tick;
        EXPECT_EQ(intent.direction, alongX().direction);
        EXPECT_EQ(intent.speedShare, alongX().speedShare);
        EXPECT_FALSE(intent.jumps);
        feet.x += 2.5F; // 250 units a second
    }
    EXPECT_FALSE(watched.isStuck());
}

TEST(StuckWatch, ABodyThatStaysIsStuckOnceItsSamplesSpanTheirTime) {
    auto watched = watch();
    const int ticks = ticksToStuck(watched, {}, 1000);
    // Five intervals of 0.1 s after its first sample, give or take a tick of the sums of ticks.
    EXPECT_GE(ticks, 49);
    EXPECT_LE(ticks, 51);
    EXPECT_TRUE(watched.isStuck());
}

TEST(StuckWatch, ABodyGoingBackAndForthAtFullSpeedIsStuckToo) {
    // At 250 units a second, four units either way of where it is, as a body bouncing in a doorway: fast,
    // and no way made.
    constexpr float kStep = 2.5F;
    constexpr float kReach = 4.0F;
    auto watched = watch();
    float y = 0.0F;
    float step = kStep;
    int ticks = 0;
    for (; ticks < 1000; ++ticks) {
        nx::agents::Intent intent = alongX();
        if (watched.watch(intent, {.y = y}, kSpeed, true, kTick) == nx::agents::Progress::Stuck) {
            break;
        }
        if (std::abs(y + step) > kReach) {
            step = -step;
        }
        y += step;
    }
    EXPECT_GE(ticks, 49);
    EXPECT_LE(ticks, 51) << "as soon as a body that stays";
}

TEST(StuckWatch, StuckItWigglesAheadBackOrAsideAtFullSpeedAndJumps) {
    auto rules = nx::test::stuckRules();
    rules.giveUpTime = nx::Seconds{60.0F};
    auto watched = watch(rules);
    ASSERT_LT(ticksToStuck(watched, {}, 1000), 1000);
    const std::array ways{nx::Vec3{.x = 1.0F}, nx::Vec3{.x = -1.0F}, nx::Vec3{.y = 1.0F},
                          nx::Vec3{.y = -1.0F}};
    std::array<int, 4> taken{};
    std::vector<float> jumps;
    for (int tick = 1; tick <= 3000; ++tick) {
        nx::agents::Intent intent = alongX();
        ASSERT_EQ(watched.watch(intent, {}, kSpeed, true, kTick), nx::agents::Progress::Stuck);
        const auto way = std::ranges::find(ways, intent.direction);
        ASSERT_NE(way, ways.end()) << "ahead, back, left or right of where it goes";
        ++taken[static_cast<std::size_t>(way - ways.begin())];
        EXPECT_EQ(intent.speedShare, 1.0F);
        if (intent.jumps) {
            jumps.push_back(static_cast<float>(tick) * kTick.count());
        }
    }
    EXPECT_TRUE(std::ranges::all_of(taken, [](int count) { return count > 0; })) << "each way, drawn";
    ASSERT_FALSE(jumps.empty());
    EXPECT_LE(jumps.front(), 0.5F + kTick.count()) << "its first jump soon";
    for (std::size_t jump = 1; jump < jumps.size(); ++jump) {
        const float between = jumps[jump] - jumps[jump - 1];
        EXPECT_GE(between, 1.0F - kTick.count());
        EXPECT_LE(between, 2.0F + kTick.count());
    }
}

TEST(StuckWatch, ItIsUnstuckOnceItsBodyIsFarEnoughFromWhereItGotStuck) {
    auto watched = watch();
    ASSERT_LT(ticksToStuck(watched, {}, 1000), 1000);
    nx::agents::Intent intent = alongX();
    EXPECT_EQ(watched.watch(intent, {.x = 74.0F}, kSpeed, true, kTick), nx::agents::Progress::Stuck);
    intent = alongX();
    EXPECT_EQ(watched.watch(intent, {.x = 76.0F}, kSpeed, true, kTick), nx::agents::Progress::Moving);
    EXPECT_FALSE(watched.isStuck());
    EXPECT_EQ(intent.direction, alongX().direction) << "its way, as it decided it";
}

TEST(StuckWatch, StuckTooLongItGivesUpOnceAndWatchesAnew) {
    auto watched = watch();
    const int stuck = ticksToStuck(watched, {}, 1000);
    int ticks = 0;
    nx::agents::Progress progress = nx::agents::Progress::Stuck;
    for (; ticks < 1000 && progress == nx::agents::Progress::Stuck; ++ticks) {
        nx::agents::Intent intent = alongX();
        progress = watched.watch(intent, {}, kSpeed, true, kTick);
    }
    EXPECT_EQ(progress, nx::agents::Progress::GivesUp);
    EXPECT_NEAR(static_cast<float>(ticks) * kTick.count(), 4.5F, 2.0F * kTick.count()) << "after " << stuck;
    EXPECT_FALSE(watched.isStuck());
    EXPECT_GE(ticksToStuck(watched, {}, 1000), 49) << "its samples taken anew";
}

TEST(StuckWatch, NotMeaningToMoveItForgetsHowItMoved) {
    auto watched = watch();
    ASSERT_LT(ticksToStuck(watched, {}, 1000), 1000);
    nx::agents::Intent intent{};
    EXPECT_EQ(watched.watch(intent, {}, kSpeed, false, kTick), nx::agents::Progress::Moving);
    EXPECT_FALSE(watched.isStuck());
    EXPECT_EQ(intent.direction, nx::Vec3{});
    EXPECT_GE(ticksToStuck(watched, {}, 1000), 49);
}

TEST(StuckWatch, ABodyNoFasterThanTheStuckSpeedIsNotWatched) {
    auto watched = watch();
    for (int tick = 0; tick < 1000; ++tick) {
        nx::agents::Intent intent = alongX();
        ASSERT_EQ(watched.watch(intent, {}, nx::UnitsPerSecond{1.0F}, true, kTick),
                  nx::agents::Progress::Moving)
            << "a frozen round's player, at " << tick;
        EXPECT_FALSE(intent.jumps);
    }
    EXPECT_FALSE(watched.isStuck());
}

TEST(StuckWatch, TheSameForTheSameInputsAndDraws) {
    auto first = watch();
    auto second = watch();
    for (int tick = 0; tick < 600; ++tick) {
        nx::agents::Intent one = alongX();
        nx::agents::Intent other = alongX();
        ASSERT_EQ(first.watch(one, {}, kSpeed, true, kTick), second.watch(other, {}, kSpeed, true, kTick));
        ASSERT_EQ(one.direction, other.direction);
        ASSERT_EQ(one.jumps, other.jumps);
    }
}

} // namespace
