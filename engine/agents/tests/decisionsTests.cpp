// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Many brains deciding at once in jobs: the same decisions, each at its request's index, with any number
// of workers as one by one, tick after tick, for fewer brains than a batch and for many batches.

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.jobs;
import nx.navigation;
import nx.agents;
#include "contractRecorder.hpp"
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

/// A floor among boxes, which ways across it go around.
auto boxesMesh() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -768, .y = -768}, {.x = 768, .y = 768}, 0);
    for (const float x : {-384.0F, 0.0F, 384.0F}) {
        nx::test::addBox(geometry, {.x = x - 64, .y = -256, .z = 0}, {.x = x + 64, .y = 256, .z = 128});
    }
    return nx::test::playerMesh(geometry, 0);
}

/// `count` brains, each going to a goal of its own, and where each stands.
struct Agents {
    std::vector<nx::agents::Brain> brains;
    std::vector<nx::Vec3> feet;
};

constexpr float kDecisionStep = 32.0F;
/// How fast the bodies of requestsOf's requests run: they stand, so that Deciders steers none of them and
/// its decisions are those of each brain alone.
constexpr nx::UnitsPerSecond kStanding{};
/// How fast a body runs that crosses links: a player's with a knife or a pistol.
constexpr nx::UnitsPerSecond kRunSpeed{250.0F};

[[nodiscard]] auto agentsOf(std::size_t count, const nx::agents::Rules& settings = rules()) -> Agents {
    Agents agents;
    agents.brains.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto spread = static_cast<float>(index % 24) * 48.0F;
        agents.brains.emplace_back(settings);
        agents.brains.back().setGoal(nx::Vec3{.x = 640.0F, .y = spread - 552.0F});
        agents.feet.push_back({.x = -640.0F, .y = 552.0F - spread});
    }
    return agents;
}

[[nodiscard]] auto requestsOf(Agents& agents) -> std::vector<nx::agents::DecisionRequest> {
    std::vector<nx::agents::DecisionRequest> requests;
    for (std::size_t index = 0; index < agents.brains.size(); ++index) {
        requests.push_back(
            {.brain = &agents.brains[index], .feet = agents.feet[index], .maxSpeed = kStanding});
    }
    return requests;
}

/// The decisions of `count` agents over `ticks` ticks, each walking a little along its intent between
/// them, made by Deciders in jobs of `workers` workers.
[[nodiscard]] auto decisionsInJobs(const nx::navigation::NavigationMesh& mesh, std::size_t count,
                                   std::uint32_t workers, std::size_t ticks,
                                   const nx::agents::Rules& settings = rules())
    -> std::vector<std::vector<nx::agents::Decision>> {
    auto jobs = nx::test::jobSystem(workers);
    nx::agents::Deciders deciders{mesh};
    Agents agents = agentsOf(count, settings);
    std::vector<std::vector<nx::agents::Decision>> made;
    for (std::size_t tick = 0; tick < ticks; ++tick) {
        std::vector<nx::agents::Decision> decisions(count);
        deciders.decide(*jobs, requestsOf(agents), decisions);
        for (std::size_t index = 0; index < count; ++index) {
            agents.feet.at(index) += decisions.at(index).intent.direction * kDecisionStep;
        }
        made.push_back(std::move(decisions));
    }
    return made;
}

/// The same decisions made one by one with one query, each body walking between ticks.
[[nodiscard]] auto decisionsAlone(const nx::navigation::NavigationMesh& mesh, std::size_t count,
                                  std::size_t ticks, const nx::agents::Rules& settings)
    -> std::vector<std::vector<nx::agents::Decision>> {
    Agents alone = agentsOf(count, settings);
    nx::navigation::NavigationQuery query{mesh};
    std::vector<std::vector<nx::agents::Decision>> made;
    for (std::size_t tick = 0; tick < ticks; ++tick) {
        std::vector<nx::agents::Decision> decisions;
        for (std::size_t index = 0; index < count; ++index) {
            nx::Vec3& feet = alone.feet.at(index);
            decisions.push_back(alone.brains.at(index).decide(feet, query, kStanding));
            feet += decisions.back().intent.direction * kDecisionStep;
        }
        made.push_back(std::move(decisions));
    }
    return made;
}

[[nodiscard]] auto sameDecisions(const std::vector<std::vector<nx::agents::Decision>>& first,
                                 const std::vector<std::vector<nx::agents::Decision>>& second) -> bool {
    return std::ranges::equal(first, second, [](const auto& left, const auto& right) {
        return std::ranges::equal(
            left, right, [](const nx::agents::Decision& a, const nx::agents::Decision& b) {
                return a.behavior == b.behavior && a.intent.direction == b.intent.direction &&
                       a.intent.lookAt == b.intent.lookAt && a.intent.attacks == b.intent.attacks &&
                       a.intent.jumps == b.intent.jumps &&
                       std::ranges::equal(a.intent.approached(), b.intent.approached(),
                                          [](const nx::agents::Approach& firstApproach,
                                             const nx::agents::Approach& secondApproach) -> bool {
                                              return firstApproach.distance == secondApproach.distance &&
                                                     firstApproach.passShare == secondApproach.passShare;
                                          }) &&
                       a.target == b.target && a.navigationError == b.navigationError;
            });
    });
}

