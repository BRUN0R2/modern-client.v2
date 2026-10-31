// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.agents;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.navigation;

namespace nx::agents {

Deciders::Deciders(const navigation::NavigationMesh& mesh) noexcept : decidedOn{&mesh} {}

namespace {

/// Writes to `nearby` the bodies of `bodies` within `range` across and `height` up or down of `request`'s
/// feet, but its own, the nearest first, ties by entity, at most kMostSteeringBodies.
void bodiesNear(const DecisionRequest& request, std::span<const Body> bodies, float range, float height,
                std::vector<std::pair<float, const Body*>>& sorted,
                std::vector<navigation::SteeringBody>& nearby) {
    sorted.clear();
    for (const Body& body : bodies) {
        const float across = std::hypot(body.feet.x - request.feet.x, body.feet.y - request.feet.y);
        if (request.body != body.entity && across <= range &&
            std::abs(body.feet.z - request.feet.z) <= height) {
            sorted.emplace_back(across, &body);
        }
    }
    std::ranges::sort(sorted, [](const auto& first, const auto& second) {
        return first.first != second.first ? first.first < second.first
                                           : first.second->entity < second.second->entity;
    });
    nearby.clear();
    for (const auto& [across, body] : sorted | std::views::take(navigation::kMostSteeringBodies)) {
        nearby.push_back({.feet = body->feet, .velocity = body->velocity, .radius = body->radius});
    }
}

} // namespace

void Deciders::decide(jobs::JobSystem& jobs, std::span<const DecisionRequest> requests,
                      std::span<Decision> decisions, std::span<const Body> bodies) {
    expects(decisions.size() == requests.size(), "Deciders::decide: a decision for each request");
    const std::size_t count = std::min(requests.size(), decisions.size());
    if (count == 0) {
        return;
    }
    const std::size_t threads = std::size_t{jobs.workerCount()} + 1;
    const std::size_t batchCount = std::clamp(count / kSmallestBatch, std::size_t{1}, threads);
    const std::size_t batchSize = divideRoundingUp(count, batchCount);
    while (queries.size() < batchCount) {
        queries.emplace_back(*decidedOn);
    }
    const navigation::AgentProfile& profile = decidedOn->profile();
    // What every batch reads, which outlives them: decide waits for them all.
    const struct {
        std::span<const DecisionRequest> requests;
        std::span<Decision> decisions;
        std::span<const Body> bodies;
        std::span<navigation::NavigationQuery> queries;
        std::size_t batchSize;
        std::size_t count;
        float range;
        float height;
    } work{.requests = requests,
           .decisions = decisions,
           .bodies = bodies,
           .queries = queries,
           .batchSize = batchSize,
           .count = count,
           .range = profile.radius.value() * navigation::kSteeringRangeRadii,
           .height = profile.standingHeight.value()};
    // Batch `batch` decides with a query of its own, so that batches never share one.
    const auto decideBatch = [&work](std::size_t batch) {
        navigation::NavigationQuery& query = work.queries[batch];
        std::vector<std::pair<float, const Body*>> sorted;
        std::vector<navigation::SteeringBody> nearby;
        for (std::size_t index = batch * work.batchSize;
             index < std::min(work.count, (batch + 1) * work.batchSize); ++index) {
            const DecisionRequest& request = work.requests[index];
            Decision decision = request.brain->decide(request.feet, query);
            if (length(decision.intent.direction) > 0.0F && request.maxSpeed > UnitsPerSecond{}) {
                bodiesNear(request, work.bodies, work.range, work.height, sorted, nearby);
                request.brain->steer(decision.intent, request.velocity, request.maxSpeed, nearby, query);
            }
            // Last, so that what gets a stuck body out overrides how it steers (UpdatePathMovement). A body
            // told to wait, its way taken by other bodies, is not stuck: it moves on as they make way.
            const bool moves = followsCorridor(decision.behavior) && length(decision.intent.direction) > 0.0F;
            if (request.stuck != nullptr &&
                request.stuck->watch(decision.intent, request.feet, request.maxSpeed, moves,
                                     request.elapsed) == Progress::GivesUp) {
                request.brain->giveUp(request.stuck->stuckPlace(), request.stuck->reroutes());
            }
            work.decisions[index] = decision;
        }
    };
    if (batchCount == 1) {
        decideBatch(0);
        return;
    }
    jobs.wait(jobs.parallelFor({.label = "agents.decide", .priority = jobs::Priority::Critical},
                               std::views::iota(std::size_t{0}, batchCount), jobs::Grain::fixed(1),
                               decideBatch));
}

} // namespace nx::agents
