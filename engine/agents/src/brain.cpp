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
/// The least share of its full speed a steered way is worth going: below it, the agent waits.
constexpr float kLeastSteeredSpeedShare = 0.1F;
/// How far back from the corner after the next an agent aims, in shares of how far the next is: as
/// DetourCrowd anticipates a turn (calcSmoothSteerDirection), so that it takes a corner wide.
constexpr float kTurnAnticipation = 0.5F;
/// `vector` across the ground, its height left out.
[[nodiscard]] auto acrossOf(Vec3 vector) noexcept -> Vec3 {
    return Vec3{.x = vector.x, .y = vector.y};
}
[[nodiscard]] auto groundDistance(Vec3 first, Vec3 second) noexcept -> float {
    return std::hypot(first.x - second.x, first.y - second.y);
}
/// Heads `intent` for `point` from `feet`, across the ground: its way and where it faces; as it is when
/// its feet are right below or above it.
void headFor(Intent& intent, Vec3 feet, Vec3 point) noexcept {
    const Vec3 way = acrossOf(point - feet);
    const float distance = length(way);
    if (distance > 0.0F) {
        intent.direction = way / distance;
        intent.ahead = intent.direction;
    }
}
/// Whether a body at `feet` crossed `link`: it stands within `stepHeight` of the height of the link's end,
/// as far from its start across as the end is, but `slack`, the way it drops or jumps over.
[[nodiscard]] auto hasCrossed(const navigation::MeshLink& link, Vec3 feet, float stepHeight,
                              float slack) noexcept -> bool {
    return std::abs(feet.z - link.end.z) <= stepHeight &&
           groundDistance(feet, link.start) + slack >= groundDistance(link.end, link.start);
}
} // namespace

Brain::Brain(Rules settings) : rules{settings} {
    expects(rules.memoryLimit > 0 && rules.memoryLifetime > Seconds{} &&
                rules.attackDistance > HammerUnits{} && rules.arrivalDistance > HammerUnits{} &&
                rules.replanDistance > HammerUnits{} && rules.reactionTime >= Seconds{} &&
                std::isfinite(rules.replanShare) && rules.replanShare >= 0.0F &&
                rules.shortTermMemory >= Seconds{} && rules.threatRecency >= Seconds{} &&
                rules.threatMargin >= HammerUnits{},
            "Brain: positive rules, a memory budget, and a reaction time, a replan share, a short-term "
            "memory and a threat's recency and margin of none or more");
}

void Brain::perceive(std::span<const Observation> seen, GameTime now, TickRate rate) {
    std::erase_if(memory, [this, now, rate](const Memory& record) {
        const GameTime since = foughtAt ? std::max(record.perceivedAt, *foughtAt) : record.perceivedAt;
        return now.since(since, rate) >= rules.memoryLifetime;
    });
    for (Memory& record : memory) {
        record.isVisible = false;
    }
    for (const Observation& observation : seen) {
        auto found = std::ranges::find(memory, observation.entity,
                                       [](const Memory& record) { return record.observation.entity; });
        if (found != memory.end()) {
            found->observation = observation;
            found->perceivedAt = now;
            found->isVisible = true;
            found->salience = Salience::Certain;
            found->distance = {};
            found->placedAt = now;
        } else {
            remember({.observation = observation, .salience = Salience::Certain}, now, true);
        }
    }
    for (Memory& record : memory) {
        record.isRecognized = now.since(record.noticedAt, rate) >= rules.reactionTime;
    }
    if (std::ranges::any_of(memory,
                            [](const Memory& record) { return record.isVisible && record.isRecognized; })) {
        foughtAt = now;
    }
    markThreats(now, rate);
}

void Brain::markThreats(GameTime now, TickRate rate) {
    for (Memory& record : memory) {
        record.isThreat = !record.isVisible && record.salience == Salience::High &&
                          now.since(record.placedAt, rate) <= rules.threatRecency;
    }
}

void Brain::sense(std::span<const Sensed> sensed, GameTime now, TickRate rate) {
    for (const Sensed& estimate : sensed) {
        auto found = std::ranges::find(memory, estimate.observation.entity,
                                       [](const Memory& record) { return record.observation.entity; });
        if (found == memory.end()) {
            remember(estimate, now, false);
            memory.back().isRecognized = rules.reactionTime <= Seconds{};
            continue;
        }
        if (found->isVisible) {
            continue; // its sight the better
        }
        // OnEvent's short-term memory: a vaguer or no nearer estimate leaves a recent one where it is.
        const bool holds = estimate.salience != Salience::Certain &&
                           now.since(found->placedAt, rate) < rules.shortTermMemory &&
                           (estimate.salience < found->salience ||
                            (estimate.salience == found->salience && estimate.distance >= found->distance));
        if (!holds) {
            found->observation = estimate.observation;
            found->salience = estimate.salience;
            found->distance = estimate.distance;
            found->placedAt = now;
        }
        found->perceivedAt = now;
        found->isRecognized = now.since(found->noticedAt, rate) >= rules.reactionTime;
    }
    markThreats(now, rate);
}