void expectDeterministicDecisions(const nx::navigation::NavigationMesh& mesh,
                                  const nx::agents::Rules& settings) {
    constexpr std::size_t kAgents = 100;
    constexpr std::size_t kTicks = 8;
    const auto expected = decisionsAlone(mesh, kAgents, kTicks, settings);
    ASSERT_TRUE(std::ranges::any_of(expected.front(), [](const nx::agents::Decision& decision) -> bool {
        return decision.behavior == nx::agents::Behavior::MoveTo;
    }));
    if (settings.braking > 0.0F) {
        ASSERT_TRUE(std::ranges::any_of(expected.front(), [](const nx::agents::Decision& decision) -> bool {
            return !decision.intent.approached().empty();
        }));
    }
    for (const std::uint32_t workers : {0U, 1U, 3U, 15U}) {
        EXPECT_TRUE(sameDecisions(decisionsInJobs(mesh, kAgents, workers, kTicks, settings), expected))
            << workers << " workers";
    }
}

TEST(AgentDeciders, DecideAsEachBrainAloneWouldWithAnyNumberOfWorkers) {
    const auto mesh = boxesMesh();
    constexpr float kGroundBraking = 4.0F;
    constexpr nx::UnitsPerSecond kGroundStopSpeed{75.0F};
    constexpr float kGroundAcceleration = 5.0F;
    nx::agents::Rules braking = rules();
    braking.braking = kGroundBraking;
    braking.stopSpeed = kGroundStopSpeed;
    braking.acceleration = kGroundAcceleration;
    {
        SCOPED_TRACE("without braking");
        expectDeterministicDecisions(mesh, rules());
    }
    {
        SCOPED_TRACE("with braking");
        expectDeterministicDecisions(mesh, braking);
    }
}

TEST(AgentDeciders, TwoAgentsMeetingHeadOnInAPassageSteerPastEachOther) {
    // A passage 160 units wide: room for two bodies of 32 side by side, not for one to walk through the
    // other.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -80}, {.x = 512, .y = 80}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr nx::HammerUnits kRadius{16.0F};
    constexpr float kTick = 0.01F;
    constexpr float kArrived = 24.0F;
    const std::array goals{nx::Vec3{.x = 400}, nx::Vec3{.x = -400}};
    std::array feet{nx::Vec3{.x = -400}, nx::Vec3{.x = 400}};
    std::array<nx::Vec3, 2> velocities{};
    const std::array entities{world.spawn(), world.spawn()};
    std::vector<nx::agents::Brain> brains;
    for (const nx::Vec3 goal : goals) {
        brains.emplace_back(rules()).setGoal(goal);
    }
    float closest = std::numeric_limits<float>::max();
    for (int tick = 0; tick < 600; ++tick) {
        std::vector<nx::agents::DecisionRequest> requests;
        std::vector<nx::agents::Body> bodies;
        for (std::size_t agent = 0; agent < brains.size(); ++agent) {
            requests.push_back({.brain = &brains[agent],
                                .feet = feet[agent],
                                .velocity = velocities[agent],
                                .maxSpeed = kSpeed,
                                .body = entities[agent]});
            bodies.push_back({.entity = entities[agent],
                              .feet = feet[agent],
                              .velocity = velocities[agent],
                              .radius = kRadius});
        }
        std::vector<nx::agents::Decision> decisions(brains.size());
        deciders.decide(*jobs, requests, decisions, bodies);
        for (std::size_t agent = 0; agent < brains.size(); ++agent) {
            const nx::agents::Intent& intent = decisions[agent].intent;
            velocities[agent] = intent.direction * (kSpeed.value() * intent.speedShare);
            feet[agent] += velocities[agent] * kTick;
        }
        closest = std::min(closest, nx::test::distanceAcross(feet[0], feet[1]));
    }
    EXPECT_GE(closest, 2.0F * kRadius.value() - 4.0F) << "never through each other";
    for (std::size_t agent = 0; agent < brains.size(); ++agent) {
        EXPECT_LT(nx::test::distanceAcross(feet[agent], goals[agent]), kArrived)
            << "agent " << agent << " got by";
    }
}

