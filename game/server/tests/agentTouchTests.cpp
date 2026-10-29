// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the server's agents touch: the enemies whose boxes meet theirs, felt where they are; a teammate
// never, nor itself; a body a little apart is not touched.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.agents;
import cs.shared;
import cs.server;

namespace {

/// A living player of `team` standing with its origin at `origin`, ducked when `isDucked`.
auto playerAt(nx::ecs::Entity entity, cs::Team team, nx::Vec3 origin, bool isDucked = false)
    -> cs::PerceptionTarget {
    const nx::Aabb& box = isDucked ? cs::kDuckedBox : cs::kStandingBox;
    return {.entity = entity,
            .team = team,
            .origin = origin,
            .eye = origin + nx::Vec3{.z = 17.0F},
            .feet = origin + nx::Vec3{.z = box.min.z},
            .isDucked = isDucked};
}

TEST(AgentTouchTest, AnAgentFeelsTheEnemiesItsBodyTouchesWhereTheyAre) {
    nx::ecs::World world;
    const auto self = world.spawn();
    const auto touching = world.spawn();
    const auto apart = world.spawn();
    const auto teammate = world.spawn();
    const auto ducked = world.spawn();
    // Its box 16 either way across: another standing 32 off touches it, 36 off does not.
    const std::array targets{
        playerAt(self, cs::Team::CounterTerrorists, {}),
        playerAt(touching, cs::Team::Terrorists, {.x = 32.5F}),
        playerAt(apart, cs::Team::Terrorists, {.y = 36.0F}),
        playerAt(teammate, cs::Team::CounterTerrorists, {.x = -32.0F}),
        playerAt(ducked, cs::Team::Terrorists, {.y = -32.0F, .z = -18.0F}, true),
    };
    const cs::Toucher toucher{
        .entity = self, .team = cs::Team::CounterTerrorists, .origin = {}, .box = cs::kStandingBox};
    const std::vector<nx::agents::Observation> touched = cs::touchedBy(toucher, targets);
    ASSERT_EQ(touched.size(), 2U)
        << "the one beside it and the ducked one; not one apart, a teammate, itself";
    EXPECT_EQ(touched[0].entity, touching);
    EXPECT_EQ(touched[0].position, targets[1].feet) << "where it is";
    EXPECT_EQ(touched[0].aimAt, targets[1].eye);
    EXPECT_EQ(touched[1].entity, ducked);

    const cs::Toucher ofNoTeam{.entity = self, .origin = {}, .box = cs::kStandingBox};
    EXPECT_EQ(cs::touchedBy(ofNoTeam, targets).size(), 3U) << "an agent of no team feels every player";
}

TEST(AgentTouchTest, ABodyAboveOrBelowIsNotTouched) {
    nx::ecs::World world;
    const auto self = world.spawn();
    const auto above = world.spawn();
    const std::array targets{playerAt(above, cs::Team::Terrorists, {.z = 80.0F})};
    const cs::Toucher toucher{
        .entity = self, .team = cs::Team::CounterTerrorists, .origin = {}, .box = cs::kStandingBox};
    EXPECT_TRUE(cs::touchedBy(toucher, targets).empty()) << "8 units above its head";
}

} // namespace
