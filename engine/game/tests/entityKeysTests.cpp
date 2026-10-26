// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The angles the engine reads of a map entity's keys, in the order the map gives them, the same for every
// entity the game and its clients make: "angles" whole, "angle" a yaw, straight up or straight down.

#include <gtest/gtest.h>

import std;
import nx.math;
import nx.assets;
import nx.game;

namespace {

/// An entity of the keys `keyValues`, in their order.
[[nodiscard]] auto entityOf(std::vector<std::pair<std::string, std::string>> keyValues)
    -> nx::assets::BspEntity {
    return nx::assets::BspEntity{.keyValues = std::move(keyValues)};
}

TEST(EntityAnglesTest, TheLaterOfAnglesAndAngleWins) {
    EXPECT_EQ(nx::game::anglesOf(entityOf({{"angles", "10 20 30"}, {"angle", "90"}})),
              (nx::Vec3{.x = 10.0F, .y = 90.0F, .z = 30.0F}))
        << "\"angle\" sets the yaw alone";
    EXPECT_EQ(nx::game::anglesOf(entityOf({{"angle", "90"}, {"angles", "10 20 30"}})),
              (nx::Vec3{.x = 10.0F, .y = 20.0F, .z = 30.0F}));
}

TEST(EntityAnglesTest, ANegativeAngleAimsStraightUpOrDown) {
    EXPECT_EQ(nx::game::anglesOf(entityOf({{"angle", "-1"}})), (nx::Vec3{.x = -90.0F})) << "up";
    EXPECT_EQ(nx::game::anglesOf(entityOf({{"angle", "-2"}})), (nx::Vec3{.x = 90.0F})) << "down";
    EXPECT_EQ(nx::game::anglesOf(entityOf({{"angle", "-1.5"}})), (nx::Vec3{.x = -90.0F}))
        << "cut to a whole number, as the original compares it";
}

TEST(EntityAnglesTest, AnEntityWithoutAnglesFacesTheWorldsAxes) {
    EXPECT_EQ(nx::game::anglesOf(entityOf({{"classname", "info_target"}})), nx::Vec3{});
    EXPECT_FALSE(nx::game::anglesAfter({}, "origin", "1 2 3").has_value());
}

} // namespace