TEST(AgentDeciders, ABodyNearIsSteeredAroundHoweverFarTheOthersAre) {
    // A body in the way, and others as far as could be, which squeeze every body near into one square of
    // those it is looked for in: the same decision as with it alone.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -256}, {.x = 512, .y = 256}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr nx::HammerUnits kRadius{16.0F};
    constexpr float kFar = 1.0e6F;
    const nx::ecs::Entity self = world.spawn();
    const nx::agents::Body inTheWay{
        .entity = world.spawn(), .feet = {.x = 40.0F}, .velocity = {}, .radius = kRadius};
    const auto decisionAmong = [&](std::span<const nx::agents::Body> bodies) {
        nx::agents::Brain brain{rules()};
        brain.setGoal(nx::Vec3{.x = 400});
        const std::array requests{nx::agents::DecisionRequest{
            .brain = &brain, .feet = {}, .velocity = {.x = 100.0F}, .maxSpeed = kSpeed, .body = self}};
        std::array<nx::agents::Decision, 1> decisions{};
        deciders.decide(*jobs, requests, decisions, bodies);
        return decisions.front().intent;
    };
    const nx::agents::Intent alone = decisionAmong(std::array{inTheWay});
    for (const float far : {kFar, -kFar}) {
        const std::array bodies{
            inTheWay,
            nx::agents::Body{.entity = world.spawn(), .feet = {.x = far, .y = far}, .radius = kRadius}};
        const nx::agents::Intent among = decisionAmong(bodies);
        EXPECT_EQ(among.direction, alone.direction);
        EXPECT_EQ(among.speedShare, alone.speedShare);
    }
    const nx::agents::Intent freeWay = decisionAmong({});
    EXPECT_NE(alone.direction, freeWay.direction) << "the body near is steered around";
}

TEST(AgentDeciders, ABodyThatCannotMoveWigglesOutSearchesAnotherWayAndThenGivesUpItsGoal) {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -512}, {.x = 512, .y = 512}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    constexpr nx::Seconds kTick{0.01F};
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    nx::agents::Agent agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()};
    agent.brain.setGoal(nx::Vec3{.x = 400});
    // Its feet stay where they are, as a body pinned on something the mesh does not know of.
    const std::array requests{nx::agents::DecisionRequest{
        .brain = &agent.brain, .feet = {}, .maxSpeed = kSpeed, .stuck = &agent.stuck, .elapsed = kTick}};
    std::array<nx::agents::Decision, 1> decisions{};
    bool wiggled = false;
    bool jumped = false;
    int ticks = 0;
    for (; ticks < 2000 && agent.brain.goal(); ++ticks) {
        deciders.decide(*jobs, requests, decisions);
        const nx::agents::Intent& intent = decisions.front().intent;
        EXPECT_GT(intent.ahead.x, 0.9F) << "it faces its way all along";
        // Back or aside: a way of its wiggle's other than ahead.
        wiggled = wiggled || (agent.stuck.isStuck() && intent.direction.x < 0.5F);
        jumped = jumped || intent.jumps;
    }
    EXPECT_TRUE(wiggled);
    EXPECT_TRUE(jumped);
    EXPECT_FALSE(agent.brain.goal().has_value()) << "given up";
    // Stuck half a second, then 4.5 more: its way searched around where it got stuck, its goal kept; then
    // the same again, and its goal given up.
    EXPECT_NEAR(static_cast<float>(ticks) * kTick.count(), 10.0F, 0.05F);
    deciders.decide(*jobs, requests, decisions);
    EXPECT_EQ(decisions.front().behavior, nx::agents::Behavior::Idle);
    EXPECT_FALSE(agent.stuck.isStuck());
}

TEST(AgentDeciders, ABodyKnockedBackWhereItIsIsNoStuckOneAndKeepsToItsWay) {
    constexpr int kTicksShotAt = 300;
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -512}, {.x = 512, .y = 512}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::agents::Agent agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()};
    agent.brain.setGoal(nx::Vec3{.x = 400});
    // Its feet stay where they are, as each shot takes back what it walks.
    const std::array requests{nx::agents::DecisionRequest{.brain = &agent.brain,
                                                          .feet = {},
                                                          .maxSpeed = nx::UnitsPerSecond{250.0F},
                                                          .stuck = &agent.stuck,
                                                          .elapsed = nx::Seconds{0.01F},
                                                          .isKnockedBack = true}};
    std::array<nx::agents::Decision, 1> decisions{};
    for (int tick = 0; tick < kTicksShotAt; ++tick) {
        deciders.decide(*jobs, requests, decisions);
        EXPECT_FALSE(agent.stuck.isStuck());
        EXPECT_GT(decisions.front().intent.direction.x, 0.9F) << "on along its way";
    }
    EXPECT_TRUE(agent.brain.goal().has_value());
}

