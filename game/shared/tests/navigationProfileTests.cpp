// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The player's navigation profile of data/cstrike/navigation.toml is the player's movement: a mesh
// built for it holds the ways a player walks (docs/adr/0023-navigation.md, its consequences).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.navigation;
import nx.game;
import cs.shared;

namespace {

TEST(NavigationProfileTest, ThePlayersProfileIsTheMovementsPlayer) {
    const std::filesystem::path file =
        std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "navigation.toml";
    auto text = nx::platform::readTextFile(file);
    ASSERT_TRUE(text.has_value());
    const auto read = nx::game::readNavigationData(*text, "navigation.toml");
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    const auto player =
        std::ranges::find(read->profiles, std::string_view{"player"}, &nx::navigation::AgentProfile::name);
    ASSERT_NE(player, read->profiles.end());

    EXPECT_EQ(player->radius.value(), cs::kStandingBox.max.x);
    EXPECT_EQ(player->standingHeight.value(), cs::kStandingBox.size().z);
    EXPECT_EQ(player->crouchedHeight.value(), cs::kDuckedBox.size().z);
    EXPECT_EQ(player->stepHeight, cs::MovementSettings{}.stepSize);
    EXPECT_EQ(player->minGroundNormalZ, cs::kMinGroundNormalZ);
}

} // namespace
