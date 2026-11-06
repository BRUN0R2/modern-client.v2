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
#include "testAgents.hpp"
#include "testGeometry.hpp"

namespace {
auto rules() -> nx::agents::Rules {
    return {.memoryLifetime = nx::Seconds{2.0F},
            .attackDistance = nx::HammerUnits{40.0F},
            .arrivalDistance = nx::HammerUnits{4.0F},
            .replanDistance = nx::HammerUnits{16.0F},
            .memoryLimit = 2};
}
/// How fast the body a brain decides for runs: a player's with a knife or a pistol.
constexpr nx::UnitsPerSecond kRunSpeed{250.0F};
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

TEST(AgentBrain, ForgetsATargetThatIsNoLongerThereAndPlansNoWayToIt) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto dying = world.spawn();
    const auto living = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(
        std::array{nx::agents::Observation{dying, {.x = 200}}, nx::agents::Observation{living, {.y = -200}}},
        {}, nx::kDefaultTickRate);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    ASSERT_EQ(brain.memories().size(), 2U);
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Investigate);

    std::array present{living};
    std::ranges::sort(present);
    brain.forgetAbsent(present);
    ASSERT_EQ(brain.memories().size(), 1U);
    EXPECT_EQ(brain.memories().front().observation.entity, living);

    brain.forgetAbsent({});
    EXPECT_TRUE(brain.memories().empty());
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle)
        << "nothing left to go to";
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    EXPECT_TRUE(way.empty()) << "its corridor forgotten with it";
}

TEST(AgentBrain, ATargetHeardIsInvestigatedWhereItWasHeardLookingThere) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto heard = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive({}, {}, nx::kDefaultTickRate);
    const nx::Vec3 eye{.x = -300, .y = 50, .z = 53};
    brain.sense(std::array{nx::agents::Sensed{
                    .observation = {.entity = heard, .position = {.x = -300, .y = 50}, .aimAt = eye}}},
                {}, nx::kDefaultTickRate);
    ASSERT_EQ(brain.memories().size(), 1U);
    EXPECT_FALSE(brain.memories().front().isVisible) << "heard, not seen";
    const nx::agents::Decision decision = brain.decide({}, query, kRunSpeed);
    EXPECT_EQ(decision.behavior, nx::agents::Behavior::Investigate);
    EXPECT_EQ(decision.target, heard);
    EXPECT_EQ(decision.intent.lookAt, eye) << "it turns to look where it heard it";
    EXPECT_FALSE(decision.intent.attacks) << "only a target it sees it attacks";
    EXPECT_LT(decision.intent.direction.x, 0.0F) << "and goes there";
}

TEST(AgentBrain, WhatItHearsOfATargetItSeesChangesNothingAndAHeardOneIsRecognizedAfterItsReactionTime) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    auto withReaction = rules();
    withReaction.reactionTime = nx::Seconds{0.5F};
    nx::agents::Brain brain{withReaction};
    const nx::Vec3 seenAt{.x = 200};
    brain.perceive(std::array{nx::agents::Observation{target, seenAt}}, {}, nx::kDefaultTickRate);
    brain.sense(std::array{nx::agents::Sensed{.observation = {target, {.x = 260, .y = 40}}}}, {},
                nx::kDefaultTickRate);
    ASSERT_EQ(brain.memories().size(), 1U);
    EXPECT_EQ(brain.memories().front().observation.position, seenAt) << "its sight the better";
    EXPECT_TRUE(brain.memories().front().isVisible);

    const auto other = world.spawn();
    const nx::GameTime later = nx::GameTime{}.after(nx::Seconds{0.25F}, nx::kDefaultTickRate);
    brain.perceive({}, later, nx::kDefaultTickRate);
    brain.sense(std::array{nx::agents::Sensed{.observation = {other, {.y = -200}}}}, later,
                nx::kDefaultTickRate);
    const auto heardRecord = std::ranges::find(
        brain.memories(), other, [](const nx::agents::Memory& record) { return record.observation.entity; });
    ASSERT_NE(heardRecord, brain.memories().end());
    EXPECT_FALSE(heardRecord->isRecognized) << "heard just now";
    const nx::GameTime reacted = later.after(nx::Seconds{0.5F}, nx::kDefaultTickRate);
    brain.perceive({}, reacted, nx::kDefaultTickRate);
    brain.sense(std::array{nx::agents::Sensed{.observation = {other, {.y = -210}}}}, reacted,
                nx::kDefaultTickRate);
    const auto recognized = std::ranges::find(
        brain.memories(), other, [](const nx::agents::Memory& record) { return record.observation.entity; });
    ASSERT_NE(recognized, brain.memories().end());
    EXPECT_TRUE(recognized->isRecognized);
    EXPECT_EQ(recognized->observation.position, (nx::Vec3{.y = -210})) << "where it was heard last";
}

/// An estimate of `entity` at `position`, as salient as `salience`, sensed `distance` away.
auto estimateOf(nx::ecs::Entity entity, nx::Vec3 position, nx::agents::Salience salience, float distance)
    -> nx::agents::Sensed {
    return {.observation = {.entity = entity, .position = position, .aimAt = position + nx::Vec3{.z = 53}},
            .salience = salience,
            .distance = nx::HammerUnits{distance}};
}

/// Where `brain` remembers `entity` to be.
auto rememberedAt(const nx::agents::Brain& brain, nx::ecs::Entity entity) -> std::optional<nx::Vec3> {
    const auto found = std::ranges::find(
        brain.memories(), entity, [](const nx::agents::Memory& record) { return record.observation.entity; });
    return found != brain.memories().end() ? std::optional{found->observation.position} : std::nullopt;
}

TEST(AgentBrain, ARecentEstimateHoldsAgainstAVaguerOrNoNearerOneUntilItsShortTermMemoryWentBy) {
    using nx::agents::Salience;
    nx::ecs::World world;
    const auto enemy = world.spawn();
    auto shortTerm = rules();
    shortTerm.shortTermMemory = nx::Seconds{3.0F};
    shortTerm.memoryLifetime = nx::Seconds{10.0F};
    nx::agents::Brain brain{shortTerm};
    const auto at = [](float seconds) {
        return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
    };
    const nx::Vec3 shot{.x = 500};
    brain.perceive({}, at(0.0F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, shot, Salience::High, 500)}, at(0.0F), nx::kDefaultTickRate);
    ASSERT_EQ(rememberedAt(brain, enemy), shot);

    brain.perceive({}, at(0.5F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, {.x = 380}, Salience::Low, 380)}, at(0.5F),
                nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), shot) << "a step heard does not move where it heard a shot";
    brain.perceive({}, at(1.0F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, {.x = 640}, Salience::High, 640)}, at(1.0F),
                nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), shot) << "nor a farther shot";
    const nx::Vec3 nearer{.x = 450, .y = 30};
    brain.perceive({}, at(1.5F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, nearer, Salience::High, 450)}, at(1.5F), nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), nearer) << "a nearer shot does";

    // Three seconds after the nearer shot placed it, a step moves it.
    const nx::Vec3 step{.x = 700};
    brain.perceive({}, at(4.6F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, step, Salience::Low, 700)}, at(4.6F), nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), step) << "its short-term memory went by";
    const nx::Vec3 touched{.x = 20};
    brain.perceive({}, at(4.7F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, touched, Salience::Certain, 32)}, at(4.7F),
                nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), touched) << "what it touches it is sure of";
}

TEST(AgentBrain, WhereItSawATargetHoldsAgainstWhatItHearsOfItForItsShortTermMemory) {
    using nx::agents::Salience;
    nx::ecs::World world;
    const auto enemy = world.spawn();
    auto shortTerm = rules();
    shortTerm.shortTermMemory = nx::Seconds{3.0F};
    shortTerm.memoryLifetime = nx::Seconds{10.0F};
    nx::agents::Brain brain{shortTerm};
    const auto at = [](float seconds) {
        return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
    };
    const nx::Vec3 seen{.x = 300};
    brain.perceive(std::array{nx::agents::Observation{enemy, seen}}, at(0.0F), nx::kDefaultTickRate);
    brain.perceive({}, at(1.0F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, {.x = 380}, Salience::High, 380)}, at(1.0F),
                nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), seen) << "where it saw it, lately";
    brain.perceive({}, at(3.5F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(enemy, {.x = 420}, Salience::Low, 420)}, at(3.5F),
                nx::kDefaultTickRate);
    EXPECT_EQ(rememberedAt(brain, enemy), (nx::Vec3{.x = 420}));
}