TEST(AgentDeciders, TheRequestsOfACallAreOfOneTick) {
    constexpr nx::Seconds kTick{0.01F};
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -512}, {.x = 512, .y = 512}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    std::array agents{
        nx::agents::Agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()},
        nx::agents::Agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()}};
    const auto requestOf = [](nx::agents::Agent& agent, nx::Seconds elapsed) {
        return nx::agents::DecisionRequest{.brain = &agent.brain,
                                           .maxSpeed = nx::UnitsPerSecond{250.0F},
                                           .stuck = &agent.stuck,
                                           .elapsed = elapsed};
    };
    std::array<nx::agents::Decision, 2> decisions{};
    const nx::test::ContractRecorder recorder;
    deciders.decide(*jobs, std::array{requestOf(agents[0], kTick), requestOf(agents[1], kTick)}, decisions);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 0);
    deciders.decide(*jobs, std::array{requestOf(agents[0], kTick), requestOf(agents[1], kTick + kTick)},
                    decisions);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

TEST(AgentDeciders, ABodyInTheAirIsNoStuckOneButOneStuckStaysSoThroughItsJumps) {
    constexpr int kTicksLong = 100;
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -512}, {.x = 512, .y = 512}, 0);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::agents::Agent agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()};
    agent.brain.setGoal(nx::Vec3{.x = 400});
    std::array<nx::agents::Decision, 1> decisions{};
    const auto decideFor = [&](bool isOnGround) {
        const std::array requests{nx::agents::DecisionRequest{.brain = &agent.brain,
                                                              .feet = {},
                                                              .isOnGround = isOnGround,
                                                              .maxSpeed = nx::UnitsPerSecond{250.0F},
                                                              .stuck = &agent.stuck,
                                                              .elapsed = nx::Seconds{0.01F}}};
        for (int tick = 0; tick < kTicksLong; ++tick) {
            deciders.decide(*jobs, requests, decisions);
        }
    };
    decideFor(false);
    EXPECT_FALSE(agent.stuck.isStuck()) << "going straight down, say";
    decideFor(true);
    EXPECT_TRUE(agent.stuck.isStuck());
    decideFor(false);
    EXPECT_TRUE(agent.stuck.isStuck()) << "up on a jump that gets it out";
}

TEST(AgentDeciders, ALinkItsBodyIsKnockedOffIsNoneItFailed) {
    constexpr int kKnockedOff = 5;
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = 384, .y = 128, .z = -16}, {.x = 640, .y = 384, .z = 40});
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::navigation::NavigationQuery query{mesh};
    const nx::Vec3 goal{.x = 512, .y = 360, .z = 40};
    auto corridor = nx::navigation::Corridor::plan(query, {.from = {.x = 100, .y = 256}, .to = goal});
    ASSERT_TRUE(corridor.has_value());
    const auto link = corridor->nextLink(query);
    ASSERT_TRUE(link.has_value());
    nx::agents::Brain brain{rules()};
    brain.setGoal(goal);
    std::array<nx::agents::Decision, 1> decisions{};
    const auto decided = [&](nx::Vec3 feet, bool isOnGround, bool isKnockedBack) {
        const std::array requests{nx::agents::DecisionRequest{.brain = &brain,
                                                              .feet = feet,
                                                              .isOnGround = isOnGround,
                                                              .maxSpeed = kRunSpeed,
                                                              .isKnockedBack = isKnockedBack}};
        deciders.decide(*jobs, requests, decisions);
        return decisions.front();
    };
    const nx::Vec3 atIt = link->start - nx::Vec3{.x = 1};
    const nx::Vec3 halfWayUp = atIt + nx::Vec3{.z = 20};
    for (int time = 0; time < kKnockedOff; ++time) {
        EXPECT_TRUE(decided(atIt, true, false).intent.jumps) << "at it again";
        std::ignore = decided(halfWayUp, false, true);
    }
}

