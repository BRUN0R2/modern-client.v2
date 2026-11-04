// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The navigation profile the server's agents walk by: the player's of the repository's data, and what is
// refused for it.

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

TEST(AgentNavigationTest, TheAgentsWalkThePlayersProfile) {
    const nx::game::NavigationData navigation = repositoryNavigation();
    const auto player =
        std::ranges::find(navigation.profiles, cs::kPlayerProfileName, &nx::navigation::AgentProfile::name);
    ASSERT_NE(player, navigation.profiles.end());
    const auto walked = cs::agentProfile(navigation);
    ASSERT_TRUE(walked.has_value()) << nx::describe(walked.error());
    EXPECT_EQ(walked->crouchedHeight, player->crouchedHeight);
    EXPECT_LT(walked->crouchedHeight, walked->standingHeight) << "agents duck";
    EXPECT_EQ(walked->standingHeight, player->standingHeight);
    EXPECT_EQ(walked->radius, player->radius);
    EXPECT_EQ(walked->stepHeight, player->stepHeight);
    EXPECT_EQ(walked->name, player->name);
    EXPECT_TRUE(nx::navigation::checkProfile(*walked, navigation.build).has_value());
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
    nx::game::NavigationData neverCrouched = repositoryNavigation();
    for (nx::navigation::AgentProfile& profile : neverCrouched.profiles) {
        profile.crouchedHeight = profile.standingHeight;
    }
    for (const nx::game::NavigationData& navigation : {unnamed, wider, neverCrouched}) {
        const auto refused = cs::agentProfile(navigation);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, nx::navigation::MeshBuildError::InvalidProfile);
    }
}

} // namespace