TEST(AgentBrain, AFarGoalThatMovesLessThanItsShareKeepsItsWayAndOneThatMovesMoreGetsANewOne) {
    // Two corridors along y = 0 to 256 and 512 to 768, joined only at their far ends, x below 256 and above
    // 1792: a goal across the hole from where the last search aimed, about x 1000, is a long way around,
    // beyond what the corridor's end slides through.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 256, .y = 768}, 0);
    nx::test::addFloor(geometry, {.x = 256, .y = 0}, {.x = 1792, .y = 256}, 0);
    nx::test::addFloor(geometry, {.x = 256, .y = 512}, {.x = 1792, .y = 768}, 0);
    nx::test::addFloor(geometry, {.x = 1792, .y = 0}, {.x = 2048, .y = 768}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    auto shared = rules();
    shared.replanShare = 0.35F;
    nx::agents::Brain brain{shared};
    const nx::Vec3 feet{.x = 128, .y = 128};
    const auto wayEnd = [&](nx::Vec3 goal) {
        brain.setGoal(goal);
        std::ignore = brain.decide(feet, query, kRunSpeed);
        std::vector<nx::Vec3> way;
        brain.appendWay(query, way);
        return way.empty() ? nx::Vec3{} : way.back();
    };
    ASSERT_LT(nx::test::distanceAcross(wayEnd({.x = 1000, .y = 232}), {.x = 1000, .y = 232}), 1.0F);
    // 304 across the hole, less than 0.35 of how far it is (963): the way's end slides, and stops at the
    // edge.
    const nx::Vec3 slid = wayEnd({.x = 1000, .y = 536});
    EXPECT_LT(slid.y, 300.0F) << "the same way, no search";
    // 508 from where the search aimed, more than 0.35 of how far it is (1066): a new way, around.
    const nx::Vec3 around = wayEnd({.x = 1000, .y = 740});
    EXPECT_LT(nx::test::distanceAcross(around, {.x = 1000, .y = 740}), 1.0F);
}

TEST(AgentBrain, WhatItPerceivedWhileItFoughtWaitsForTheFightToBeOver) {
    using nx::agents::Salience;
    nx::ecs::World world;
    const auto fought = world.spawn();
    const auto heard = world.spawn();
    nx::agents::Brain brain{rules()}; // remembers 2 seconds
    const auto at = [](float seconds) {
        return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
    };
    const auto remembers = [&brain](nx::ecs::Entity entity) {
        return std::ranges::contains(brain.memories(), entity, [](const nx::agents::Memory& record) {
            return record.observation.entity;
        });
    };
    brain.perceive(std::array{nx::agents::Observation{fought, {.x = 300}}}, at(0.0F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(heard, {.y = 200}, Salience::Low, 200)}, at(0.0F),
                nx::kDefaultTickRate);
    // It fights for 5 seconds, longer than it remembers: what it heard at the start waits.
    for (int second = 1; second <= 5; ++second) {
        brain.perceive(std::array{nx::agents::Observation{fought, {.x = 300}}},
                       at(static_cast<float>(second)), nx::kDefaultTickRate);
        ASSERT_TRUE(remembers(heard)) << "at " << second;
    }
    // The fight over, as its enemy died, it has its memory's lifetime to look into it.
    brain.forgetAbsent(std::array{heard});
    brain.perceive({}, at(6.9F), nx::kDefaultTickRate);
    EXPECT_TRUE(remembers(heard));
    brain.perceive({}, at(7.1F), nx::kDefaultTickRate);
    EXPECT_FALSE(remembers(heard));
}

TEST(AgentBrain, LosingSightOfWhatItFoughtItKeepsAfterItUnlessAThreatIsMuchNearer) {
    using nx::agents::Salience;
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto fought = world.spawn();
    const auto stepping = world.spawn();
    const auto shooting = world.spawn();
    auto threats = rules();
    threats.memoryLifetime = nx::Seconds{10.0F};
    threats.threatRecency = nx::Seconds{0.5F};
    threats.threatMargin = nx::HammerUnits{100.0F};
    nx::agents::Brain brain{threats};
    const auto at = [](float seconds) {
        return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
    };
    brain.perceive(std::array{nx::agents::Observation{fought, {.x = 400}}}, at(0.0F), nx::kDefaultTickRate);
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).target, fought);
    // Lost from sight; a step heard since, nearer and fresher, does not draw it off.
    brain.perceive({}, at(0.1F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(stepping, {.y = 200}, Salience::Low, 200)}, at(0.1F),
                nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).target, fought) << "it keeps after what it fought";
    // A shot fired 250 nearer does.
    brain.perceive({}, at(0.2F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(shooting, {.y = -150}, Salience::High, 150)}, at(0.2F),
                nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).target, shooting) << "a threat much nearer";
}

TEST(AgentBrain, AShotHeardLessThanTheThreatsMarginNearerOrLongAgoDrawsItNotOff) {
    using nx::agents::Salience;
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto fought = world.spawn();
    const auto shooting = world.spawn();
    auto threats = rules();
    threats.memoryLifetime = nx::Seconds{10.0F};
    threats.threatRecency = nx::Seconds{0.5F};
    threats.threatMargin = nx::HammerUnits{100.0F};
    nx::agents::Brain brain{threats};
    const auto at = [](float seconds) {
        return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
    };
    brain.perceive(std::array{nx::agents::Observation{fought, {.x = 300}}}, at(0.0F), nx::kDefaultTickRate);
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).target, fought);
    brain.perceive({}, at(0.1F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(shooting, {.y = -250}, Salience::High, 250)}, at(0.1F),
                nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).target, fought) << "50 nearer, not 100";
    brain.sense(std::array{estimateOf(shooting, {.y = -150}, Salience::High, 150)}, at(0.1F),
                nx::kDefaultTickRate);
    brain.perceive({}, at(1.0F), nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).target, fought) << "a shot heard long ago is no threat";
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
    const auto chase = brain.decide({}, query, kRunSpeed);
    EXPECT_EQ(chase.behavior, nx::agents::Behavior::Chase);
    EXPECT_GT(chase.intent.direction.x, 0);
    const auto attack = brain.decide({.x = 180}, query, kRunSpeed);
    EXPECT_TRUE(attack.intent.attacks);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    EXPECT_FALSE(brain.decide({.x = 180}, query, kRunSpeed).intent.attacks);
}

TEST(AgentBrain, PursuesATargetOnlyItsReactionTimeAfterItFirstSawIt) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Rules reacting = rules();
    reacting.reactionTime = nx::Seconds{0.5F};
    nx::agents::Brain brain{reacting};
    const std::array seen{nx::agents::Observation{.entity = target, .position = {.x = 20}}};
    const auto at = [](float seconds) {
        return nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate);
    };
    brain.perceive(seen, at(0.0F), nx::kDefaultTickRate);
    EXPECT_FALSE(brain.memories().front().isRecognized);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle)
        << "seen, not reacted to";
    brain.perceive(seen, at(0.49F), nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
    brain.perceive({}, at(0.5F), nx::kDefaultTickRate);
    const auto investigated = brain.decide({}, query, kRunSpeed);
    EXPECT_EQ(investigated.behavior, nx::agents::Behavior::Investigate) << "recognized out of sight too";
    EXPECT_EQ(investigated.target, target);
    brain.perceive(seen, at(0.6F), nx::kDefaultTickRate);
    const auto attack = brain.decide({}, query, kRunSpeed);
    EXPECT_EQ(attack.behavior, nx::agents::Behavior::Attack);
    EXPECT_EQ(attack.target, target);
    // Forgotten, it is noticed anew when it is seen again.
    brain.perceive({}, at(2.7F), nx::kDefaultTickRate);
    ASSERT_TRUE(brain.memories().empty());
    brain.perceive(seen, at(2.8F), nx::kDefaultTickRate);
    EXPECT_FALSE(brain.memories().front().isRecognized);
}

TEST(AgentBrain, ReportsAnUnreachableGoalWithoutDirectMovement) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{target, {.x = 5000}}}, {}, nx::kDefaultTickRate);
    const auto decision = brain.decide({}, query, kRunSpeed);
    EXPECT_EQ(decision.behavior, nx::agents::Behavior::Blocked);
    EXPECT_TRUE(decision.navigationError.has_value());
    EXPECT_EQ(decision.intent.direction, nx::Vec3{});
    EXPECT_FALSE(decision.intent.attacks);
}

TEST(AgentBrain, ABodyOnAWallOffTheMeshWalksBackToTheFloorAlongItsWay) {
    // A wall 24 thick and 150 tall on a floor, too thin for the mesh, which the body drops from unhurt.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -512}, {.x = 512, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = -12, .y = -512, .z = -16}, {.x = 12, .y = 256, .z = 150});
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 onTop{.x = 6, .y = -256, .z = 150}; // nearer the side of its goal
    brain.setGoal(nx::Vec3{.x = 400, .y = -256});
    for (int decision = 0; decision < 3; ++decision) {
        const auto moving = brain.decide(onTop, query, kRunSpeed);
        ASSERT_EQ(moving.behavior, nx::agents::Behavior::MoveTo) << "not blocked off the mesh";
        EXPECT_GT(moving.intent.direction.x, 0.0F) << "off the wall, toward its goal";
    }
}

TEST(AgentBrain, MovesToItsGoalWithNoTargetAndForgetsItAsItArrives) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
    const nx::Vec3 goal{.y = 200};
    brain.setGoal(goal);
    const auto moving = brain.decide({}, query, kRunSpeed);
    EXPECT_EQ(moving.behavior, nx::agents::Behavior::MoveTo);
    EXPECT_GT(moving.intent.direction.y, 0.0F);
    EXPECT_FALSE(moving.intent.lookAt.has_value());
    EXPECT_FALSE(moving.intent.attacks);
    EXPECT_EQ(brain.decide(goal, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
    EXPECT_FALSE(brain.goal().has_value());
}

TEST(AgentBrain, FacesAheadAlongItsWayAsItMoves) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.y = 200});
    const nx::agents::Intent intent = brain.decide({}, query, kRunSpeed).intent;
    EXPECT_EQ(intent.ahead, intent.direction) << "until it steers or wiggles";
    EXPECT_FALSE(intent.jumps);
}

/// A box on a floor at 0 from `low` to `high`, which a body's feet walking a step at a time fall off
/// at its edge, and climb onto only once it jumped.
struct Platform {
    nx::Vec3 low{};
    nx::Vec3 high{};

    [[nodiscard]] auto isUnder(nx::Vec3 feet) const noexcept -> bool {
        return feet.x >= low.x && feet.x <= high.x && feet.y >= low.y && feet.y <= high.y;
    }
    /// Where `feet` are a step along `direction`, a body that `jumped` or not: on the box's top over it, on
    /// the floor off it, where they were against its side.
    [[nodiscard]] auto stepped(nx::Vec3 feet, nx::Vec3 direction, bool jumped) const noexcept -> nx::Vec3 {
        constexpr float kStep = 3.0F;
        nx::Vec3 next = feet + (direction * kStep);
        const bool isOnTop = feet.z >= high.z;
        if (!isUnder(next)) {
            next.z = 0.0F;
        } else if (isOnTop || jumped) {
            next.z = high.z;
        } else {
            return feet;
        }
        return next;
    }
};

/// The geometry of `platform` on a floor 1024 by 512.
auto platformGeometry(const Platform& platform) -> nx::navigation::Geometry {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, platform.low, platform.high);
    return geometry;
}

/// Where a walk ended, how many times the body jumped on its way, and whether it arrived.
struct Walked {
    nx::Vec3 feet{};
    int jumps = 0;
    bool isArrived = false;
};

/// The walk of a body of `brain` from `feet` to its goal about `platform`, a step at a time, at most
/// `most` decisions, none of them blocked.
auto walkAbout(const Platform& platform, nx::agents::Brain& brain, nx::navigation::NavigationQuery& query,
               nx::Vec3 feet, int most) -> Walked {
    Walked walked{.feet = feet};
    for (int decision = 0; decision < most; ++decision) {
        const nx::agents::Decision decided = brain.decide(walked.feet, query, kRunSpeed);
        EXPECT_NE(decided.behavior, nx::agents::Behavior::Blocked) << std::format("{}", walked.feet);
        if (decided.behavior == nx::agents::Behavior::Idle) {
            walked.isArrived = true;
            break;
        }
        walked.jumps += decided.intent.jumps ? 1 : 0;
        walked.feet = platform.stepped(walked.feet, decided.intent.direction, walked.jumps > 0);
    }
    return walked;
}

