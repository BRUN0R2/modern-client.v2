// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import cs.server;
import cs.bots;

namespace {

constexpr nx::Degrees kPlayersCone{120.0F};
constexpr float kTolerance = 1e-4F;
const cs::BotSightSpots kSpots{.head = nx::HammerUnits{25.0F},
                               .standingFeet = nx::HammerUnits{34.0F},
                               .duckedFeet = nx::HammerUnits{14.0F},
                               .side = nx::HammerUnits{13.0F}};

void expectNear(nx::Vec3 actual, nx::Vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

TEST(BotSightTest, ThePlayersConeIsSixtyDegreesEitherWayOfItsYawAcrossTheGround) {
    const nx::Vec3 origin{.x = 100.0F, .y = 100.0F};
    constexpr nx::Degrees kFacingY{90.0F};
    const auto at = [&origin](float degrees, float height) {
        const nx::Vec3 way = nx::angleVectors(nx::EulerAngles{.yaw = nx::Degrees{degrees}}).forward;
        return origin + (way * 200.0F) + nx::Vec3{.z = height};
    };
    EXPECT_TRUE(cs::isInViewCone(origin, kFacingY, kPlayersCone, at(90.0F, 0.0F)));
    EXPECT_TRUE(cs::isInViewCone(origin, kFacingY, kPlayersCone, at(149.0F, 500.0F)))
        << "the height is not weighed";
    EXPECT_FALSE(cs::isInViewCone(origin, kFacingY, kPlayersCone, at(151.0F, 0.0F)));
    EXPECT_FALSE(cs::isInViewCone(origin, kFacingY, kPlayersCone, at(29.0F, 0.0F)));
    EXPECT_FALSE(cs::isInViewCone(origin, kFacingY, kPlayersCone, at(270.0F, 0.0F))) << "behind";
    EXPECT_TRUE(cs::isInViewCone(origin, kFacingY, kPlayersCone, origin + nx::Vec3{.z = 300.0F})) << "above";
}

TEST(BotSightTest, TheSpotsAreTheChestHeadFeetAndSidesAcrossTheLineToIt) {
    const cs::PerceptionTarget standing{.origin = {.x = 100.0F, .z = 36.0F}};
    const std::array spots = cs::sightSpotsOf(standing, {}, kSpots);
    expectNear(spots[0], {.x = 100.0F, .z = 36.0F});
    expectNear(spots[1], {.x = 100.0F, .z = 61.0F});
    expectNear(spots[2], {.x = 100.0F, .z = 2.0F});
    // Seen along +x, its left is +y.
    expectNear(spots[3], {.x = 100.0F, .y = 13.0F, .z = 36.0F});
    expectNear(spots[4], {.x = 100.0F, .y = -13.0F, .z = 36.0F});

    cs::PerceptionTarget ducked = standing;
    ducked.isDucked = true;
    expectNear(cs::sightSpotsOf(ducked, {}, kSpots)[2], {.x = 100.0F, .z = 22.0F});
    const std::array above = cs::sightSpotsOf(standing, standing.origin + nx::Vec3{.z = 100.0F}, kSpots);
    expectNear(above[3], standing.origin);
    expectNear(above[4], standing.origin);
}

} // namespace
