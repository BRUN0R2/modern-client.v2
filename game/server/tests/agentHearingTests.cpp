// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the server's agents hear of the players' noises: an enemy's within how far its kind carries, as
// loud as it was made, placed off where it was made by an error that grows with how far it is, on the
// mesh; a teammate's never; the same for the same draws.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.jobs;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

#include "testGeometry.hpp"

namespace {

/// Condition Zero's ranges and error, as data/cstrike/bots.toml gives them.
auto hearing() -> cs::HearingRules {
    return {.footstep = nx::HammerUnits{1100.0F},
            .landing = nx::HammerUnits{1100.0F},
            .shot = nx::HammerUnits{2000.0F},
            .quietShot = nx::HammerUnits{1000.0F},
            .emptyShot = nx::HammerUnits{1100.0F},
            .errorShare = 0.2F};
}

/// A floor of 4096 by 4096 about the origin.
auto floorMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -2048, .y = -2048}, {.x = 2048, .y = 2048}, 0);
    return nx::test::playerMesh(geometry, 0);
}

/// The eye of a player standing at `feet`, a standing view above them.
auto eyeAbove(nx::Vec3 feet) -> nx::Vec3 {
    return feet + nx::Vec3{.z = cs::kStandingViewHeight.value() - cs::kStandingBox.min.z};
}

/// A noise of `kind` the player of `entity` and `team` made with its feet at `feet`.
auto noiseAt(nx::ecs::Entity entity, cs::Team team, nx::Vec3 feet, cs::NoiseKind kind, float loudness = 1.0F)
    -> cs::GameNoise {
    return {.entity = entity,
            .team = team,
            .feet = feet,
            .eye = eyeAbove(feet),
            .kind = kind,
            .loudness = loudness};
}

TEST(AgentHearingTest, HearingIsValidWithRangesAndAnErrorOfNoneOrMore) {
    EXPECT_TRUE(cs::isValid(hearing()));
    auto rules = hearing();
    rules.quietShot = nx::HammerUnits{};
    EXPECT_FALSE(cs::isValid(rules));
    rules = hearing();
    rules.errorShare = -0.1F;
    EXPECT_FALSE(cs::isValid(rules));
    EXPECT_EQ(cs::rangeOf(hearing(), cs::NoiseKind::Shot), nx::HammerUnits{2000.0F});
    EXPECT_EQ(cs::rangeOf(hearing(), cs::NoiseKind::QuietShot), nx::HammerUnits{1000.0F});
    EXPECT_EQ(cs::salienceOf(cs::NoiseKind::Shot), nx::agents::Salience::High);
    EXPECT_EQ(cs::salienceOf(cs::NoiseKind::QuietShot), nx::agents::Salience::High);
    EXPECT_EQ(cs::salienceOf(cs::NoiseKind::Footstep), nx::agents::Salience::Low);
}

TEST(AgentHearingTest, AnEnemysNoiseIsHeardWithinItsKindsRangeAsLoudAsItWasMade) {
    const auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto near = world.spawn();
    const auto far = world.spawn();
    const auto quiet = world.spawn();
    const auto teammate = world.spawn();
    const cs::Listener listener{.team = cs::Team::CounterTerrorists, .ear = eyeAbove({})};
    const std::array noises{
        noiseAt(near, cs::Team::Terrorists, {.x = 1000}, cs::NoiseKind::Footstep),
        noiseAt(far, cs::Team::Terrorists, {.x = -1200}, cs::NoiseKind::Footstep),
        noiseAt(quiet, cs::Team::Terrorists, {.y = 1000}, cs::NoiseKind::Footstep, 0.5F),
        noiseAt(teammate, cs::Team::CounterTerrorists, {.y = -100}, cs::NoiseKind::Shot),
    };
    nx::Random random{1, "hearing tests"};
    const std::vector<nx::agents::Sensed> heard = cs::heardBy(listener, noises, hearing(), query, random);
    ASSERT_EQ(heard.size(), 1U)
        << "a step 1000 away; none 1200 away, half as loud 1000 away, nor a teammate's";
    EXPECT_EQ(heard.front().observation.entity, near);
    EXPECT_EQ(heard.front().salience, nx::agents::Salience::Low) << "a step";
    EXPECT_NEAR(heard.front().distance.value(), 1000.0F, 1.0F) << "heard as far as it was made";

    nx::Random again{1, "hearing tests"};
    const auto everyone = cs::heardBy({.ear = eyeAbove({})}, noises, hearing(), query, again);
    EXPECT_EQ(everyone.size(), 2U) << "an agent of no team hears every player: the step and the shot";
}

