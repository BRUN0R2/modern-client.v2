// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The step indicator: the way a body heard is from where the player looks, how loud a step shows by its
// distance, how a mark lights, holds and fades by the time since its steps, hud_stepindicator, and the
// bodies heard, each one kept once, the faintest giving way, gone once no longer lit.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.console;
import nx.render;
import cs.shared;
import cs.client;

#include "consoleFixtures.hpp"

namespace {

constexpr float kTolerance = 1e-4F;
constexpr float kHalfTurn = std::numbers::pi_v<float>;

/// A look of round numbers: wholly loud within 100 units, none at 1100; lit over a tenth of a second,
/// holding half a second, fading out over 0.4; at most 2 marks and 3 bodies.
[[nodiscard]] auto roundLook() -> cs::StepIndicatorLook {
    return {.color = {.red = 255, .green = 70, .blue = 50},
            .opacity = 1.0F,
            .ringRadius = 0.17F,
            .size = 0.06F,
            .nearDistance = nx::HammerUnits{100},
            .farDistance = nx::HammerUnits{1100},
            .attack = nx::Seconds{0.1F},
            .hold = nx::Seconds{0.5F},
            .release = nx::Seconds{0.4F},
            .pulseScale = 0.25F,
            .pulseTime = nx::Seconds{0.25F},
            .separation = 0.8F,
            .mostShown = 2,
            .mostHeard = 3};
}

TEST(StepIndicatorTest, ABodyIsAheadToTheRightOrBehindAsItIsFromWhereThePlayerLooks) {
    const nx::Vec3 listener{.x = 100.0F, .y = 100.0F};
    // Looking along +x: +y is to the left.
    EXPECT_NEAR(cs::bearingOf(listener, nx::Degrees{0}, {.x = 500.0F, .y = 100.0F}), 0.0F, kTolerance);
    EXPECT_NEAR(cs::bearingOf(listener, nx::Degrees{0}, {.x = 100.0F, .y = -300.0F}), kHalfTurn * 0.5F,
                kTolerance);
    EXPECT_NEAR(cs::bearingOf(listener, nx::Degrees{0}, {.x = 100.0F, .y = 500.0F}), -kHalfTurn * 0.5F,
                kTolerance);
    EXPECT_NEAR(std::abs(cs::bearingOf(listener, nx::Degrees{0}, {.x = -300.0F, .y = 100.0F})), kHalfTurn,
                kTolerance);
    // Turned to look along +y, what was to the left is ahead.
    EXPECT_NEAR(cs::bearingOf(listener, nx::Degrees{90}, {.x = 100.0F, .y = 500.0F}), 0.0F, kTolerance);
}

TEST(StepIndicatorTest, AStepNearIsWhollyLoudFadingOutToNoneAtTheFarDistance) {
    const cs::StepIndicatorLook look = roundLook();
    EXPECT_FLOAT_EQ(cs::hearingOf(look, nx::HammerUnits{50}), 1.0F);
    EXPECT_FLOAT_EQ(cs::hearingOf(look, nx::HammerUnits{600}), 0.5F) << "halfway";
    EXPECT_FLOAT_EQ(cs::hearingOf(look, nx::HammerUnits{1100}), 0.0F);
    EXPECT_FLOAT_EQ(cs::hearingOf(look, nx::HammerUnits{5000}), 0.0F);
}

TEST(StepIndicatorTest, AMarkLightsFromWhatItShowedHoldsAndFadesOut) {
    const cs::StepIndicatorLook look = roundLook();
    EXPECT_FLOAT_EQ(cs::stepLevelAt(look, nx::Seconds{}, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(cs::stepLevelAt(look, nx::Seconds{0.05F}, 0.0F), 0.5F) << "halfway lit";
    EXPECT_FLOAT_EQ(cs::stepLevelAt(look, nx::Seconds{}, 0.6F), 0.6F)
        << "a step on a lit mark starts from it";
    EXPECT_FLOAT_EQ(cs::stepLevelAt(look, nx::Seconds{0.3F}, 0.0F), 1.0F) << "holding";
    EXPECT_NEAR(cs::stepLevelAt(look, nx::Seconds{0.8F}, 0.0F), 0.5F, kTolerance) << "halfway faded";
    EXPECT_FLOAT_EQ(cs::stepLevelAt(look, nx::Seconds{1.0F}, 0.0F), 0.0F);
}

TEST(StepIndicatorTest, HudStepIndicatorIsArchivedAndOnByDefault) {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    const auto cvars = cs::StepIndicatorCvars::registerIn(console);
    ASSERT_TRUE(cvars.has_value());
    EXPECT_TRUE(cvars->shown.value());
    EXPECT_TRUE(cvars->shown.flags().test(nx::console::CvarFlag::Archive));
}

TEST(StepIndicatorTest, EachBodyIsHeardOnceTheFaintestGivingWayAndGoneOnceNoLongerLit) {
    const cs::StepIndicatorLook look = roundLook();
    cs::StepIndicator indicator{look};
    nx::ecs::World world;
    const std::array bodies{world.spawn(), world.spawn(), world.spawn(), world.spawn()};
    const auto body = [&bodies](std::size_t which) {
        return bodies[which - 1];
    };
    indicator.hear(std::array{cs::EntityStep{.entity = body(1)}, cs::EntityStep{.entity = body(2)},
                              cs::EntityStep{.entity = body(1)}},
                   nx::Seconds{});
    EXPECT_EQ(indicator.size(), 2U) << "a body stepping twice is heard once";
    indicator.hear(std::array{cs::EntityStep{.entity = body(3)}}, nx::Seconds{0.3F});
    indicator.hear(std::array{cs::EntityStep{.entity = body(4)}}, nx::Seconds{0.75F});
    EXPECT_EQ(indicator.size(), 3U) << "the faintest gave way";
    std::vector<nx::render::ScreenQuad> quads;
    indicator.append(quads, {}, nx::Degrees{0}, {.width = 1920, .height = 1080}, nx::Seconds{1.2F});
    EXPECT_TRUE(quads.empty()) << "not drawn before its picture is on the GPU";
    EXPECT_EQ(indicator.size(), 2U) << "the first, stepped a second ago, no longer lit";
    indicator.append(quads, {}, nx::Degrees{0}, {.width = 1920, .height = 1080}, nx::Seconds{2.0F});
    EXPECT_EQ(indicator.size(), 0U);
}

} // namespace