TEST(AgentBrain, ABodyOnABoxItDropsFromWalksOffItAlongItsWay) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 120}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 goal{.x = 900, .y = 256};
    brain.setGoal(goal);
    const Walked walked = walkAbout(box, brain, query, {.x = 512, .y = 256, .z = 120}, 1000);
    EXPECT_TRUE(walked.isArrived);
    EXPECT_EQ(walked.feet.z, 0.0F) << "down on the floor";
    EXPECT_LE(nx::length(walked.feet - goal), rules().arrivalDistance.value() + 3.0F);
    EXPECT_EQ(walked.jumps, 0) << "it walks off a ledge it drops from";
}

TEST(AgentBrain, ABodyAcrossALinkSearchesNoWayUntilItLands) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 120}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 900, .y = 256});
    nx::Vec3 feet{.x = 512, .y = 256, .z = 120};
    for (int decision = 0; decision < 1000 && box.isUnder(feet); ++decision) {
        feet = box.stepped(feet, brain.decide(feet, query, kRunSpeed).intent.direction, false);
    }
    ASSERT_FALSE(box.isUnder(feet)) << "off the box";
    const nx::Vec3 aside{.x = 700, .y = 480}; // on the floor beside the box
    brain.setGoal(aside);
    const nx::Vec3 falling{.x = feet.x, .y = feet.y, .z = box.high.z / 2.0F};
    const nx::Vec3 way = brain.decide(falling, query, kRunSpeed, false).intent.direction;
    EXPECT_GT(way.x, std::abs(way.y)) << "on to where the link lands, not aside";
    const Walked walked = walkAbout(box, brain, query, feet, 1000);
    EXPECT_TRUE(walked.isArrived) << "aside once it landed";
    EXPECT_LE(nx::length(walked.feet - aside), rules().arrivalDistance.value() + 3.0F);
}

TEST(AgentBrain, ABodyFallenOntoAnotherFloorThanItsCorridorsSearchesItsWayAnewFromThere) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 30}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 600, .y = 256, .z = box.high.z});
    std::ignore = brain.decide({.x = 420, .y = 256, .z = box.high.z}, query, kRunSpeed);
    // Off the box's side, on the floor beside it: no way off where it walked, across.
    std::ignore = brain.decide({.x = 370, .y = 256}, query, kRunSpeed);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    ASSERT_FALSE(way.empty());
    EXPECT_LT(way.front().z, box.high.z / 2.0F) << "from the floor it is on";
    EXPECT_NEAR(way.back().z, box.high.z, 4.0F);
}

TEST(AgentBrain, ALinkItFailsTwiceInARowIsOneItsWaysKeepOffWhileTheyHaveAnother) {
    // A jump onto a box, as a ledge with no room for a run at it has it: the body comes back down short of
    // it.
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 30}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 512, .y = 256, .z = box.high.z});
    std::ignore = brain.decide({.x = 300, .y = 256}, query, kRunSpeed);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    ASSERT_GE(way.size(), 3U);
    const nx::Vec3 linkStart = way[1];
    const nx::Vec3 atIt = linkStart - nx::Vec3{.x = 2.0F};
    const auto jumpsAndFallsBack = [&] {
        const nx::agents::Decision atTheLink = brain.decide(atIt, query, kRunSpeed);
        EXPECT_TRUE(atTheLink.intent.jumps);
        std::ignore =
            brain.decide({.x = atIt.x, .y = atIt.y, .z = box.high.z / 2.0F}, query, kRunSpeed, false);
    };
    jumpsAndFallsBack();
    jumpsAndFallsBack(); // once more: once may be another body in the way
    const nx::agents::Decision third = brain.decide(atIt, query, kRunSpeed);
    EXPECT_FALSE(third.intent.jumps) << "not again";
    way.clear();
    brain.appendWay(query, way);
    ASSERT_GE(way.size(), 3U);
    EXPECT_GT(nx::length(way[1] - linkStart), 1.0F) << "by another";
}

TEST(AgentBrain, ALinkItIsKnockedOffIsNoneItFailedHoweverOftenItIs) {
    constexpr int kKnockedOff = 5;
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 30}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 512, .y = 256, .z = box.high.z});
    std::ignore = brain.decide({.x = 300, .y = 256}, query, kRunSpeed);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    ASSERT_GE(way.size(), 3U);
    const nx::Vec3 atIt = way[1] - nx::Vec3{.x = 2.0F};
    const auto jumpsAndFallsBack = [&](bool isKnockedOff) {
        const nx::agents::Decision atTheLink = brain.decide(atIt, query, kRunSpeed);
        EXPECT_TRUE(atTheLink.intent.jumps) << "at it again";
        if (isKnockedOff) {
            brain.noteKnockedBack();
        }
        std::ignore =
            brain.decide({.x = atIt.x, .y = atIt.y, .z = box.high.z / 2.0F}, query, kRunSpeed, false);
    };
    for (int time = 0; time < kKnockedOff; ++time) {
        jumpsAndFallsBack(true);
    }
    // Falling of itself, twice in a row, it failed it as ever.
    jumpsAndFallsBack(false);
    jumpsAndFallsBack(false);
    EXPECT_FALSE(brain.decide(atIt, query, kRunSpeed).intent.jumps) << "not again";
}

TEST(AgentBrain, ABodyOnATargetsHeadStrikesItsEyesThoughItsFeetAreOutOfReach) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    const float reach = rules().attackDistance.value();
    brain.perceive(std::array{nx::agents::Observation{.entity = target,
                                                      .position = {.z = -2.0F * reach},
                                                      .aimAt = nx::Vec3{.z = -reach / 2.0F}}},
                   {}, nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Attack);
}

TEST(AgentBrain, ABodyJumpsUpALedgeOfItsCrouchedJumpDuckingUntilItLandsAndWalksOffItUpright) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 50}};
    nx::navigation::AgentProfile jumping = nx::test::playerProfile();
    jumping.crouchedJumpHeight = nx::HammerUnits{58.0F};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0, jumping);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 512, .y = 256, .z = box.high.z});
    nx::Vec3 feet{.x = 100, .y = 256};
    int jumps = 0;
    bool wasCrouchedAtTheJump = false;
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    for (int step = 0; step < 1000 && feet.z < box.high.z; ++step) {
        if (decision.intent.jumps) {
            ++jumps;
            wasCrouchedAtTheJump = decision.intent.crouches;
        } else if (jumps == 0) {
            EXPECT_FALSE(decision.intent.crouches) << "walking up to it";
        } else {
            EXPECT_TRUE(decision.intent.crouches) << "in the air";
        }
        feet = box.stepped(feet, decision.intent.direction, jumps > 0);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    EXPECT_EQ(jumps, 1);
    EXPECT_TRUE(wasCrouchedAtTheJump);
    EXPECT_EQ(feet.z, box.high.z);
    const Walked arrived = walkAbout(box, brain, query, feet, 1000);
    ASSERT_TRUE(arrived.isArrived);
    feet = arrived.feet;
    // Back down off it, none of that.
    brain.setGoal(nx::Vec3{.x = 100, .y = 256});
    for (int step = 0; step < 1000 && feet.z > 0.0F; ++step) {
        decision = brain.decide(feet, query, kRunSpeed);
        EXPECT_FALSE(decision.intent.jumps);
        EXPECT_FALSE(decision.intent.crouches);
        feet = box.stepped(feet, decision.intent.direction, false);
    }
    EXPECT_EQ(feet.z, 0.0F);
}

TEST(AgentBrain, ABodyComesUpToACrouchedJumpStandingThoughLowRoomLiesBeforeIt) {
    // The box of a crouched jump at x = 384, and a ceiling 50 up over the floor short of its start.
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 50}};
    nx::navigation::Geometry geometry = platformGeometry(box);
    nx::test::addCeiling(geometry, {.x = 0, .y = -32}, {.x = 330, .y = 544}, 50);
    nx::navigation::AgentProfile jumping = nx::test::playerProfile();
    jumping.crouchedJumpHeight = nx::HammerUnits{58.0F};
    const auto mesh = nx::test::playerMesh(geometry, 0, jumping);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 512, .y = 256, .z = box.high.z});
    nx::Vec3 feet{.x = 100, .y = 256};
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    EXPECT_TRUE(decision.intent.crouches) << "under the ceiling, far from the jump";
    for (int step = 0; step < 1000 && feet.x < 300.0F; ++step) {
        feet = feet + (decision.intent.direction * 2.0F);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    ASSERT_GE(feet.x, 300.0F) << "on its way to the jump";
    EXPECT_FALSE(decision.intent.crouches) << "still under it, near the jump";
    for (int step = 0; step < 100 && !decision.intent.jumps; ++step) {
        feet = feet + (decision.intent.direction * 2.0F);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    ASSERT_TRUE(decision.intent.jumps);
    EXPECT_TRUE(decision.intent.crouches) << "from the jump on";
}

TEST(AgentBrain, ABodyHoldsItsDuckKeyFromAheadOfLowRoomUntilItIsPastIt) {
    // A floor with a ceiling 50 units up from x = 512 to 768, all of it the way.
    constexpr float kFrom = 512.0F;
    constexpr float kTo = 768.0F;
    constexpr float kLead = 96.0F; // kCrouchLeadRadii
    constexpr float kStep = 2.0F;
    constexpr float kCorner = 8.0F;
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1280, .y = 256}, 0);
    nx::test::addCeiling(geometry, {.x = kFrom, .y = -64}, {.x = kTo, .y = 320}, 50);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 1200, .y = 128});
    std::optional<float> firstDucked;
    std::optional<float> lastDucked;
    nx::Vec3 feet{.x = 100, .y = 128};
    for (int step = 0; step < 1000 && feet.x < 1190.0F; ++step) {
        const nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
        if (decision.intent.crouches) {
            firstDucked = firstDucked.value_or(feet.x);
            lastDucked = feet.x;
        }
        feet = feet + (decision.intent.direction * kStep);
    }
    ASSERT_TRUE(firstDucked.has_value());
    EXPECT_NEAR(*firstDucked, kFrom - kLead, kCorner);
    EXPECT_NEAR(*lastDucked, kTo, kCorner);
}