TEST(AgentDeciders, ABodyDrawnBackFromACornerItReachedAsItGotStuckHeadsBackToIt) {
    // A floor with a block in its far corner, which the way from top left to bottom right turns round.
    constexpr nx::Vec3 kBlockCorner{.x = 300, .y = 300};
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 1024}, 0);
    nx::test::addBox(geometry, kBlockCorner, {.x = 1024, .y = 1024, .z = 300});
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::agents::Agent agent{.brain = nx::agents::Brain{rules()}, .stuck = nx::test::stuckWatch()};
    agent.brain.setGoal(nx::Vec3{.x = 900, .y = 150});
    std::array<nx::agents::Decision, 1> decisions{};
    const auto decided = [&](nx::Vec3 feet) {
        const std::array requests{nx::agents::DecisionRequest{.brain = &agent.brain,
                                                              .feet = feet,
                                                              .maxSpeed = nx::UnitsPerSecond{250.0F},
                                                              .stuck = &agent.stuck,
                                                              .elapsed = nx::Seconds{0.01F}}};
        deciders.decide(*jobs, requests, decisions);
        return decisions.front().intent;
    };
    std::ignore = decided({.x = 150, .y = 900});
    std::vector<nx::Vec3> way;
    agent.brain.appendWay(query, way);
    // Down the block's side, round its corner, and on: the turn round it is the way's corner nearest it.
    const auto turn =
        std::ranges::min_element(way, {}, [&](nx::Vec3 point) { return nx::length(point - kBlockCorner); });
    ASSERT_TRUE(turn != way.begin() && std::ranges::next(turn) != way.end());
    const nx::Vec3 corner = *turn;
    // It reaches that turn, stopping just short of it as its funnel lists it only until then, and its feet
    // stay there until its watch finds it stuck.
    std::ignore = decided(*std::ranges::prev(turn));
    constexpr int kMostTicks = 100;
    for (int tick = 0; tick < kMostTicks && !agent.stuck.isStuck(); ++tick) {
        std::ignore = decided(corner + nx::Vec3{.x = -2, .y = 2});
    }
    ASSERT_TRUE(agent.stuck.isStuck());
    // Its wiggle draws it back up the block's side, within its unstuck distance: however it wiggles, what it
    // faces is the way on along its corridor.
    constexpr nx::Vec3 kDrawnBack{.y = 60.0F};
    const nx::Vec3 drawnBack = corner + kDrawnBack;
    const nx::Vec3 ahead = decided(drawnBack).ahead;
    const nx::Vec3 toCorner = corner - drawnBack;
    EXPECT_GT(nx::dot(ahead, toCorner / nx::length(toCorner)), 0.7F) << "back to the corner";
    EXPECT_LT(ahead.x, 0.2F) << "along the block's side, not on to the next corner through the block";
}

TEST(AgentDeciders, ABodyInTheAirHasNotLandedAcrossALink) {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = 0, .y = 0}, {.x = 1024, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = 384, .y = 128, .z = -16}, {.x = 640, .y = 384, .z = 40});
    const auto mesh = nx::test::playerMesh(geometry, 0);
    auto jobs = nx::jobs::JobSystem::create({.workerCount = 0});
    ASSERT_TRUE(jobs.has_value());
    nx::agents::Deciders deciders{mesh};
    nx::navigation::NavigationQuery query{mesh};
    const nx::Vec3 goal{.x = 512, .y = 360, .z = 40};
    auto corridor = nx::navigation::Corridor::plan(query, {.from = {.x = 100, .y = 256}, .to = goal});
    ASSERT_TRUE(corridor.has_value());
    const auto link = corridor->nextLink(query);
    ASSERT_TRUE(link.has_value());
    nx::agents::Brain brain{rules()};
    brain.setGoal(goal);
    std::array<nx::agents::Decision, 1> decisions{};
    const auto decided = [&](nx::Vec3 feet, bool isOnGround) {
        const std::array requests{nx::agents::DecisionRequest{
            .brain = &brain, .feet = feet, .isOnGround = isOnGround, .maxSpeed = kRunSpeed}};
        deciders.decide(**jobs, requests, decisions);
        return decisions.front();
    };
    ASSERT_TRUE(decided(link->start - nx::Vec3{.x = 1}, true).intent.jumps);
    const nx::Vec3 over = link->end + nx::Vec3{.x = 10};
    EXPECT_LT(decided(over, false).intent.direction.x, 0.0F) << "still for the link's end";
    EXPECT_GT(decided(over, true).intent.direction.y, 0.0F) << "landed, on to its goal";
}

/// A door in a wall along x = 0 across two rooms.
constexpr nx::Aabb kDoor{.min = {.x = -16, .y = -32, .z = 0}, .max = {.x = 16, .y = 32, .z = 96}};

/// What a body uses at `point` to open a door: a button there.
[[nodiscard]] auto usedAt(nx::Vec3 point) -> nx::navigation::GateOpener {
    return {.box = {.min = point, .max = point}, .kind = nx::navigation::OpenerKind::Used};
}

/// The mesh of those rooms.
auto roomsWithADoor() -> nx::navigation::NavigationMesh {
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -256}, {.x = 512, .y = 256}, 0);
    nx::test::addBox(geometry, {.x = -16, .y = -288, .z = 0}, {.x = 16, .y = -32, .z = 128});
    nx::test::addBox(geometry, {.x = -16, .y = 32, .z = 0}, {.x = 16, .y = 288, .z = 128});
    nx::test::addBox(geometry, kDoor.min, kDoor.max);
    geometry.doors.push_back(kDoor);
    return nx::test::playerMesh(geometry, 0);
}

