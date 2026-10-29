// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.agents;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.navigation;

namespace nx::agents {
namespace {
constexpr std::size_t kSteeringCorners = 4;
[[nodiscard]] auto groundDistance(Vec3 first, Vec3 second) noexcept -> float {
    return std::hypot(first.x - second.x, first.y - second.y);
}
} // namespace

Brain::Brain(Rules settings) : rules{settings} {
    expects(rules.memoryLimit > 0 && rules.memoryLifetime > Seconds{} &&
                rules.attackDistance > HammerUnits{} && rules.arrivalDistance > HammerUnits{} &&
                rules.replanDistance > HammerUnits{} && rules.reactionTime >= Seconds{},
            "Brain: positive rules, a memory budget and a reaction time of none or more");
}

void Brain::perceive(std::span<const Observation> seen, GameTime now, TickRate rate) {
    std::erase_if(memory, [this, now, rate](const Memory& record) {
        return now.since(record.seenAt, rate) >= rules.memoryLifetime;
    });
    for (Memory& record : memory) {
        record.isVisible = false;
    }
    for (const Observation& observation : seen) {
        auto found = std::ranges::find(memory, observation.entity,
                                       [](const Memory& record) { return record.observation.entity; });
        if (found != memory.end()) {
            found->observation = observation;
            found->seenAt = now;
            found->isVisible = true;
        } else {
            if (memory.size() == rules.memoryLimit) {
                // The oldest unobserved fact has the weakest confidence.
                const auto weakest = std::ranges::min_element(memory, {}, &Memory::seenAt);
                memory.erase(weakest);
            }
            memory.push_back(
                {.observation = observation, .seenAt = now, .noticedAt = now, .isVisible = true});
        }
    }
    for (Memory& record : memory) {
        record.isRecognized = now.since(record.noticedAt, rate) >= rules.reactionTime;
    }
}

void Brain::forgetAbsent(std::span<const ecs::Entity> present) {
    expects(std::ranges::is_sorted(present), "Brain::forgetAbsent: the targets present, sorted");
    std::erase_if(memory, [present](const Memory& record) {
        return !std::ranges::binary_search(present, record.observation.entity);
    });
}

auto Brain::decide(Vec3 feet, navigation::NavigationQuery& query) -> Decision {
    const Memory* selected = nullptr;
    for (const Memory& candidate : memory | std::views::filter(&Memory::isRecognized)) {
        if (selected == nullptr || (candidate.isVisible && !selected->isVisible) ||
            (candidate.isVisible && selected->isVisible &&
             groundDistance(feet, candidate.observation.position) <
                 groundDistance(feet, selected->observation.position)) ||
            (!candidate.isVisible && !selected->isVisible && candidate.seenAt > selected->seenAt)) {
            selected = &candidate;
        }
    }
    if (selected == nullptr && !givenGoal) {
        target.reset();
        plannedGoal.reset();
        plannedFrom.reset();
        corridor.reset();
        pathError.reset();
        return {};
    }
    // A target remembered first, else the goal given, which no entity is pursued for.
    const Vec3 goal = selected != nullptr ? selected->observation.position : *givenGoal;
    const std::optional<ecs::Entity> pursued =
        selected != nullptr ? std::optional{selected->observation.entity} : std::nullopt;
    Decision decision{.behavior = Behavior::MoveTo};
    if (selected != nullptr) {
        decision = {.intent = {.lookAt = selected->observation.aimAt.value_or(goal)},
                    .behavior = selected->isVisible ? Behavior::Chase : Behavior::Investigate,
                    .target = pursued};
        if (selected->isVisible && length(goal - feet) <= rules.attackDistance.value()) {
            decision.behavior = Behavior::Attack;
            decision.intent.attacks = true;
            return decision;
        }
    }
    if (target != pursued || !plannedGoal ||
        groundDistance(*plannedGoal, goal) >= rules.replanDistance.value() ||
        (pathError && plannedFrom && groundDistance(*plannedFrom, feet) >= rules.replanDistance.value())) {
        target = pursued;
        plannedGoal = goal;
        plannedFrom = feet;
        corridor.reset();
        pathError.reset();
        auto planned = navigation::Corridor::plan(query, {.from = feet, .to = goal});
        if (planned) {
            corridor.emplace(std::move(*planned));
        } else {
            pathError = planned.error().code;
        }
    }
    if (pathError) {
        decision.behavior = Behavior::Blocked;
        decision.navigationError = pathError;
        return decision;
    }
    invariant(corridor.has_value(), "Brain: a planned corridor or a reported path error");
    std::ignore = corridor->moveTarget(query, goal);
    const Vec3 walkablePosition = corridor->moveAgent(query, feet);
    if (length(walkablePosition - corridor->target()) <= rules.arrivalDistance.value()) {
        if (selected == nullptr) {
            givenGoal.reset();
        } else if (!selected->isVisible) {
            const ecs::Entity investigated = selected->observation.entity;
            std::erase_if(memory, [investigated](const Memory& record) {
                return record.observation.entity == investigated;
            });
        }
        decision.behavior = Behavior::Idle;
        return decision;
    }
    const std::vector<Vec3> corners = corridor->corners(query, kSteeringCorners);
    for (const Vec3 corner : corners) {
        if (groundDistance(feet, corner) > rules.arrivalDistance.value()) {
            const Vec3 direction{.x = corner.x - feet.x, .y = corner.y - feet.y};
            decision.intent.direction = direction / length(direction);
            break;
        }
    }
    return decision;
}
} // namespace nx::agents