TEST(AgentBrain, ACrouchingBodyAsksAllItsSpeedHoweverItSlowsForWhatItApproaches) {
    constexpr float kFullSpeed = 250.0F;
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 512, .y = 256}, 0);
    nx::test::addCeiling(geometry, {.x = 128, .y = -64}, {.x = 576, .y = 320}, 50);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Rules braking = rules();
    braking.arrivalDistance = nx::HammerUnits{8.0F};
    braking.braking = 4.0F;
    braking.stopSpeed = nx::UnitsPerSecond{75.0F};
    braking.acceleration = 5.0F;
    nx::agents::Brain brain{braking};
    // Its way ends 16 units on: standing, it would ask a crawl of that.
    brain.setGoal(nx::Vec3{.x = 316, .y = 128});
    const nx::Vec3 feet{.x = 300, .y = 128};
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    ASSERT_TRUE(decision.intent.crouches);
    brain.steer(decision.intent, {}, nx::UnitsPerSecond{kFullSpeed}, {}, query);
    EXPECT_FLOAT_EQ(decision.intent.speedShare, 1.0F);
}

TEST(AgentBrain, ABodyHoldsItsDuckKeyNearALinksEndOnLowRoom) {
    // The building of ladderMesh, with no more than a crouched body's room on its roof.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = 512, .y = -32, .z = -16}, {.x = 1024, .y = 544, .z = 256});
    nx::test::addCeiling(geometry, {.x = 512, .y = -32}, {.x = 1024, .y = 544}, 306);
    geometry.ladders.push_back({.min = {.x = 500, .y = 240}, .max = {.x = 512, .y = 272, .z = 256}});
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 800, .y = 256, .z = 256});
    nx::Vec3 feet{.x = 300, .y = 256};
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    for (int step = 0; step < 1000 && !decision.intent.climb; ++step) {
        EXPECT_FALSE(decision.intent.crouches) << "on the floor at its foot";
        feet = feet + (decision.intent.direction * 2.0F);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    ASSERT_TRUE(decision.intent.climb.has_value());
    EXPECT_FALSE(brain.decide({.x = 490, .y = 256, .z = 100}, query, kRunSpeed).intent.crouches)
        << "far below its end";
    EXPECT_TRUE(brain.decide({.x = 490, .y = 256, .z = 220}, query, kRunSpeed).intent.crouches)
        << "near its end";
}

TEST(AgentBrain, ABodyHoldsItsDuckKeyAtTheFootOfALadderIntoLowRoomNotAtItsTop) {
    // The building of ladderMesh, with no more than a crouched body's room on the floor at its foot.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = 512, .y = -32, .z = -16}, {.x = 1024, .y = 544, .z = 256});
    nx::test::addCeiling(geometry, {.x = 0, .y = -32}, {.x = 512, .y = 544}, 50);
    geometry.ladders.push_back({.min = {.x = 500, .y = 240}, .max = {.x = 512, .y = 272, .z = 256}});
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 200, .y = 256});
    nx::Vec3 feet{.x = 800, .y = 256, .z = 256};
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    for (int step = 0; step < 1000 && !decision.intent.climb; ++step) {
        EXPECT_FALSE(decision.intent.crouches) << "on the roof";
        feet = feet + (decision.intent.direction * 2.0F);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    ASSERT_TRUE(decision.intent.climb.has_value());
    EXPECT_FALSE(decision.intent.crouches) << "at its top";
    EXPECT_FALSE(brain.decide({.x = 490, .y = 256, .z = 100}, query, kRunSpeed).intent.crouches)
        << "on its way down";
    EXPECT_TRUE(brain.decide({.x = 490, .y = 256, .z = 10}, query, kRunSpeed).intent.crouches)
        << "at its foot";
}

/// A floor and a building on it, 256 tall, with a ladder up its wall at y = 256.
auto ladderMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = 512, .y = -32, .z = -16}, {.x = 1024, .y = 544, .z = 256});
    geometry.ladders.push_back({.min = {.x = 500, .y = 240}, .max = {.x = 512, .y = 272, .z = 256}});
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentBrain, ABodyClimbsALadderFacingItLevelAndStepsOffAtTheTop) {
    const auto mesh = ladderMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 800, .y = 256, .z = 256});
    // To the ladder's foot along its way.
    nx::Vec3 feet{.x = 300, .y = 256};
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    EXPECT_FALSE(decision.intent.climb.has_value()) << "far from it";
    for (int step = 0; step < 1000 && !decision.intent.climb; ++step) {
        feet = feet + (decision.intent.direction * 2.0F);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    ASSERT_TRUE(decision.intent.climb.has_value()) << "at its foot";
    EXPECT_LT(feet.x, 500.0F);
    EXPECT_GT(decision.intent.direction.x, 0.9F) << "toward the ladder";
    EXPECT_FLOAT_EQ(decision.intent.climb->z, 0.0F) << "level: what goes into its face goes up it";
    EXPECT_FALSE(decision.intent.jumps) << "a jump would push it off";
    // Up it, facing it.
    feet = {.x = 490, .y = 256, .z = 150};
    decision = brain.decide(feet, query, kRunSpeed);
    ASSERT_TRUE(decision.intent.climb.has_value());
    EXPECT_GT(decision.intent.direction.x, 0.9F);
    // At the top, on to where its way goes on, its key still held for as long as the ladder holds it.
    feet = {.x = 490, .y = 256, .z = 250};
    decision = brain.decide(feet, query, kRunSpeed);
    ASSERT_TRUE(decision.intent.climb.has_value());
    EXPECT_GT(decision.intent.direction.x, 0.9F);
    // Off it, on the roof, it walks on.
    feet = {.x = 560, .y = 256, .z = 256};
    decision = brain.decide(feet, query, kRunSpeed);
    EXPECT_FALSE(decision.intent.climb.has_value());
    EXPECT_GT(nx::length(decision.intent.direction), 0.0F);
}

TEST(AgentBrain, ABodyComesDownALadderWalkingOverToItLookingBelowLevel) {
    const auto mesh = ladderMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 200, .y = 256});
    nx::Vec3 feet{.x = 800, .y = 256, .z = 256};
    nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
    for (int step = 0; step < 1000 && !decision.intent.climb; ++step) {
        feet = feet + (decision.intent.direction * 2.0F);
        decision = brain.decide(feet, query, kRunSpeed);
    }
    ASSERT_TRUE(decision.intent.climb.has_value()) << "at the roof's edge";
    EXPECT_GT(feet.x, 512.0F);
    EXPECT_LT(decision.intent.direction.x, -0.9F) << "over toward the ladder and the floor past it";
    EXPECT_LT(decision.intent.climb->z, -0.8F) << "well below level";
    EXPECT_LT(decision.intent.climb->x, 0.0F);
    // Past its face, still coming down the same way.
    feet = {.x = 490, .y = 256, .z = 120};
    decision = brain.decide(feet, query, kRunSpeed);
    ASSERT_TRUE(decision.intent.climb.has_value());
    EXPECT_LT(decision.intent.direction.x, -0.9F);
    // On the floor, past where the link lands, it walks on.
    feet = {.x = 450, .y = 256};
    decision = brain.decide(feet, query, kRunSpeed);
    EXPECT_FALSE(decision.intent.climb.has_value());
}

TEST(AgentBrain, ABodyOffTheMeshJumpsAndSearchesAnewAsSoonAsItIsBackOnIt) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 goal{.x = 200, .y = 0};
    brain.setGoal(goal);
    const nx::Vec3 inTheAir{.x = 0, .y = 0, .z = 400};
    const nx::agents::Decision off = brain.decide(inTheAir, query, kRunSpeed);
    EXPECT_EQ(off.behavior, nx::agents::Behavior::Blocked);
    EXPECT_EQ(off.navigationError, nx::navigation::NavigationError::OutsideMesh);
    EXPECT_TRUE(off.intent.jumps) << "a jump lets go of a ladder";
    // Within the replan distance across of where it failed: nothing but its being there changed.
    const nx::agents::Decision back = brain.decide({.x = 0, .y = 0, .z = 0}, query, kRunSpeed);
    EXPECT_EQ(back.behavior, nx::agents::Behavior::MoveTo);
    EXPECT_GT(nx::length(back.intent.direction), 0.0F);
    EXPECT_FALSE(back.intent.jumps);
}

TEST(AgentBrain, ABodyBelowABoxItJumpsOntoJumpsOnceAndWalksOnOnItsTop) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 40}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 goal{.x = 512, .y = 256, .z = 40};
    brain.setGoal(goal);
    const Walked walked = walkAbout(box, brain, query, {.x = 100, .y = 256}, 1000);
    EXPECT_TRUE(walked.isArrived);
    EXPECT_EQ(walked.feet.z, 40.0F) << "up on the box";
    EXPECT_LE(nx::length(walked.feet - goal), rules().arrivalDistance.value() + 3.0F);
    EXPECT_EQ(walked.jumps, 1);
}

/// A box 40 high on a floor, a body's way onto its top from far off, and the link of that way, which leaves
/// the floor along +x.
struct JumpUp {
    Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 40}};
    nx::Vec3 from{.x = 100, .y = 256};
    nx::Vec3 goal{.x = 512, .y = 360, .z = 40};
    nx::navigation::NavigationMesh mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    nx::navigation::MeshLink link = wayUp();

    JumpUp() { brain.setGoal(goal); }

    /// A body's feet at the link's start, a unit short of it as a body comes to it.
    [[nodiscard]] auto atStart() const -> nx::Vec3 { return link.start - nx::Vec3{.x = 1}; }

private:
    [[nodiscard]] auto wayUp() -> nx::navigation::MeshLink {
        auto corridor = nx::navigation::Corridor::plan(query, {.from = from, .to = goal});
        EXPECT_TRUE(corridor.has_value());
        const auto next = corridor->nextLink(query);
        EXPECT_TRUE(next.has_value());
        EXPECT_GT(next->end.x, next->start.x);
        return *next;
    }
};

TEST(AgentBrain, ABodyHasCrossedALinkOnlyOnTheGround) {
    JumpUp up;
    ASSERT_TRUE(up.brain.decide(up.atStart(), up.query, kRunSpeed).intent.jumps);
    // Along the box's face, as high as its top and as far from the link's start as its end.
    const float span = nx::test::distanceAcross(up.link.start, up.link.end);
    const nx::Vec3 alongTheFace = up.link.start + nx::Vec3{.y = span, .z = up.link.end.z - up.link.start.z};
    const nx::agents::Decision flying = up.brain.decide(alongTheFace, up.query, kRunSpeed, false);
    EXPECT_GT(flying.intent.direction.x, 0.0F);
    EXPECT_LT(flying.intent.direction.y, 0.0F) << "still for the link's end, not on along its way";
    const nx::Vec3 over = up.link.end + nx::Vec3{.x = 10};
    EXPECT_LT(up.brain.decide(over, up.query, kRunSpeed, false).intent.direction.x, 0.0F)
        << "still for the link's end";
    EXPECT_GT(up.brain.decide(over, up.query, kRunSpeed).intent.direction.y, 0.0F)
        << "landed, on to its goal";
}

