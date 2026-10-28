// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The navigation profile the server's agents walk by: the player's of the repository's data, standing,
// and what is refused for it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.game;
import nx.navigation;
import cs.server;

namespace {

/// data/cstrike/navigation.toml, which the test needs.
[[nodiscard]] auto repositoryNavigation() -> nx::game::NavigationData {
    const auto file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "navigation.toml";
    auto navigation = nx::game::readNavigationData(nx::platform::readTextFile(file).value_or(std::string{}),
                                                   "navigation.toml");
    EXPECT_TRUE(navigation.has_value()) << (navigation ? "" : nx::describe(navigation.error()));
    return std::move(navigation).value_or(nx::game::NavigationData{});
}

TEST(AgentNavigationTest, TheAgentsWalkThePlayersProfileStanding) {
    const nx::game::NavigationData navigation = repositoryNavigation();
    const auto player =
        std::ranges::find(navigation.profiles, cs::kPlayerProfileName, &nx::navigation::AgentProfile::name);
    ASSERT_NE(player, navigation.profiles.end());
    const auto standing = cs::standingPlayerProfile(navigation);
    ASSERT_TRUE(standing.has_value()) << nx::describe(standing.error());
    EXPECT_EQ(standing->crouchedHeight, player->standingHeight) << "no agent ducks yet";
    EXPECT_EQ(standing->standingHeight, player->standingHeight);
    EXPECT_EQ(standing->radius, player->radius);
    EXPECT_EQ(standing->stepHeight, player->stepHeight);
    EXPECT_EQ(standing->name, player->name);
    EXPECT_TRUE(nx::navigation::checkProfile(*standing, navigation.build).has_value());
}

TEST(AgentNavigationTest, NoPlayersProfileOrOneUnlikeTheMovementIsRefused) {
    nx::game::NavigationData unnamed = repositoryNavigation();
    for (nx::navigation::AgentProfile& profile : unnamed.profiles) {
        profile.name = "boss";
    }
    nx::game::NavigationData wider = repositoryNavigation();
    for (nx::navigation::AgentProfile& profile : wider.profiles) {
        profile.radius = nx::HammerUnits{profile.radius.value() * 2.0F};
    }
    for (const nx::game::NavigationData& navigation : {unnamed, wider}) {
        const auto refused = cs::standingPlayerProfile(navigation);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, nx::navigation::MeshBuildError::InvalidProfile);
    }
}

} // namespace
