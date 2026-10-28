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

[[nodiscard]] auto agentsOf(std::size_t count) -> Agents {
    Agents agents;
    agents.brains.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto spread = static_cast<float>(index % 24) * 48.0F;
        agents.brains.emplace_back(rules());
        agents.brains.back().setGoal(nx::Vec3{.x = 640.0F, .y = spread - 552.0F});
        agents.feet.push_back({.x = -640.0F, .y = 552.0F - spread});
    }
    return agents;
}

[[nodiscard]] auto requestsOf(Agents& agents) -> std::vector<nx::agents::DecisionRequest> {
    std::vector<nx::agents::DecisionRequest> requests;
    for (std::size_t index = 0; index < agents.brains.size(); ++index) {
        requests.push_back({.brain = &agents.brains[index], .feet = agents.feet[index]});
    }
    return requests;
}

/// The decisions of `count` agents over `ticks` ticks, each walking a little along its intent between
/// them, made by Deciders in jobs of `workers` workers.
[[nodiscard]] auto decisionsInJobs(const nx::navigation::NavigationMesh& mesh, std::size_t count,
                                   std::uint32_t workers, std::size_t ticks)
    -> std::vector<std::vector<nx::agents::Decision>> {
    auto jobs = nx::test::jobSystem(workers);
    nx::agents::Deciders deciders{mesh};
    Agents agents = agentsOf(count);
    std::vector<std::vector<nx::agents::Decision>> made;
    for (std::size_t tick = 0; tick < ticks; ++tick) {
        std::vector<nx::agents::Decision> decisions(count);
        deciders.decide(*jobs, requestsOf(agents), decisions);
        for (std::size_t index = 0; index < count; ++index) {
            agents.feet[index] += decisions[index].intent.direction * 32.0F;
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
                       a.target == b.target && a.navigationError == b.navigationError;
            });
    });
}

TEST(AgentDeciders, DecideAsEachBrainAloneWouldWithAnyNumberOfWorkers) {
    const auto mesh = boxesMesh();
    constexpr std::size_t kAgents = 100;
    constexpr std::size_t kTicks = 8;
    // One by one, with a query of their own: what each brain decides alone.
    Agents alone = agentsOf(kAgents);
    nx::navigation::NavigationQuery query{mesh};
    std::vector<std::vector<nx::agents::Decision>> expected;
    for (std::size_t tick = 0; tick < kTicks; ++tick) {
        std::vector<nx::agents::Decision> decisions;
        for (std::size_t index = 0; index < kAgents; ++index) {
            decisions.push_back(alone.brains[index].decide(alone.feet[index], query));
            alone.feet[index] += decisions.back().intent.direction * 32.0F;
        }
        expected.push_back(std::move(decisions));
    }
    ASSERT_TRUE(std::ranges::any_of(expected.front(), [](const nx::agents::Decision& decision) {
        return decision.behavior == nx::agents::Behavior::MoveTo;
    }));
    for (const std::uint32_t workers : {0U, 1U, 3U, 15U}) {
        EXPECT_TRUE(sameDecisions(decisionsInJobs(mesh, kAgents, workers, kTicks), expected))
            << workers << " workers";
    }
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