TEST(AgentBrain, ABodyBackOnTheGroundBelowALinksEndSearchesItsWayAnewAndJumpsAgain) {
    JumpUp up;
    ASSERT_TRUE(up.brain.decide(up.atStart(), up.query, kRunSpeed).intent.jumps);
    EXPECT_FALSE(up.brain.decide(up.atStart(), up.query, kRunSpeed).intent.jumps) << "jumping still";
    EXPECT_FALSE(up.brain.decide(up.atStart() + nx::Vec3{.z = 30}, up.query, kRunSpeed, false).intent.jumps);
    EXPECT_TRUE(up.brain.decide(up.atStart(), up.query, kRunSpeed).intent.jumps) << "fallen short, once more";
}

TEST(AgentBrain, ABodyBackOnTheGroundAtTheHeightOfALinksEndShortOfItWalksOnToItUpright) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 50}};
    nx::navigation::AgentProfile jumping = nx::test::playerProfile();
    jumping.crouchedJumpHeight = nx::HammerUnits{58.0F};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0, jumping);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 goal{.x = 512, .y = 360, .z = box.high.z};
    brain.setGoal(goal);
    auto corridor = nx::navigation::Corridor::plan(query, {.from = {.x = 100, .y = 256}, .to = goal});
    ASSERT_TRUE(corridor.has_value());
    const auto link = corridor->nextLink(query);
    ASSERT_TRUE(link.has_value());
    ASSERT_EQ(link->kind, nx::navigation::AreaKind::CrouchedJump);
    const nx::agents::Decision jump = brain.decide(link->start - nx::Vec3{.x = 1}, query, kRunSpeed);
    ASSERT_TRUE(jump.intent.jumps);
    EXPECT_TRUE(jump.intent.crouches);
    const nx::Vec3 over{.x = link->start.x, .y = link->start.y, .z = link->end.z};
    EXPECT_TRUE(brain.decide(over, query, kRunSpeed, false).intent.crouches) << "in the air";
    const nx::agents::Decision landed = brain.decide(over, query, kRunSpeed);
    EXPECT_FALSE(landed.intent.jumps) << "up there already";
    EXPECT_FALSE(landed.intent.crouches);
    EXPECT_GT(landed.intent.direction.x, 0.99F) << "on to the link's end, no way searched from the edge";
}

TEST(AgentBrain, ABodyBackPastACornerItReachedHeadsForTheCornersItSeesAgain) {
    // A passage whose walls each turn its way, more corners than a body sees ahead.
    constexpr float kWallEvery = 200.0F;
    constexpr int kWalls = 6;
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = kWallEvery * (kWalls + 1), .y = 256}, 0);
    for (int wall = 1; wall <= kWalls; ++wall) {
        const float x = kWallEvery * static_cast<float>(wall);
        const bool isLow = wall % 2 == 0;
        nx::test::addBox(geometry, {.x = x, .y = isLow ? -32.0F : 96.0F, .z = -16},
                         {.x = x + 32, .y = isLow ? 160.0F : 288.0F, .z = 200});
    }
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = kWallEvery * (kWalls + 0.5F), .y = 128});
    constexpr float kStep = 2.0F;
    constexpr float kPastTheLastWall = 40.0F;
    nx::Vec3 feet{.x = 100, .y = 128};
    std::vector<nx::Vec3> walked{feet};
    for (int step = 0; step < 5000 && feet.x < (kWallEvery * kWalls) + kPastTheLastWall; ++step) {
        const nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
        ASSERT_EQ(decision.behavior, nx::agents::Behavior::MoveTo);
        feet = feet + (decision.intent.direction * kStep);
        walked.push_back(feet);
    }
    ASSERT_GE(feet.x, (kWallEvery * kWalls) + kPastTheLastWall);
    // Back the way it came, as what gets a stuck body out may take it: it never stands with nothing to head
    // for.
    for (const nx::Vec3 back : walked | std::views::reverse) {
        const nx::agents::Decision decision = brain.decide(back, query, kRunSpeed);
        ASSERT_EQ(decision.behavior, nx::agents::Behavior::MoveTo);
        EXPECT_GT(nx::length(decision.intent.direction), 0.0F) << std::format("{}", back);
    }
}

/// Where a body got stuck, as its watch tells it: the polygons within 75 units cost twenty times their own.
auto stuckAt(nx::Vec3 position) -> nx::navigation::AvoidedPlace {
    return {.position = position, .radius = nx::HammerUnits{75.0F}, .cost = 20.0F};
}

/// A corridor along y = 0 to 256 and another along y = 512 to 768, joined at both ends, with nothing
/// between them: two ways from one end to the other.
auto twoWaysMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 256, .y = 768}, 0);
    nx::test::addFloor(geometry, {.x = 256, .y = 0}, {.x = 768, .y = 256}, 0);
    nx::test::addFloor(geometry, {.x = 256, .y = 512}, {.x = 768, .y = 768}, 0);
    nx::test::addFloor(geometry, {.x = 768, .y = 0}, {.x = 1024, .y = 768}, 0);
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentBrain, GivingUpItSearchesAnotherWayAroundWhereItGotStuckThenGivesUpItsGoal) {
    const auto mesh = twoWaysMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 from{.x = 128, .y = 128};
    const nx::Vec3 goal{.x = 896, .y = 128};
    const auto wayFrom = [&](nx::Vec3 feet) {
        std::ignore = brain.decide(feet, query, kRunSpeed);
        std::vector<nx::Vec3> way;
        brain.appendWay(query, way);
        return way;
    };
    brain.setGoal(goal);
    const std::vector<nx::Vec3> straight = wayFrom(from);
    ASSERT_FALSE(straight.empty());
    EXPECT_TRUE(std::ranges::all_of(straight, [](nx::Vec3 point) { return point.y < 256; }))
        << "the near way";

    // Stuck in the near corridor, on its way: once, it searches the far one, its goal kept.
    const nx::Vec3 jammed{.x = 512, .y = 128};
    brain.giveUp(stuckAt(jammed), 1);
    ASSERT_TRUE(brain.goal().has_value());
    const std::vector<nx::Vec3> around = wayFrom(from);
    EXPECT_TRUE(std::ranges::any_of(around, [](nx::Vec3 point) { return point.y > 384; })) << "the far way";
    // Stuck again: it gives its goal up, and pursues nothing for its owner to give it another.
    brain.giveUp(stuckAt({.x = 128, .y = 600}), 1);
    EXPECT_FALSE(brain.goal().has_value());
    EXPECT_EQ(brain.decide(from, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
    // A goal given anew is pursued anew, by the near way again.
    brain.setGoal(goal);
    EXPECT_TRUE(std::ranges::all_of(wayFrom(from), [](nx::Vec3 point) { return point.y < 256; }));
}

TEST(AgentBrain, ABodyAwayFromItsCorridorSearchesItsWayAnewFromWhereItIs) {
    const auto mesh = twoWaysMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    const nx::Vec3 goal{.x = 896, .y = 128};
    brain.setGoal(goal);
    ASSERT_EQ(brain.decide({.x = 512, .y = 128}, query, kRunSpeed).behavior, nx::agents::Behavior::MoveTo);
    // Spawned again across the hole from where its corridor is, alive all along: its corridor cannot
    // follow it there along the mesh.
    const nx::Vec3 elsewhere{.x = 512, .y = 640};
    const nx::agents::Decision decision = brain.decide(elsewhere, query, kRunSpeed);
    EXPECT_EQ(decision.behavior, nx::agents::Behavior::MoveTo);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    ASSERT_FALSE(way.empty());
    EXPECT_LT(nx::test::distanceAcross(way.front(), elsewhere), 1.0F) << "from where it is";
    EXPECT_LT(nx::test::distanceAcross(way.back(), goal), 1.0F);
    EXPECT_GT(decision.intent.direction.x, 0.0F) << "along the far corridor, not at the hole";
}

TEST(AgentBrain, GivingUpWithNoWayAroundItKeepsTheOnlyWayAndThenTheTargetItInvestigatesIsForgotten) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto lost = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{lost, {.x = 200}}}, {}, nx::kDefaultTickRate);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Investigate);
    brain.giveUp(stuckAt({.x = 100}), 1);
    EXPECT_EQ(brain.memories().size(), 1U) << "investigated still, by another way";
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Investigate);
    brain.giveUp(stuckAt({.x = 100}), 1);
    EXPECT_TRUE(brain.memories().empty());
    EXPECT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
}

TEST(AgentBrain, GivingUpItNeverGivesUpATargetItSeesAndSearchesItsWayAnew) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto chased = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{chased, {.x = 200}}}, {}, nx::kDefaultTickRate);
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Chase);
    for (int stuck = 0; stuck < 3; ++stuck) {
        brain.giveUp(stuckAt({.x = 100}), 1);
        EXPECT_EQ(brain.memories().size(), 1U) << "it still sees it";
    }
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    EXPECT_TRUE(way.empty()) << "its corridor dropped";
    const nx::Vec3 elsewhere{.y = 100};
    const auto chase = brain.decide(elsewhere, query, kRunSpeed);
    EXPECT_EQ(chase.behavior, nx::agents::Behavior::Chase);
    brain.appendWay(query, way);
    ASSERT_FALSE(way.empty());
    EXPECT_NEAR(way.front().x, elsewhere.x, 1.0F) << "searched from where it is";
    EXPECT_NEAR(way.front().y, elsewhere.y, 1.0F);
}

TEST(AgentBrain, ItsWayGoesFromWhereItIsToItsGoal) {
    constexpr float kOnTheMesh = 1.0F;
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    EXPECT_TRUE(way.empty()) << "no corridor before it decides";
    const nx::Vec3 feet{.x = 10};
    const nx::Vec3 goal{.y = 200};
    brain.setGoal(goal);
    ASSERT_EQ(brain.decide(feet, query, kRunSpeed).behavior, nx::agents::Behavior::MoveTo);
    brain.appendWay(query, way);
    ASSERT_GE(way.size(), 2U);
    EXPECT_LT(nx::test::distanceAcross(way.front(), feet), kOnTheMesh);
    EXPECT_LT(nx::test::distanceAcross(way.back(), goal), kOnTheMesh);
}

