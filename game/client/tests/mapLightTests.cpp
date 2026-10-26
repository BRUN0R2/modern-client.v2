// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.math;
import nx.render;
import cs.client;

namespace {

TEST(NearestLightsTest, KeepsOnlyReachingLightsInDistanceOrderWithinTheBudget) {
    const std::array lights{nx::render::StudioLight{.position = {.x = 4.0F}, .reach = 8.0F},
                            nx::render::StudioLight{.position = {.x = 2.0F}, .reach = 1.0F},
                            nx::render::StudioLight{.position = {.x = 3.0F}, .reach = 8.0F},
                            nx::render::StudioLight{.position = {.x = 1.0F}, .reach = 8.0F},
                            nx::render::StudioLight{.position = {.x = 8.0F}, .reach = 8.0F}};
    const std::vector<nx::render::StudioLight> selected = cs::nearestLights(lights, {}, 2);
    ASSERT_EQ(selected.size(), 2U);
    EXPECT_FLOAT_EQ(selected[0].position.x, 1.0F);
    EXPECT_FLOAT_EQ(selected[1].position.x, 3.0F);

    const std::vector<nx::render::StudioLight> all = cs::nearestLights(lights, {}, 10);
    ASSERT_EQ(all.size(), 3U) << "neither the unreachable light nor the one at its reach";
    EXPECT_FLOAT_EQ(all[2].position.x, 4.0F);
    EXPECT_TRUE(cs::nearestLights(lights, {}, 0).empty());
    EXPECT_TRUE(cs::nearestLights({}, {}, 3).empty());
    EXPECT_TRUE(cs::nearestLights(lights, {.x = 100.0F}, 3).empty());
}

} // namespace