void Brain::remember(const Sensed& sensed, GameTime now, bool isVisible) {
    if (memory.size() == rules.memoryLimit) {
        // The oldest unobserved fact has the weakest confidence.
        const auto weakest = std::ranges::min_element(memory, {}, &Memory::perceivedAt);
        memory.erase(weakest);
    }
    memory.push_back({.observation = sensed.observation,
                      .perceivedAt = now,
                      .noticedAt = now,
                      .isVisible = isVisible,
                      .salience = sensed.salience,
                      .distance = sensed.distance,
                      .placedAt = now});
}

auto Brain::movedFarEnough(Vec3 feet, Vec3 goal) const noexcept -> bool {
    if (!plannedGoal) {
        return true;
    }
    const float threshold =
        std::max(rules.replanDistance.value(), rules.replanShare * groundDistance(feet, goal));
    return groundDistance(*plannedGoal, goal) >= threshold;
}

void Brain::forgetAbsent(std::span<const ecs::Entity> present) {
    expects(std::ranges::is_sorted(present), "Brain::forgetAbsent: the targets present, sorted");
    std::erase_if(memory, [present](const Memory& record) {
        return !std::ranges::binary_search(present, record.observation.entity);
    });
}

auto Brain::choose(Vec3 feet) const -> const Memory* {
    const auto distanceTo = [feet](const Memory* record) {
        return groundDistance(feet, record->observation.position);
    };
    const Memory* seen = nullptr;
    const Memory* focused = nullptr;
    const Memory* threat = nullptr;
    const Memory* freshest = nullptr;
    for (const Memory& candidate : memory | std::views::filter(&Memory::isRecognized)) {
        if (candidate.isVisible) {
            if (seen == nullptr || distanceTo(&candidate) < distanceTo(seen)) {
                seen = &candidate;
            }
            continue;
        }
        if (focus == candidate.observation.entity) {
            focused = &candidate;
        }
        if (candidate.isThreat && (threat == nullptr || distanceTo(&candidate) < distanceTo(threat))) {
            threat = &candidate;
        }
        if (freshest == nullptr || candidate.perceivedAt > freshest->perceivedAt) {
            freshest = &candidate;
        }
    }
    if (seen != nullptr) {
        return seen;
    }
    if (focused != nullptr) {
        // Lost from sight, it keeps after it, unless a threat is much nearer (CanHearNearbyEnemyGunfire).
        const bool isThreatNearer = threat != nullptr && threat != focused &&
                                    distanceTo(threat) + rules.threatMargin.value() <= distanceTo(focused);
        return isThreatNearer ? threat : focused;
    }
    return freshest;
}