TEST(AgentBrain, AnAgentHasAWayOnlyWhileItsLastDecisionFollowsTheCorridor) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Agent agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()};
    const nx::Vec3 goal{.y = 200};
    agent.brain.setGoal(goal);
    const auto wayOf = [&query](const auto& follower) {
        std::vector<nx::Vec3> points;
        follower.appendWay(query, points);
        return points;
    };
    agent.behavior = agent.brain.decide({}, query, kRunSpeed).behavior;
    ASSERT_TRUE(nx::agents::followsCorridor(agent.behavior));
    EXPECT_FALSE(wayOf(agent).empty());
    EXPECT_EQ(wayOf(agent), wayOf(agent.brain));
    agent.behavior = agent.brain.decide(goal, query, kRunSpeed).behavior;
    ASSERT_EQ(agent.behavior, nx::agents::Behavior::Idle);
    EXPECT_FALSE(wayOf(agent.brain).empty()) << "the corridor it arrived by is kept";
    EXPECT_TRUE(wayOf(agent).empty());
}

TEST(AgentBrain, ATargetItSeesComesBeforeItsGoal) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.y = 200});
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::MoveTo);
    brain.perceive(std::array{nx::agents::Observation{target, {.x = -200}}}, {}, nx::kDefaultTickRate);
    const auto chase = brain.decide({}, query, kRunSpeed);
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
    ASSERT_EQ(brain.decide({}, query, kRunSpeed).behavior, nx::agents::Behavior::Chase);
    const nx::Vec3 shiftedGoal{.x = originalGoal.x + rules().replanDistance.value() / 2.0F};
    brain.perceive(std::array{nx::agents::Observation{.entity = target, .position = shiftedGoal}}, {},
                   nx::kDefaultTickRate);
    brain.perceive({}, {}, nx::kDefaultTickRate);
    const auto investigation = brain.decide(originalGoal, query, kRunSpeed);
    EXPECT_EQ(investigation.behavior, nx::agents::Behavior::Investigate);
    EXPECT_GT(investigation.intent.direction.x, 0.0F);
    EXPECT_FALSE(brain.memories().empty());
    EXPECT_EQ(brain.decide(shiftedGoal, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
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
    const auto decision = brain.decide(feet, query, kRunSpeed);
    EXPECT_EQ(decision.behavior, nx::agents::Behavior::Idle);
    EXPECT_EQ(decision.intent.direction, nx::Vec3{});
    EXPECT_TRUE(brain.memories().empty());
}

/// A body that walks as a GoldSrc player walks on flat ground (PM_Friction, then PM_Accelerate): its speed
/// falls by four times itself a second, and by no less than a stop speed's 75 four times, and it gains its
/// wished velocity at five times the wished speed a second; it cannot turn on the spot.
struct InertialBody {
    static constexpr float kFriction = 4.0F;
    static constexpr float kStopSpeed = 75.0F;
    static constexpr float kAcceleration = 5.0F;

    nx::Vec3 feet{};
    nx::Vec3 velocity{};

    void walk(const nx::agents::Intent& intent, float maxSpeed, float seconds) {
        if (const float speed = nx::length(velocity); speed > 0.0F) {
            const float dropped = std::max(speed, kStopSpeed) * kFriction * seconds;
            velocity = velocity * (std::max(speed - dropped, 0.0F) / speed);
        }
        const nx::Vec3 driven = intent.drivenWay();
        const float wished = nx::length(driven) > 0.0F ? maxSpeed * intent.speedShare : 0.0F;
        const float gained = wished - nx::dot(velocity, driven);
        if (wished > 0.0F && gained > 0.0F) {
            velocity = velocity + (driven * std::min(kAcceleration * wished * seconds, gained));
        }
        feet = feet + (velocity * seconds);
    }
};

/// How a walk of an inertial body went: whether it arrived, after how long, and how far its way turned all
/// told, in radians.
struct InertialWalk {
    bool isArrived = false;
    float seconds = 0.0F;
    float turned = 0.0F;
};

/// The walk of `body`, a zombie's at 160 units a second, to the goal of `brain`, decided and steered 100
/// times a second for at most 10 seconds.
auto walkInertially(nx::agents::Brain& brain, nx::navigation::NavigationQuery& query, InertialBody body)
    -> InertialWalk {
    constexpr float kMaxSpeed = 160.0F;
    constexpr int kTicks = 1000;
    constexpr float kTick = 0.01F;
    constexpr float kTurning = 5.0F; ///< Slower than this, its way turns no way.
    InertialWalk walked;
    std::optional<float> heading;
    for (int tick = 0; tick < kTicks; ++tick) {
        nx::agents::Decision decision =
            brain.decide(body.feet, query, nx::UnitsPerSecond{kMaxSpeed}, true, body.velocity);
        if (decision.behavior == nx::agents::Behavior::Idle) {
            walked.isArrived = true;
            break;
        }
        if (nx::length(decision.intent.direction) > 0.0F) {
            brain.steer(decision.intent, body.velocity, nx::UnitsPerSecond{kMaxSpeed}, {}, query);
        }
        body.walk(decision.intent, kMaxSpeed, kTick);
        walked.seconds += kTick;
        if (nx::length(body.velocity) > kTurning) {
            const float now = std::atan2(body.velocity.y, body.velocity.x);
            if (heading) {
                walked.turned += std::abs(std::remainder(now - *heading, 2.0F * std::numbers::pi_v<float>));
            }
            heading = now;
        }
    }
    return walked;
}

/// The rules of a body that cannot turn on the spot, as a zombie: arriving within 8 units, braking as the
/// ground's friction stops it, its walks gaining it speed as fast as the ground's acceleration gives it.
auto brakingRules() -> nx::agents::Rules {
    nx::agents::Rules braking = rules();
    braking.arrivalDistance = nx::HammerUnits{8.0F};
    braking.braking = InertialBody::kFriction;
    braking.stopSpeed = nx::UnitsPerSecond{InertialBody::kStopSpeed};
    braking.acceleration = InertialBody::kAcceleration;
    return braking;
}

/// A gap of 100 between two floors from x = 512 on: a player's box spans 32 of it, and a leap 41.8 high is
/// 0.646 seconds in the air, so it takes 105 units a second across.
constexpr float kGap = 100.0F;

/// Two floors kGap apart, nothing under it.
auto gapMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 512, .y = 512}, 0);
    nx::test::addFloor(geometry, {.x = 512.0F + kGap, .y = 0}, {.x = 1024.0F + kGap, .y = 512}, 0);
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentBrain, ABodyTooSlowForAGapBacksOffForARunAtItAndJumpsItAtSpeed) {
    const auto mesh = gapMesh();
    nx::navigation::NavigationQuery query{mesh};
    const nx::Vec3 goal{.x = 900, .y = 256};
    auto planned = nx::navigation::Corridor::plan(query, {.from = {.x = 400, .y = 256}, .to = goal});
    ASSERT_TRUE(planned.has_value());
    const auto link = planned->nextLink(query);
    ASSERT_TRUE(link.has_value());
    ASSERT_EQ(link->kind, nx::navigation::AreaKind::Jump);

    // Within arrival of its start: right on it, Detour's way goes on past it with no sign of the link.
    const nx::Vec3 atTheEdge = link->start - nx::Vec3{.x = 2.0F};
    nx::agents::Brain slow{brakingRules()};
    slow.setGoal(goal);
    const nx::agents::Decision backing = slow.decide(atTheEdge, query, kRunSpeed);
    EXPECT_FALSE(backing.intent.jumps);
    EXPECT_TRUE(slow.isRunningUp());
    EXPECT_LT(backing.intent.direction.x, 0.0F) << "back from the edge";

    // Stopped on its way back, by what the mesh does not tell of, it goes at the gap with what run it has.
    constexpr nx::Vec3 kStopped{};
    const nx::Vec3 pressedBack = atTheEdge - nx::Vec3{.x = 24.0F};
    bool jumpedAtLast = false;
    for (int decision = 0; decision < 12 && !jumpedAtLast; ++decision) {
        nx::Vec3 feet = pressedBack;
        if (!slow.isRunningUp()) {
            feet = atTheEdge; // on to the edge again, at last
        }
        jumpedAtLast = slow.decide(feet, query, kRunSpeed, true, kStopped).intent.jumps;
    }
    EXPECT_TRUE(jumpedAtLast);

    nx::agents::Brain fast{brakingRules()};
    fast.setGoal(goal);
    constexpr nx::Vec3 kAtSpeed{.x = kRunSpeed.value()};
    const nx::agents::Decision leaping = fast.decide(atTheEdge, query, kRunSpeed, true, kAtSpeed);
    EXPECT_TRUE(leaping.intent.jumps);
    EXPECT_FALSE(fast.isRunningUp());
    EXPECT_GT(leaping.intent.direction.x, 0.0F) << "on across";
}

TEST(AgentBrain, ABodyThatRunsTooSlowlyForAGapSearchesNoWayOverItAndOneFastEnoughDoes) {
    const auto mesh = gapMesh();
    nx::navigation::NavigationQuery query{mesh};
    constexpr nx::UnitsPerSecond kTooSlow{100.0F};
    const nx::Vec3 here{.x = 400, .y = 256};
    const nx::Vec3 goal{.x = 512.0F + kGap + 256.0F, .y = 256};
    nx::agents::Brain slow{brakingRules()};
    slow.setGoal(goal);
    const nx::agents::Decision blocked = slow.decide(here, query, kTooSlow);
    EXPECT_EQ(blocked.behavior, nx::agents::Behavior::Blocked);
    EXPECT_EQ(blocked.navigationError, nx::navigation::NavigationError::Unreachable);
    nx::agents::Brain fast{brakingRules()};
    fast.setGoal(goal);
    EXPECT_EQ(fast.decide(here, query, kRunSpeed).behavior, nx::agents::Behavior::MoveTo);
}

