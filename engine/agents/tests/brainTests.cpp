// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.jobs;
import nx.navigation;
import nx.agents;
#include "testGeometry.hpp"

namespace {
auto rules() -> nx::agents::Rules {
    return {.memoryLifetime = nx::Seconds{2.0F},
            .attackDistance = nx::HammerUnits{40.0F},
            .arrivalDistance = nx::HammerUnits{4.0F},
            .replanDistance = nx::HammerUnits{16.0F},
            .memoryLimit = 2};
}
auto floorMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -512}, {.x = 512, .y = 512}, 0);
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentBrain, KnowsOnlyObservedTargetsAndForgetsThemAtTheLifetimeBoundary) {
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    EXPECT_TRUE(brain.memories().empty());
    brain.perceive(std::array{nx::agents::Observation{.entity = target, .position = {.x = 100}}}, {},
                   nx::kDefaultTickRate);
    ASSERT_EQ(brain.memories().size(), 1U);
    brain.perceive({}, nx::GameTime{}.after(nx::Seconds{1}, nx::kDefaultTickRate), nx::kDefaultTickRate);
    ASSERT_EQ(brain.memories().size(), 1U);
    EXPECT_FALSE(brain.memories().front().isVisible);
    EXPECT_EQ(brain.memories().front().observation.position.x, 100);
    brain.perceive({}, nx::GameTime{}.after(nx::Seconds{2}, nx::kDefaultTickRate), nx::kDefaultTickRate);
    EXPECT_TRUE(brain.memories().empty());
}

TEST(AgentBrain, CapsMemoryAndEvictsTheOldestFact) {
    nx::ecs::World world;
    const auto first = world.spawn();
    const auto second = world.spawn();
    const auto third = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{first, {}}}, {}, nx::kDefaultTickRate);
    const auto later = nx::GameTime{}.after(nx::Seconds{1}, nx::kDefaultTickRate);
    brain.perceive(std::array{nx::agents::Observation{second, {}}, nx::agents::Observation{third, {}}}, later,
                   nx::kDefaultTickRate);
    ASSERT_EQ(brain.memories().size(), 2U);
    EXPECT_EQ(brain.memories()[0].observation.entity, second);
    EXPECT_EQ(brain.memories()[1].observation.entity, third);
}

TEST(AgentBrain, ChasesOnTheMeshAndAttacksOnlyWhileItSeesTheTarget) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{target, {.x = 200}}}, {}, nx::kDefaultTickRate);
    const auto chase = brain.decide({}, query);
    EXPECT_EQ(chase.behavior, nx::agents::Behavior::Chase);
    EXPECT_GT(chase.intent.direction.x, 0);
    const auto attack = brain.decide({.x = 180}, query);
    EXPECT_TRUE(attack.intent.attacks);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    EXPECT_FALSE(brain.decide({.x = 180}, query).intent.attacks);
}

TEST(AgentBrain, ReportsAnUnreachableGoalWithoutDirectMovement) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{target, {.x = 5000}}}, {}, nx::kDefaultTickRate);
    const auto decision = brain.decide({}, query);
    EXPECT_EQ(decision.behavior, nx::agents::Behavior::Blocked);
    EXPECT_TRUE(decision.navigationError.has_value());
    EXPECT_EQ(decision.intent.direction, nx::Vec3{});
    EXPECT_FALSE(decision.intent.attacks);
}

TEST(AgentBrain, MovesToItsGoalWithNoTargetAndForgetsItAsItArrives) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    EXPECT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Idle);
    const nx::Vec3 goal{.y = 200};
    brain.setGoal(goal);
    const auto moving = brain.decide({}, query);
    EXPECT_EQ(moving.behavior, nx::agents::Behavior::MoveTo);
    EXPECT_GT(moving.intent.direction.y, 0.0F);
    EXPECT_FALSE(moving.intent.lookAt.has_value());
    EXPECT_FALSE(moving.intent.attacks);
    EXPECT_EQ(brain.decide(goal, query).behavior, nx::agents::Behavior::Idle);
    EXPECT_FALSE(brain.goal().has_value());
}

TEST(AgentBrain, ATargetItSeesComesBeforeItsGoal) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.y = 200});
    ASSERT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::MoveTo);
    brain.perceive(std::array{nx::agents::Observation{target, {.x = -200}}}, {}, nx::kDefaultTickRate);
    const auto chase = brain.decide({}, query);
    EXPECT_EQ(chase.behavior, nx::agents::Behavior::Chase);
    EXPECT_LT(chase.intent.direction.x, 0.0F);
    EXPECT_TRUE(brain.goal().has_value()) << "kept for when it has no target";
}

TEST(AgentBrain, FollowsAnObservationThatMovesLessThanTheReplanDistance) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    const nx::Vec3 originalGoal{.x = 200};
    brain.perceive(std::array{nx::agents::Observation{.entity = target, .position = originalGoal}}, {},
                   nx::kDefaultTickRate);
    ASSERT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Chase);
    const nx::Vec3 shiftedGoal{.x = originalGoal.x + rules().replanDistance.value() / 2.0F};
    brain.perceive(std::array{nx::agents::Observation{.entity = target, .position = shiftedGoal}}, {},
                   nx::kDefaultTickRate);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    const auto investigation = brain.decide(originalGoal, query);
    EXPECT_EQ(investigation.behavior, nx::agents::Behavior::Investigate);
    EXPECT_GT(investigation.intent.direction.x, 0.0F);
    EXPECT_FALSE(brain.memories().empty());
    EXPECT_EQ(brain.decide(shiftedGoal, query).behavior, nx::agents::Behavior::Idle);
    EXPECT_TRUE(brain.memories().empty());
}

TEST(AgentBrain, FinishesInvestigationAtTheWalkablePointBelowItsLastObservation) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    constexpr float kObservedJumpHeight = 16.0F;
    const nx::Vec3 feet{.x = 100};
    const nx::Vec3 observedFeet{.x = feet.x, .z = kObservedJumpHeight};
    brain.perceive(std::array{nx::agents::Observation{.entity = target, .position = observedFeet}}, {},
                   nx::kDefaultTickRate);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    const auto decision = brain.decide(feet, query);
    EXPECT_EQ(decision.behavior, nx::agents::Behavior::Idle);
    EXPECT_EQ(decision.intent.direction, nx::Vec3{});
    EXPECT_TRUE(brain.memories().empty());
}

TEST(AgentBrain, PreservesTheObservedAimPointSeparatelyFromTheGroundGoal) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    const nx::Vec3 crouchedEye{.x = 30, .z = 30};
    brain.perceive(
        std::array{nx::agents::Observation{.entity = target, .position = {.x = 30}, .aimAt = crouchedEye}},
        {}, nx::kDefaultTickRate);
    const auto decision = brain.decide({}, query);
    EXPECT_TRUE(decision.intent.attacks);
    ASSERT_TRUE(decision.intent.lookAt.has_value());
    EXPECT_EQ(*decision.intent.lookAt, crouchedEye);
    EXPECT_EQ(brain.memories().front().observation.position.z, 0);
}
} // namespace