TEST(AgentHearingTest, WhatIsHeardIsPlacedOffWhereItWasMadeByAnErrorThatGrowsWithHowFarItIs) {
    const auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto enemy = world.spawn();
    const nx::Vec3 made{.x = 1500, .y = 200};
    const std::array noises{noiseAt(enemy, cs::Team::Terrorists, made, cs::NoiseKind::Shot)};
    const cs::Listener listener{.team = cs::Team::CounterTerrorists, .ear = eyeAbove({})};
    const float distance = nx::length(eyeAbove(made) - listener.ear);
    bool wasOff = false;
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        nx::Random random{seed, "hearing tests"};
        const auto heard = cs::heardBy(listener, noises, hearing(), query, random);
        ASSERT_EQ(heard.size(), 1U);
        const nx::agents::Observation& observation = heard.front().observation;
        EXPECT_LE(std::abs(observation.position.x - made.x), 0.2F * distance + 1.0F);
        EXPECT_LE(std::abs(observation.position.y - made.y), 0.2F * distance + 1.0F);
        EXPECT_NEAR(observation.position.z, 0.0F, 8.0F) << "on the floor's mesh";
        ASSERT_TRUE(observation.aimAt.has_value());
        EXPECT_NEAR(observation.aimAt->z - observation.position.z, eyeAbove({}).z, 0.01F)
            << "its eye above it";
        wasOff = wasOff || nx::test::distanceAcross(observation.position, made) > 16.0F;
    }
    EXPECT_TRUE(wasOff) << "never exactly where it was made";

    auto exact = hearing();
    exact.errorShare = 0.0F;
    nx::Random random{1, "hearing tests"};
    const auto heard = cs::heardBy(listener, noises, exact, query, random);
    ASSERT_EQ(heard.size(), 1U);
    EXPECT_LT(nx::test::distanceAcross(heard.front().observation.position, made), 1.0F)
        << "with no error, where it was made";
}

TEST(AgentHearingTest, EachEnemyIsHeardOnceByItsNearestNoiseAndTheSameDrawsHearTheSame) {
    const auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto enemy = world.spawn();
    const std::array noises{
        noiseAt(enemy, cs::Team::Terrorists, {.x = 900}, cs::NoiseKind::Shot),
        noiseAt(enemy, cs::Team::Terrorists, {.x = 300}, cs::NoiseKind::Footstep),
    };
    auto exact = hearing();
    exact.errorShare = 0.0F;
    nx::Random random{1, "hearing tests"};
    const auto heard =
        cs::heardBy({.team = cs::Team::CounterTerrorists, .ear = eyeAbove({})}, noises, exact, query, random);
    ASSERT_EQ(heard.size(), 1U);
    EXPECT_NEAR(heard.front().observation.position.x, 300.0F, 1.0F) << "its nearest";

    nx::Random first{7, "hearing tests"};
    nx::Random second{7, "hearing tests"};
    const auto once = cs::heardBy({.ear = eyeAbove({})}, noises, hearing(), query, first);
    const auto twice = cs::heardBy({.ear = eyeAbove({})}, noises, hearing(), query, second);
    ASSERT_EQ(once.size(), twice.size());
    EXPECT_EQ(once.front().observation.position, twice.front().observation.position);
}

} // namespace