TEST(AgentBrain, ABodyHeldAtARoundsStartSearchesItsWayAnewOnceItRuns) {
    // A frozen round's start holds a player to 1 unit a second (cs::kFreezeSpeed): no gap it leaps.
    const auto mesh = gapMesh();
    nx::navigation::NavigationQuery query{mesh};
    constexpr nx::UnitsPerSecond kFrozen{1.0F};
    const nx::Vec3 here{.x = 400, .y = 256};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 900, .y = 256});
    EXPECT_EQ(brain.decide(here, query, kFrozen).behavior, nx::agents::Behavior::Blocked);
    // Where it stood, its goal where it was: only how fast it runs tells it to search again.
    EXPECT_EQ(brain.decide(here, query, kRunSpeed).behavior, nx::agents::Behavior::MoveTo);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    ASSERT_FALSE(way.empty());
    EXPECT_GT(way.back().x, 512.0F + kGap) << "over the gap";
}

void expectApproachToLink(const nx::agents::Decision& decision, nx::Vec3 start,
                          const nx::navigation::MeshLink& next) {
    constexpr float kDistanceTolerance = 1.0F;
    ASSERT_EQ(decision.behavior, nx::agents::Behavior::MoveTo);
    ASSERT_FALSE(decision.intent.approached().empty());
    const auto& approach = decision.intent.approached().back();
    EXPECT_NEAR(approach.distance.value(), nx::test::distanceAcross(start, next.start), kDistanceTolerance);
    EXPECT_GT(approach.passShare, 0.0F) << "the way continues over the link";
}

void checkBrakingCrossing(const Platform& platform, nx::Vec3 start, nx::Vec3 goal, int jumps) {
    constexpr int kMostDecisions = 1000;
    const auto mesh = nx::test::playerMesh(platformGeometry(platform), 0);
    nx::navigation::NavigationQuery query{mesh};
    auto corridor = nx::navigation::Corridor::plan(query, {.from = start, .to = goal});
    ASSERT_TRUE(corridor.has_value());
    const auto next = corridor->nextLink(query);
    ASSERT_TRUE(next.has_value());
    ASSERT_GT(nx::test::distanceAcross(start, next->start), brakingRules().arrivalDistance.value());

    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(goal);
    expectApproachToLink(brain.decide(start, query, kRunSpeed), start, *next);
    const Walked walked = walkAbout(platform, brain, query, start, kMostDecisions);
    EXPECT_TRUE(walked.isArrived);
    EXPECT_EQ(walked.jumps, jumps);
}

TEST(AgentBrain, ABrakingBodyAnticipatesAndCrossesAJump) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 40}};
    constexpr nx::Vec3 kStart{.x = 100, .y = 256};
    constexpr nx::Vec3 kGoal{.x = 512, .y = 256, .z = 40};
    checkBrakingCrossing(box, kStart, kGoal, 1);
}

TEST(AgentBrain, ABrakingBodyComesToALinkItJumpsUpWithLittleSpeedAcrossIt) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 40}};
    const auto mesh = nx::test::playerMesh(platformGeometry(box), 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    // Along the box's face, to a top whose link leaves the floor across its way.
    const nx::Vec3 from{.x = 360, .y = 400};
    const nx::Vec3 goal{.x = 420, .y = 150, .z = 40};
    brain.setGoal(goal);
    auto corridor = nx::navigation::Corridor::plan(query, {.from = from, .to = goal});
    ASSERT_TRUE(corridor.has_value());
    const auto next = corridor->nextLink(query);
    ASSERT_TRUE(next.has_value());
    const nx::agents::Decision decision = brain.decide(from, query, kRunSpeed);
    ASSERT_FALSE(decision.intent.approached().empty());
    const nx::Vec3 into = next->start - from;
    const nx::Vec3 beyond = next->end - next->start;
    const float turnCosine =
        ((into.x * beyond.x) + (into.y * beyond.y)) /
        (nx::test::distanceAcross(next->start, from) * nx::test::distanceAcross(next->end, next->start));
    ASSERT_LT(turnCosine, 0.5F) << "a sharp turn";
    EXPECT_NEAR(decision.intent.approached().back().passShare, std::max(turnCosine, 0.0F), 0.01F)
        << "not a corner's cosine of half the turn: in the air it flies on";
}

TEST(AgentBrain, ABrakingBodyAnticipatesAndCrossesADrop) {
    const Platform box{.low = {.x = 384, .y = 128, .z = -16}, .high = {.x = 640, .y = 384, .z = 120}};
    constexpr nx::Vec3 kStart{.x = 512, .y = 256, .z = 120};
    constexpr nx::Vec3 kGoal{.x = 900, .y = 256};
    checkBrakingCrossing(box, kStart, kGoal, 0);
}

/// A floor with a block in its far corner, which the way from the top left to the bottom right turns round.
/// The corner of cornerMesh's block that a way from the floor's top left to its bottom right turns round.
constexpr nx::Vec3 kBlockCorner{.x = 300, .y = 300};

auto cornerMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 1024}, 0);
    nx::test::addBox(geometry, kBlockCorner, {.x = 1024, .y = 1024, .z = 300});
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentBrain, ABodyThatCannotTurnOnTheSpotReachesAGoalAnywhereAboutItWithoutCirclingIt) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    constexpr float kHalfTurn = std::numbers::pi_v<float>;
    constexpr float kGrid = 20.0F;
    // Running across at full speed, a goal about it within 80 units: it turns toward it, stopping and coming
    // back for one behind it at most, never circling it (driven the way it should go, it swung round some by
    // two thirds of a turn and more).
    for (float x = -80.0F; x <= 80.0F; x += kGrid) {
        for (float y = -40.0F; y <= 120.0F; y += kGrid) {
            if (std::hypot(x, y) <= brakingRules().arrivalDistance.value()) {
                continue;
            }
            nx::agents::Brain brain{brakingRules()};
            brain.setGoal(nx::Vec3{.x = x, .y = y});
            const InertialWalk walked = walkInertially(brain, query, {.velocity = {.y = 160}});
            EXPECT_TRUE(walked.isArrived) << x << ' ' << y;
            EXPECT_LT(walked.seconds, 4.0F) << x << ' ' << y;
            EXPECT_LE(walked.turned, kHalfTurn + 0.1F) << x << ' ' << y;
        }
    }
}

TEST(AgentBrain, ABodyThatCannotTurnOnTheSpotSlowsForItsGoalAndArrivesWithoutCirclingIt) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    // Running across at full speed, its goal close by to its side.
    brain.setGoal(nx::Vec3{.x = 120});
    const InertialWalk walked = walkInertially(brain, query, {.velocity = {.y = 160}});
    EXPECT_TRUE(walked.isArrived);
    EXPECT_LT(walked.seconds, 3.0F);
    EXPECT_LT(walked.turned, std::numbers::pi_v<float>) << "it turns toward it, never round it";
}

TEST(AgentBrain, ABodyThatCannotTurnOnTheSpotSlowsForASharpCornerAndTakesItWithoutCirclingIt) {
    auto mesh = cornerMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 900, .y = 150});
    const InertialWalk walked = walkInertially(brain, query, {.feet = {.x = 150, .y = 900}});
    EXPECT_TRUE(walked.isArrived);
    EXPECT_LT(walked.turned, std::numbers::pi_v<float>) << "a right angle, and the swing into it";
}

TEST(AgentBrain, ItsIntentTellsTheCornerItTurnsAtAndTheStopItsWayEndsAtButNotATargetItChases) {
    auto mesh = cornerMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain moving{brakingRules()};
    moving.setGoal(nx::Vec3{.x = 900, .y = 150});
    const nx::agents::Intent intent = moving.decide({.x = 150, .y = 900}, query, kRunSpeed).intent;
    const std::span<const nx::agents::Approach> approached = intent.approached();
    ASSERT_GE(approached.size(), 2U) << "the corners it sees ahead";
    // Its way turns round the block's corner, the turn measured over the way on past each corner of the mesh,
    // so that it is slowed for as a whole. A share of none is the stop at its end.
    auto turning = approached | std::views::transform(&nx::agents::Approach::passShare) |
                   std::views::filter([](float passShare) { return passShare > 0.0F; });
    const float sharpest = std::ranges::min(turning);
    // Down the strip and out along the other, about three quarters of a right angle: slowed for, not stopped.
    EXPECT_GT(sharpest, 0.5F);
    EXPECT_LT(sharpest, 0.9F);
    EXPECT_TRUE(std::ranges::is_sorted(approached, {}, &nx::agents::Approach::distance))
        << "the nearest first";
    // Past the corner, its end among the corners it sees ahead: a stop.
    const nx::agents::Intent past = moving.decide({.x = 450, .y = 150}, query, kRunSpeed).intent;
    ASSERT_FALSE(past.approached().empty());
    EXPECT_EQ(past.approached().back().passShare, 0.0F);
    // Chasing a target it sees, it runs at it: the attack ends the chase, no stop.
    nx::ecs::World world;
    const auto target = world.spawn();
    nx::agents::Brain chasing{brakingRules()};
    chasing.perceive(std::array{nx::agents::Observation{.entity = target, .position = {.x = 150, .y = 600}}},
                     {}, nx::kDefaultTickRate);
    const nx::agents::Decision chase = chasing.decide({.x = 150, .y = 900}, query, kRunSpeed);
    ASSERT_EQ(chase.behavior, nx::agents::Behavior::Chase);
    EXPECT_TRUE(std::ranges::none_of(chase.intent.approached(), [](const nx::agents::Approach& point) {
        return point.passShare == 0.0F;
    }));
    // A brain that does not brake approaches nothing.
    nx::agents::Brain kinematic{rules()};
    kinematic.setGoal(nx::Vec3{.x = 900, .y = 150});
    EXPECT_TRUE(kinematic.decide({.x = 150, .y = 900}, query, kRunSpeed).intent.approached().empty());
}

TEST(AgentBrain, ABodyThatRoundsACornerWideHeadsOnFromItRatherThanBackToIt) {
    auto mesh = cornerMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    const nx::Vec3 goal{.x = 900, .y = 150};
    brain.setGoal(goal);
    std::ignore = brain.decide({.x = 150, .y = 900}, query, kRunSpeed);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    ASSERT_GE(way.size(), 3U) << "round the block's corner";
    const nx::Vec3 corner = way[1];
    // It reaches the corner, then drifts on past it, wide, where its funnel lists the corner still: measured
    // anew, the corner was behind it, and it turned back to it.
    std::ignore = brain.decide(corner + nx::Vec3{.x = -4, .y = 5}, query, kRunSpeed);
    const nx::Vec3 drifted = corner + nx::Vec3{.x = -24, .y = -16};
    const nx::agents::Decision decision = brain.decide(drifted, query, kRunSpeed);
    ASSERT_EQ(decision.behavior, nx::agents::Behavior::MoveTo);
    EXPECT_GT(nx::length(decision.intent.direction), 0.0F);
    const nx::Vec3 toCorner = corner - drifted;
    EXPECT_LT(nx::dot(decision.intent.direction, toCorner / nx::length(toCorner)), 0.7F)
        << "on to the corner after it, not back to it";
}

