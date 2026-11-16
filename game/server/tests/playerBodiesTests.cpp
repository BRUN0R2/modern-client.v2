// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How the server's traces see a player's aim (R_StudioPlayerBlend): the first blending of its sequence by
// its body's pitch, and the pitch its model is turned by beyond the blends.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import cs.server;

namespace {

constexpr float kTolerance = 1e-4F;

/// A sequence whose first blend runs from `start` to `end`, as a player's aims do.
[[nodiscard]] auto blendedFrom(float start, float end) -> nx::assets::StudioSequence {
    return nx::assets::StudioSequence{.label = "ref_aim", .blendStarts = {start}, .blendEnds = {end}};
}

TEST(PlayerBlendTest, APitchWithinTheBlendsIsItsShareOfTheWayAndTurnsTheModelNoFurther) {
    // 10 degrees, times 3: 30, of the way from -90 to 90, 120 of 180, times 255.
    const cs::PlayerBlend blend = cs::playerBlendOf(blendedFrom(-90.0F, 90.0F), nx::Degrees{10.0F});
    EXPECT_EQ(blend.blending, 170);
    EXPECT_EQ(blend.pitch, nx::Degrees{});
}

TEST(PlayerBlendTest, APitchBeyondTheBlendsTakesTheLastAndTurnsTheModelByWhatIsLeft) {
    const cs::PlayerBlend up = cs::playerBlendOf(blendedFrom(-90.0F, 90.0F), nx::Degrees{-40.0F});
    EXPECT_EQ(up.blending, 0);
    EXPECT_NEAR(up.pitch.value(), -10.0F, kTolerance) << "-40 less a third of -90";
    const cs::PlayerBlend down = cs::playerBlendOf(blendedFrom(-90.0F, 90.0F), nx::Degrees{40.0F});
    EXPECT_EQ(down.blending, 255);
    EXPECT_NEAR(down.pitch.value(), 10.0F, kTolerance) << "40 less a third of 90";
}

TEST(PlayerBlendTest, AWayShorterThanATenthIsItsMiddle) {
    EXPECT_EQ(cs::playerBlendOf(blendedFrom(0.0F, 0.05F), nx::Degrees{}).blending, 127);
}

} // namespace