TEST(AgentDeciders, WhatOpensADoorFromBehindItIsNoWayToOpenItHoweverNearItIs) {
    // On a body's way through the door, what opens it on the other side is nearest: its way there goes
    // through the door, at which it would wait for itself. It goes to what opens it on its own side.
    const auto mesh = roomsWithADoor();
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr nx::Seconds kTick{0.01F};
    const nx::Vec3 behind{.x = 100, .y = 200};
    const nx::Vec3 onItsSide{.x = -450, .y = 200};
    const std::vector gates{nx::navigation::Gate{.middle = (kDoor.min + kDoor.max) * 0.5F,
                                                 .isOpen = false,
                                                 .openers = {usedAt(behind), usedAt(onItsSide)}}};
    nx::Vec3 feet{.x = -60};
    const auto entity = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 400});
    bool used = false;
    for (int tick = 0; tick < 500 && !used; ++tick) {
        const std::array requests{nx::agents::DecisionRequest{
            .brain = &brain, .feet = feet, .maxSpeed = kSpeed, .body = entity, .elapsed = kTick}};
        std::array<nx::agents::Decision, 1> decisions{};
        deciders.decide(*jobs, requests, decisions, {}, gates);
        const nx::agents::Intent& intent = decisions.front().intent;
        used = intent.uses;
        feet += intent.direction * (kSpeed.value() * intent.speedShare * kTick.count());
    }
    EXPECT_TRUE(used);
    EXPECT_LT(nx::test::distanceAcross(feet, onItsSide), 64.0F);
}

TEST(AgentDeciders, AClosedDoorNothingOpensIsAsAWall) {
    const auto mesh = roomsWithADoor();
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    const std::vector gates{nx::navigation::Gate{.middle = (kDoor.min + kDoor.max) * 0.5F, .isOpen = false}};
    const auto entity = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 400});
    const std::array requests{nx::agents::DecisionRequest{.brain = &brain,
                                                          .feet = {.x = -60},
                                                          .maxSpeed = nx::UnitsPerSecond{250.0F},
                                                          .body = entity,
                                                          .elapsed = nx::Seconds{0.01F}}};
    std::array<nx::agents::Decision, 1> decisions{};
    deciders.decide(*jobs, requests, decisions, {}, gates);
    EXPECT_EQ(decisions.front().behavior, nx::agents::Behavior::Blocked);
    EXPECT_EQ(decisions.front().navigationError, nx::navigation::NavigationError::Unreachable);
    EXPECT_FALSE(decisions.front().closedGate.has_value());
}

TEST(AgentDeciders, AWayKeepsOffAClosedDoorWhileItHasAnother) {
    // The rooms joined round the end of the wall too, a long way: a door that opens or not, it goes round.
    nx::navigation::Geometry geometry;
    nx::test::addFloor(geometry, {.x = -512, .y = -256}, {.x = 512, .y = 512}, 0);
    nx::test::addBox(geometry, {.x = -16, .y = -288, .z = 0}, {.x = 16, .y = -32, .z = 128});
    nx::test::addBox(geometry, {.x = -16, .y = 32, .z = 0}, {.x = 16, .y = 288, .z = 128});
    nx::test::addBox(geometry, kDoor.min, kDoor.max);
    geometry.doors.push_back(kDoor);
    const auto mesh = nx::test::playerMesh(geometry, 0);
    nx::navigation::NavigationQuery query{mesh};
    const std::vector gates{nx::navigation::Gate{.middle = (kDoor.min + kDoor.max) * 0.5F,
                                                 .isOpen = false,
                                                 .openers = {usedAt({.x = -400, .y = 100})}}};
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 200});
    const nx::agents::Decision decision = brain.decide({.x = -60}, query, kRunSpeed, true, {}, gates);
    EXPECT_FALSE(decision.closedGate.has_value());
    std::vector<nx::Vec3> way;
    brain.appendWay(query, way);
    EXPECT_TRUE(std::ranges::any_of(way, [](nx::Vec3 point) { return point.y > 288.0F; }))
        << "round the wall";
}

TEST(AgentDeciders, ADoorUsedOnlyIsUsedByTheBodyThatGetsThere) {
    const auto mesh = roomsWithADoor();
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr nx::Seconds kTick{0.01F};
    const nx::Vec3 middle = (kDoor.min + kDoor.max) * 0.5F;
    const std::vector gates{
        nx::navigation::Gate{.middle = middle, .isOpen = false, .openers = {usedAt(middle)}}};
    nx::Vec3 feet{.x = -200};
    const auto entity = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 400});
    bool used = false;
    for (int tick = 0; tick < 300 && !used; ++tick) {
        const std::array requests{nx::agents::DecisionRequest{
            .brain = &brain, .feet = feet, .maxSpeed = kSpeed, .body = entity, .elapsed = kTick}};
        std::array<nx::agents::Decision, 1> decisions{};
        deciders.decide(*jobs, requests, decisions, {}, gates);
        const nx::agents::Intent& intent = decisions.front().intent;
        used = intent.uses;
        if (used) {
            ASSERT_TRUE(intent.lookAt.has_value());
            EXPECT_EQ(*intent.lookAt, middle);
        }
        feet += intent.direction * (kSpeed.value() * intent.speedShare * kTick.count());
    }
    EXPECT_TRUE(used);
    EXPECT_LT(feet.x, kDoor.min.x);
}