TEST(AgentBrain, ABodyDrawnBackFromACornerItReachedAsItGotStuckReturnsToIt) {
    auto mesh = cornerMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 900, .y = 150});
    std::ignore = brain.decide({.x = 150, .y = 900}, query, kRunSpeed);
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    // Down the block's side, round its corner, and on: the turn round it is the way's corner nearest it.
    const auto turn =
        std::ranges::min_element(way, {}, [](nx::Vec3 point) { return nx::length(point - kBlockCorner); });
    ASSERT_TRUE(turn != way.begin() && std::ranges::next(turn) != way.end());
    const nx::Vec3 corner = *turn;
    // It reaches that turn, stopping just short of it as its funnel lists it only until then.
    std::ignore = brain.decide(*std::ranges::prev(turn), query, kRunSpeed);
    std::ignore = brain.decide(corner + nx::Vec3{.x = -2, .y = 2}, query, kRunSpeed);
    // It gets stuck there, and is drawn back up the block's side.
    constexpr nx::Vec3 kDrawnBack{.y = 60.0F};
    const nx::Vec3 drawnBack = corner + kDrawnBack;
    brain.noteStuck();
    const nx::Vec3 direction = brain.decide(drawnBack, query, kRunSpeed).intent.direction;
    const nx::Vec3 toCorner = corner - drawnBack;
    EXPECT_GT(nx::dot(direction, toCorner / nx::length(toCorner)), 0.7F) << "back to the corner";
    EXPECT_LT(direction.x, 0.2F) << "along the block's side, not on to the next corner through the block";
}

TEST(AgentBrain, InACrowdABodyWaitsForThoseAheadNearerItsEndAndTheyGoOn) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    constexpr float kRadius = 16.0F;
    const nx::Vec3 end{.x = 400};
    // How a body at `feet` going to `end` steers among `others`, standing still.
    const auto steered = [&](nx::Vec3 feet, std::span<const nx::Vec3> others) {
        nx::agents::Brain brain{brakingRules()};
        brain.setGoal(end);
        nx::agents::Decision decision = brain.decide(feet, query, kRunSpeed);
        std::vector<nx::navigation::SteeringBody> nearby;
        for (const nx::Vec3 other : others) {
            nearby.push_back({.feet = other, .radius = nx::HammerUnits{kRadius}});
        }
        brain.steer(decision.intent, {}, nx::UnitsPerSecond{250}, nearby, query);
        return decision.intent.direction;
    };
    // Three bodies stand ahead of it, touching it, nearer its end: no way round them, it waits.
    const std::array ahead{nx::Vec3{.x = 36}, nx::Vec3{.x = 26, .y = 26}, nx::Vec3{.x = 26, .y = -26}};
    EXPECT_EQ(steered({}, ahead), nx::Vec3{});
    // The one straight ahead of it, the one behind it farther from its end, goes on: none waits for one that
    // waits for it.
    const std::array behind{nx::Vec3{}, nx::Vec3{.x = 26, .y = 26}, nx::Vec3{.x = 26, .y = -26}};
    EXPECT_GT(steered({.x = 36}, behind).x, 0.0F);
}

/// A passage a body wide along the floor's left half that turns up its middle.
auto turningPassageMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 1024}, 0);
    nx::test::addBox(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 480, .z = 300});
    nx::test::addBox(geometry, {.x = 0, .y = 544}, {.x = 576, .y = 1024, .z = 300});
    nx::test::addBox(geometry, {.x = 640, .y = 480}, {.x = 1024, .y = 1024, .z = 300});
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentBrain, TheFirstOfAQueueAtRestWhereItsPassageTurnsGoesOn) {
    // At rest short of where its passage turns, the avoidance weighs the wall its way turns from before it
    // meets it and keeps it where it is: the first of a queue, all of whom then waited, none went on.
    auto mesh = turningPassageMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 608, .y = 900});
    nx::agents::Decision decision = brain.decide({.x = 520, .y = 512}, query, kRunSpeed);
    const std::array behind{
        nx::navigation::SteeringBody{.feet = {.x = 484, .y = 512}, .radius = nx::HammerUnits{16}}};
    brain.steer(decision.intent, {}, nx::UnitsPerSecond{250}, behind, query);
    EXPECT_GT(nx::length(decision.intent.direction), 0.0F);
    EXPECT_GT(decision.intent.direction.x, 0.0F) << "on toward the turn";
}

TEST(AgentBrain, ABodyIsDrivenTheWayItsVelocityMustChangeAndAlongItsWayWhenItHardlyMust) {
    constexpr nx::Vec3 kSteered{.x = 100.0F};
    const std::optional<nx::Vec3> across = nx::agents::driveToward(kSteered, {.x = 100.0F, .y = 100.0F});
    ASSERT_TRUE(across.has_value());
    EXPECT_NEAR(across->x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(across->y, -1.0F, 1.0e-5F) << "against its drift";
    EXPECT_FALSE(nx::agents::driveToward(kSteered, {.x = 95.0F, .y = 5.0F}).has_value())
        << "within a tenth of it";
    EXPECT_FALSE(nx::agents::driveToward(kSteered, {.x = 100.0F, .z = 300.0F}).has_value())
        << "how it falls is not across";
}

TEST(AgentBrain, SteeringNeverGoesFasterThanItCanStillSlowDownForWhatItApproaches) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 20});
    nx::agents::Decision decision = brain.decide({}, query, kRunSpeed);
    ASSERT_EQ(decision.intent.approached().size(), 1U) << "its stop alone";
    brain.steer(decision.intent, {}, nx::UnitsPerSecond{160}, {}, query);
    // Twenty units short of its stop, braking four times its speed a second and below 75 as at 75: 75 and
    // four times the 10.6 units farther than it stops from 75 in, 117.5 units a second, 0.73 of its full.
    constexpr float kMostSpeed = 117.5F;
    ASSERT_TRUE(decision.intent.steered.has_value());
    EXPECT_NEAR(nx::length(*decision.intent.steered), kMostSpeed, 3.0F);
    EXPECT_GT(nx::length(decision.intent.direction), 0.0F) << "it goes on, slower";
    // What a GoldSrc body gains going by what it asks (PM_Accelerate), it asks all it has short of that
    // speed, and only that speed once it goes so fast.
    EXPECT_EQ(decision.intent.speedShare, 1.0F);
    nx::agents::Decision atThatSpeed = brain.decide({}, query, kRunSpeed);
    brain.steer(atThatSpeed.intent, {.x = kMostSpeed + 3.0F}, nx::UnitsPerSecond{160}, {}, query);
    EXPECT_NEAR(atThatSpeed.intent.speedShare, kMostSpeed / 160.0F, 0.02F);
}

TEST(AgentBrain, ABodyAtRestShortOfItsStopWalksThereRatherThanCreep) {
    // 12 units short, just past its arrival distance: braking times the distance alone asked for 48 units a
    // second, slower than its friction lets it start from rest, and it crept there at 2 units a second, as
    // stuck as a stuck watch finds a body; stopping from 85 within it by friction, it walks there.
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 12});
    const InertialWalk walked = walkInertially(brain, query, InertialBody{});
    EXPECT_TRUE(walked.isArrived);
    EXPECT_LT(walked.seconds, 0.5F) << "rather than creep there";
}

TEST(AgentBrain, ItIsAskedForNoWalkSlowerThanItGainsSpeedAtButToStopWhereItsWayEnds) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{brakingRules()};
    brain.setGoal(nx::Vec3{.x = 400});
    constexpr float kFirstWaveSpeed = 100.0F;
    // Twice the walk whose gain its friction takes, 75 times 4 over 5.
    constexpr float kLeastWalk = 120.0F;
    // How fast it is asked to walk at `maxSpeed` as it comes a unit short of a point it passes at `passShare`
    // of it, none a stop: the share alone, and how fast it can still stop there from, 24.5 units a second.
    const auto walkAt = [&](float passShare, float maxSpeed) {
        nx::agents::Decision decision = brain.decide({}, query, kRunSpeed);
        decision.intent.approaches[0] = {.distance = nx::HammerUnits{1.0F}, .passShare = passShare};
        decision.intent.approachCount = 1;
        brain.steer(decision.intent, {.x = maxSpeed}, nx::UnitsPerSecond{maxSpeed}, {}, query);
        return decision.intent.speedShare * maxSpeed;
    };
    EXPECT_NEAR(walkAt(0.1F, kRunSpeed.value()), kLeastWalk, 0.5F)
        << "a sharp turn, as slow as it gains speed at";
    EXPECT_NEAR(walkAt(0.1F, kFirstWaveSpeed), kFirstWaveSpeed, 0.5F) << "its whole speed, when slower";
    EXPECT_NEAR(walkAt(0.6F, kRunSpeed.value()), (0.6F * kRunSpeed.value()) + 24.5F, 0.5F)
        << "faster than it, as braking says";
    EXPECT_NEAR(walkAt(0.0F, kRunSpeed.value()), 24.5F, 0.5F) << "its stop, as slow as it stops there from";
}

TEST(AgentBrain, ItArrivesAcrossOnItsFloorAndNotUnderAGoalAboveIt) {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = 400, .y = 100}, {.x = 700, .y = 400, .z = 60});
    auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain onTheFloor{rules()};
    onTheFloor.setGoal(nx::Vec3{.x = 200, .y = 250});
    EXPECT_EQ(onTheFloor.decide({.x = 197, .y = 250}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle);
    nx::agents::Brain upThere{rules()};
    upThere.setGoal(nx::Vec3{.x = 550, .y = 250, .z = 60});
    EXPECT_NE(upThere.decide({.x = 550, .y = 250}, query, kRunSpeed).behavior, nx::agents::Behavior::Idle)
        << "below it, on the floor, it has not arrived";
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
    const auto decision = brain.decide({}, query, kRunSpeed);
    EXPECT_TRUE(decision.intent.attacks);
    ASSERT_TRUE(decision.intent.lookAt.has_value());
    EXPECT_EQ(*decision.intent.lookAt, crouchedEye);
    EXPECT_EQ(brain.memories().front().observation.position.z, 0);
}
} // namespace
