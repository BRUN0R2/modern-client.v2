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

void Deciders::decide(jobs::JobSystem& jobs, std::span<const DecisionRequest> requests,
                      std::span<Decision> decisions) {
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
    // Batch `batch` decides with a query of its own, so that batches never share one.
    const auto decideBatch = [requests, decisions, batchSize, count,
                              queryOf = std::span{queries}](std::size_t batch) {
        navigation::NavigationQuery& query = queryOf[batch];
        for (std::size_t index = batch * batchSize; index < std::min(count, (batch + 1) * batchSize);
             ++index) {
            decisions[index] = requests[index].brain->decide(requests[index].feet, query);
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