auto Brain::decide(Vec3 feet, navigation::NavigationQuery& query) -> Decision {
    const Memory* const selected = choose(feet);
    focus = selected != nullptr ? std::optional{selected->observation.entity} : std::nullopt;
    if (selected == nullptr && !givenGoal) {
        target.reset();
        plannedGoal.reset();
        plannedFrom.reset();
        forgetCorridor();
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
    const navigation::AgentProfile& profile = query.mesh().profile();
    if (crossing && hasCrossed(*crossing, feet, profile.stepHeight.value(), rules.arrivalDistance.value())) {
        crossing.reset(); // it walks along its corridor again, from the link's end
    }
    // A body away from its corridor (spawned again, teleported, pushed off the mesh) searches its way anew
    // from where it is, rather than walking straight at a corridor left behind: away from where it stands
    // on the mesh, so that a body standing past the end of the mesh's floor, or on something it drops from,
    // keeps the way it walks back on; one nowhere near the mesh searches, and is blocked. One across a
    // link searches none until it lands, as DetourCrowd plans none for an agent over a link.
    const bool walksAlong = corridor && !crossing;
    const auto standsAt = walksAlong ? query.nearestPoint(feet) : Result<Vec3, navigation::NavigationError>{};
    const bool isAwayFromCorridor =
        walksAlong && (!standsAt || groundDistance(corridor->moveAgent(query, feet), *standsAt) >
                                        rules.replanDistance.value());
    if (!crossing &&
        (target != pursued || !plannedGoal || isAwayFromCorridor || movedFarEnough(feet, goal) ||
         (pathError && plannedFrom && groundDistance(*plannedFrom, feet) >= rules.replanDistance.value()))) {
        if (target != pursued) {
            avoided.clear(); // another pursuit, which got stuck nowhere yet
        }
        target = pursued;
        plannedGoal = goal;
        plannedFrom = feet;
        forgetCorridor();
        pathError.reset();
        auto planned = navigation::Corridor::plan(query, {.from = feet, .to = goal, .avoided = avoided});
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
    if (crossing) {
        headFor(decision.intent, feet, crossing->end);
        return decision;
    }
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
    if (beginCrossing(decision, feet, query)) {
        return decision;
    }
    // Toward the next corner farther than arrival, the turn after it anticipated (calcSmoothSteerDirection).
    const std::vector<Vec3> corners = corridor->corners(query, kSteeringCorners);
    const auto next = std::ranges::find_if(corners, [this, feet](Vec3 corner) {
        return groundDistance(feet, corner) > rules.arrivalDistance.value();
    });
    if (next != corners.end()) {
        const Vec3 toNext = acrossOf(*next - feet);
        const Vec3 toAfter = acrossOf(*std::ranges::next(next, 1, corners.end() - 1) - feet);
        const float afterLength = length(toAfter);
        const Vec3 anticipated = afterLength > 0.0F
                                     ? toNext - (toAfter * (length(toNext) * kTurnAnticipation / afterLength))
                                     : toNext;
        const Vec3 direction = length(anticipated) > 0.0F ? anticipated : toNext;
        decision.intent.direction = direction / length(direction);
        decision.intent.ahead = decision.intent.direction;
    }
    return decision;
}

auto Brain::beginCrossing(Decision& decision, Vec3 feet, navigation::NavigationQuery& query) -> bool {
    const std::optional<navigation::MeshLink> next = corridor->nextLink(query);
    if (!next || groundDistance(feet, next->start) > rules.arrivalDistance.value()) {
        return false;
    }
    crossing = corridor->crossLink(query);
    if (!crossing) {
        return false;
    }
    // A link up it jumps once, from its start; one down it walks off.
    decision.intent.jumps = crossing->end.z > crossing->start.z;
    headFor(decision.intent, feet, crossing->end);
    return true;
}

void Brain::forgetCorridor() noexcept {
    corridor.reset();
    crossing.reset();
}

void Brain::giveUp(const navigation::AvoidedPlace& stuckAt, std::size_t reroutes) {
    const auto pursued =
        target ? std::ranges::find(memory, *target,
                                   [](const Memory& record) { return record.observation.entity; })
               : memory.end();
    const bool chases = pursued != memory.end() && pursued->isVisible;
    if (avoided.size() < reroutes) {
        avoided.push_back(stuckAt);
    } else if (chases) {
        // A target it sees it never gives up: it keeps away from the last places it got stuck at.
        if (!avoided.empty()) {
            avoided.erase(avoided.begin());
            avoided.push_back(stuckAt);
        }
    } else {
        if (!target) {
            givenGoal.reset();
        } else if (pursued != memory.end()) {
            memory.erase(pursued);
        }
        avoided.clear();
    }
    // Whatever it pursues next, its way is searched anew from where it is.
    plannedGoal.reset();
    plannedFrom.reset();
    forgetCorridor();
    pathError.reset();
}

void Brain::steer(Intent& intent, Vec3 velocity, UnitsPerSecond maxSpeed,
                  std::span<const navigation::SteeringBody> nearby, navigation::NavigationQuery& query) {
    if (!corridor || crossing || length(intent.direction) == 0.0F || maxSpeed <= UnitsPerSecond{}) {
        return;
    }
    const Vec3 steered = acrossOf(corridor->steer(query, {.velocity = velocity,
                                                          .desired = intent.direction * maxSpeed.value(),
                                                          .maxSpeed = maxSpeed,
                                                          .bodies = nearby}));
    const float speed = length(steered);
    if (speed < kLeastSteeredSpeedShare * maxSpeed.value()) {
        intent.direction = {};
        return;
    }
    intent.direction = steered / speed;
    intent.speedShare = std::min(speed / maxSpeed.value(), 1.0F);
}

void Brain::appendWay(navigation::NavigationQuery& query, std::vector<Vec3>& points) const {
    if (corridor) {
        corridor->appendWay(query, points);
    }
}

void Agent::appendWay(navigation::NavigationQuery& query, std::vector<Vec3>& points) const {
    if (followsCorridor(behavior)) {
        brain.appendWay(query, points);
    }
}
} // namespace nx::agents