TEST(AgentDeciders, NoBodyGoesToOpenADoorWhenAllThatOpensItIsBehindIt) {
    const auto mesh = roomsWithADoor();
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr nx::Seconds kTick{0.01F};
    const std::vector gates{nx::navigation::Gate{.middle = (kDoor.min + kDoor.max) * 0.5F,
                                                 .isOpen = false,
                                                 .openers = {usedAt({.x = 100, .y = 200})}}};
    const auto entity = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(nx::Vec3{.x = 400});
    const std::array requests{nx::agents::DecisionRequest{
        .brain = &brain, .feet = {.x = -60}, .maxSpeed = kSpeed, .body = entity, .elapsed = kTick}};
    std::array<nx::agents::Decision, 1> decisions{};
    deciders.decide(*jobs, requests, decisions, {}, gates);
    EXPECT_FALSE(brain.gateItOpens().has_value());
    EXPECT_TRUE(decisions.front().closedGate.has_value()) << "it goes on to the door, and waits";
}

TEST(AgentDeciders, TheBodyNearestWhatOpensAClosedDoorOpensItWhileTheOthersWaitAtIt) {
    // What opens it on this side.
    const auto mesh = roomsWithADoor();
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::UnitsPerSecond kSpeed{250.0F};
    constexpr nx::Seconds kTick{0.01F};
    constexpr float kArrived = 24.0F;
    const nx::Vec3 opener{.x = -200, .y = 200};
    const nx::Vec3 goal{.x = 400};
    std::vector gates{nx::navigation::Gate{
        .middle = (kDoor.min + kDoor.max) * 0.5F, .isOpen = false, .openers = {usedAt(opener)}}};
    std::array feet{nx::Vec3{.x = -150, .y = 120}, nx::Vec3{.x = -300, .y = -120}};
    const std::array entities{world.spawn(), world.spawn()};
    std::vector<nx::agents::Brain> brains;
    brains.emplace_back(rules()).setGoal(goal);
    brains.emplace_back(rules()).setGoal(goal);
    std::array<bool, 2> used{};
    float farthestWhileClosed = std::numeric_limits<float>::lowest();
    for (int tick = 0; tick < 1000; ++tick) {
        std::vector<nx::agents::DecisionRequest> requests;
        for (std::size_t agent = 0; agent < brains.size(); ++agent) {
            requests.push_back({.brain = &brains[agent],
                                .feet = feet[agent],
                                .maxSpeed = kSpeed,
                                .body = entities[agent],
                                .elapsed = kTick});
        }
        std::vector<nx::agents::Decision> decisions(brains.size());
        deciders.decide(*jobs, requests, decisions, {}, gates);
        if (tick == 0) {
            EXPECT_TRUE(brains[0].gateItOpens().has_value()) << "the nearer";
            EXPECT_FALSE(brains[1].gateItOpens().has_value());
        }
        for (std::size_t agent = 0; agent < brains.size(); ++agent) {
            const nx::agents::Intent& intent = decisions[agent].intent;
            used[agent] = used[agent] || intent.uses;
            feet[agent] += intent.direction * (kSpeed.value() * intent.speedShare * kTick.count());
            if (!gates.front().isOpen) {
                farthestWhileClosed = std::max(farthestWhileClosed, feet[agent].x);
            }
        }
        // What it uses opens the door, some time on.
        constexpr int kOpensAfter = 300;
        gates.front().isOpen = used[0] && tick >= kOpensAfter;
    }
    EXPECT_TRUE(used[0]);
    EXPECT_FALSE(used[1]) << "one to a door";
    EXPECT_LT(farthestWhileClosed, kDoor.min.x) << "none through it closed";
    for (std::size_t agent = 0; agent < brains.size(); ++agent) {
        EXPECT_LT(nx::test::distanceAcross(feet[agent], goal), kArrived)
            << "agent " << agent << " through it";
    }
}

/// What a body walks into to open a door: a volume of `box` that fires it as the body touches it.
[[nodiscard]] auto enteredIn(nx::Aabb box) -> nx::navigation::GateOpener {
    return {.box = box, .kind = nx::navigation::OpenerKind::Entered};
}

/// How a body walking from `from` to `goal` through the door of roomsWithADoor, which `opener` opens as a
/// body's box meets it, fares: whether it ever used anything, whether its box met the opener, how far it went
/// while the door stood closed, and where it ends.
struct DoorWalk {
    bool used = false;
    bool met = false;
    float farthestWhileClosed = std::numeric_limits<float>::lowest();
    nx::Vec3 feet{};
};

