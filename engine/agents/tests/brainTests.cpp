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
    ASSERT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Investigate);

    std::array present{living};
    std::ranges::sort(present);
    brain.forgetAbsent(present);
    ASSERT_EQ(brain.memories().size(), 1U);
    EXPECT_EQ(brain.memories().front().observation.entity, living);

    brain.forgetAbsent({});
    EXPECT_TRUE(brain.memories().empty());
    EXPECT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Idle) << "nothing left to go to";
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
    const nx::agents::Decision decision = brain.decide({}, query);
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
        std::ignore = brain.decide(feet, query);
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
    ASSERT_EQ(brain.decide({}, query).target, fought);
    // Lost from sight; a step heard since, nearer and fresher, does not draw it off.
    brain.perceive({}, at(0.1F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(stepping, {.y = 200}, Salience::Low, 200)}, at(0.1F),
                nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query).target, fought) << "it keeps after what it fought";
    // A shot fired 250 nearer does.
    brain.perceive({}, at(0.2F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(shooting, {.y = -150}, Salience::High, 150)}, at(0.2F),
                nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query).target, shooting) << "a threat much nearer";
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
    ASSERT_EQ(brain.decide({}, query).target, fought);
    brain.perceive({}, at(0.1F), nx::kDefaultTickRate);
    brain.sense(std::array{estimateOf(shooting, {.y = -250}, Salience::High, 250)}, at(0.1F),
                nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query).target, fought) << "50 nearer, not 100";
    brain.sense(std::array{estimateOf(shooting, {.y = -150}, Salience::High, 150)}, at(0.1F),
                nx::kDefaultTickRate);
    brain.perceive({}, at(1.0F), nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query).target, fought) << "a shot heard long ago is no threat";
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
    EXPECT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Idle) << "seen, not reacted to";
    brain.perceive(seen, at(0.49F), nx::kDefaultTickRate);
    EXPECT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Idle);
    brain.perceive({}, at(0.5F), nx::kDefaultTickRate);
    const auto investigated = brain.decide({}, query);
    EXPECT_EQ(investigated.behavior, nx::agents::Behavior::Investigate) << "recognized out of sight too";
    EXPECT_EQ(investigated.target, target);
    brain.perceive(seen, at(0.6F), nx::kDefaultTickRate);
    const auto attack = brain.decide({}, query);
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

TEST(AgentBrain, FacesAheadAlongItsWayAsItMoves) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.y = 200});
    const nx::agents::Intent intent = brain.decide({}, query).intent;
    EXPECT_EQ(intent.ahead, intent.direction) << "until it steers or wiggles";
    EXPECT_FALSE(intent.jumps);
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
        std::ignore = brain.decide(feet, query);
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
    EXPECT_TRUE(std::ranges::any_of(around, [](nx::Vec3 point) { return point.y > 512; })) << "the far way";
    // Stuck again: it gives its goal up, and pursues nothing for its owner to give it another.
    brain.giveUp(stuckAt({.x = 128, .y = 600}), 1);
    EXPECT_FALSE(brain.goal().has_value());
    EXPECT_EQ(brain.decide(from, query).behavior, nx::agents::Behavior::Idle);
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
    ASSERT_EQ(brain.decide({.x = 512, .y = 128}, query).behavior, nx::agents::Behavior::MoveTo);
    // Spawned again across the hole from where its corridor is, alive all along: its corridor cannot
    // follow it there along the mesh.
    const nx::Vec3 elsewhere{.x = 512, .y = 640};
    const nx::agents::Decision decision = brain.decide(elsewhere, query);
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
    ASSERT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Investigate);
    brain.giveUp(stuckAt({.x = 100}), 1);
    EXPECT_EQ(brain.memories().size(), 1U) << "investigated still, by another way";
    EXPECT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Investigate);
    brain.giveUp(stuckAt({.x = 100}), 1);
    EXPECT_TRUE(brain.memories().empty());
    EXPECT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Idle);
}

TEST(AgentBrain, GivingUpItNeverGivesUpATargetItSeesAndSearchesItsWayAnew) {
    auto mesh = floorMesh();
    nx::navigation::NavigationQuery query{mesh};
    nx::ecs::World world;
    const auto chased = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.perceive(std::array{nx::agents::Observation{chased, {.x = 200}}}, {}, nx::kDefaultTickRate);
    ASSERT_EQ(brain.decide({}, query).behavior, nx::agents::Behavior::Chase);
    for (int stuck = 0; stuck < 3; ++stuck) {
        brain.giveUp(stuckAt({.x = 100}), 1);
        EXPECT_EQ(brain.memories().size(), 1U) << "it still sees it";
    }
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    EXPECT_TRUE(way.empty()) << "its corridor dropped";
    const nx::Vec3 elsewhere{.y = 100};
    const auto chase = brain.decide(elsewhere, query);
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
    ASSERT_EQ(brain.decide(feet, query).behavior, nx::agents::Behavior::MoveTo);
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
    agent.behavior = agent.brain.decide({}, query).behavior;
    ASSERT_TRUE(nx::agents::followsCorridor(agent.behavior));
    EXPECT_FALSE(wayOf(agent).empty());
    EXPECT_EQ(wayOf(agent), wayOf(agent.brain));
    agent.behavior = agent.brain.decide(goal, query).behavior;
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