[[nodiscard]] auto walkThroughDoor(nx::Vec3 from, nx::Vec3 goal, nx::navigation::GateOpener opener)
    -> DoorWalk {
    const auto mesh = roomsWithADoor();
    auto jobs = nx::test::jobSystem(0);
    nx::agents::Deciders deciders{mesh};
    nx::ecs::World world;
    constexpr nx::Seconds kTick{0.01F};
    const float radius = mesh.profile().radius.value();
    std::vector gates{
        nx::navigation::Gate{.middle = (kDoor.min + kDoor.max) * 0.5F, .isOpen = false, .openers = {opener}}};
    const auto entity = world.spawn();
    nx::agents::Brain brain{rules()};
    brain.setGoal(goal);
    DoorWalk walk{.feet = from};
    for (int tick = 0; tick < 1000; ++tick) {
        const std::array requests{nx::agents::DecisionRequest{
            .brain = &brain, .feet = walk.feet, .maxSpeed = kRunSpeed, .body = entity, .elapsed = kTick}};
        std::array<nx::agents::Decision, 1> decisions{};
        deciders.decide(*jobs, requests, decisions, {}, gates);
        const nx::agents::Intent& intent = decisions.front().intent;
        walk.used = walk.used || intent.uses;
        walk.feet += intent.direction * (kRunSpeed.value() * intent.speedShare * kTick.count());
        // As the game fires a volume a body's box meets.
        const nx::Aabb body{.min = walk.feet - nx::Vec3{.x = radius, .y = radius},
                            .max = walk.feet + nx::Vec3{.x = radius, .y = radius, .z = 72.0F}};
        walk.met = walk.met || body.intersects(opener.box);
        if (!gates.front().isOpen) {
            walk.farthestWhileClosed = std::max(walk.farthestWhileClosed, walk.feet.x);
        }
        gates.front().isOpen = walk.met;
    }
    return walk;
}

/// The part of a volume about the door 14 units deep past its side short of x = 0, as de_nuke's glass doors
/// have them.
constexpr nx::Aabb kAtTheDoor{.min = {.x = kDoor.min.x - 14.0F, .y = kDoor.min.y, .z = 0.0F},
                              .max = {.x = kDoor.min.x, .y = kDoor.max.y, .z = 64.0F}};

TEST(AgentDeciders, ABodyWalksIntoWhatOpensADoorAsItIsEnteredOnItsSideAndUsesNothing) {
    constexpr nx::Vec3 kGoal{.x = 400};
    const DoorWalk walk = walkThroughDoor({.x = -200}, kGoal, enteredIn(kAtTheDoor));
    EXPECT_TRUE(walk.met) << "on to the door, not waiting short of it";
    EXPECT_FALSE(walk.used);
    EXPECT_LT(walk.farthestWhileClosed, kDoor.min.x) << "none through it closed";
    EXPECT_LT(nx::test::distanceAcross(walk.feet, kGoal), 24.0F) << "through it once open";
}

TEST(AgentDeciders, ABodyGoesToWhatOpensADoorAsItIsEnteredAwayFromItAndStandsInIt) {
    // A plate in the floor of the room, far from the door: the body waiting at the door goes there.
    constexpr nx::Aabb kPlate{.min = {.x = -320, .y = 150, .z = 0}, .max = {.x = -280, .y = 190, .z = 8}};
    constexpr nx::Vec3 kGoal{.x = 400};
    const DoorWalk walk = walkThroughDoor({.x = -100}, kGoal, enteredIn(kPlate));
    EXPECT_TRUE(walk.met);
    EXPECT_FALSE(walk.used);
    EXPECT_LT(walk.farthestWhileClosed, kDoor.min.x);
    EXPECT_LT(nx::test::distanceAcross(walk.feet, kGoal), 24.0F);
}

TEST(AgentDeciders, WhatOpensADoorAsItIsEnteredOnlyBeyondItIsNoWayToOpenItFromThisSide) {
    constexpr nx::Aabb kBeyond{.min = {.x = kDoor.max.x, .y = kDoor.min.y, .z = 0.0F},
                               .max = {.x = kDoor.max.x + 14.0F, .y = kDoor.max.y, .z = 64.0F}};
    const DoorWalk walk = walkThroughDoor({.x = -200}, {.x = 400}, enteredIn(kBeyond));
    EXPECT_FALSE(walk.met);
    EXPECT_LT(walk.farthestWhileClosed, kDoor.min.x);
    EXPECT_LT(walk.feet.x, kDoor.min.x) << "it waits at the door";
}

TEST(AgentDeciders, FewerBrainsThanABatchAndNoneDecideToo) {
    const auto mesh = boxesMesh();
    const std::size_t few = nx::agents::kSmallestBatch - 1;
    EXPECT_TRUE(sameDecisions(decisionsInJobs(mesh, few, 3, 2), decisionsInJobs(mesh, few, 0, 2)));
    auto jobs = nx::test::jobSystem(3);
    nx::agents::Deciders deciders{mesh};
    deciders.decide(*jobs, {}, {});
}

} // namespace
