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
/// How far past a corner, in its body's radii, the way on is measured from to tell how sharply the way
/// turns there: about how wide the body turns at its full speed, so that a corner rounded into short turns
/// counts as the one turn it is.
constexpr float kTurnSpanRadii = 4.0F;
/// `vector` across the ground, its height left out.
[[nodiscard]] auto acrossOf(Vec3 vector) noexcept -> Vec3 {
    return Vec3{.x = vector.x, .y = vector.y};
}
[[nodiscard]] auto groundDistance(Vec3 first, Vec3 second) noexcept -> float {
    return std::hypot(first.x - second.x, first.y - second.y);
}
/// The point `span` along the way through `points` past its point `from`, across; its last point when the
/// way ends before.
[[nodiscard]] auto pointAlong(std::span<const Vec3> points, std::size_t from, float span) noexcept -> Vec3 {
    for (std::size_t point = from + 1; point < points.size(); ++point) {
        const float step = groundDistance(points[point - 1], points[point]);
        if (step >= span && step > 0.0F) {
            return points[point - 1] + ((points[point] - points[point - 1]) * (span / step));
        }
        span -= step;
    }
    return points.back();
}
/// The share of its full speed a body may pass a corner at, turning from `toCorner` onto `beyond`, both
/// across: the cosine of half the turn, sqrt((1 + cos turn) / 2): 1 straight on, 0.71 at a right angle,
/// none turning back. Its turning circle shrinks with the square of its speed.
[[nodiscard]] auto passShareOf(Vec3 toCorner, Vec3 beyond) noexcept -> float {
    const float lengths = length(toCorner) * length(beyond);
    if (lengths <= 0.0F) {
        return 1.0F;
    }
    const float turnCosine = std::clamp(dot(toCorner, beyond) / lengths, -1.0F, 1.0F);
    return std::sqrt((1.0F + turnCosine) * 0.5F);
}
/// The speed a body can still stop from by friction alone within `distance`, as PM_Friction slows it by
/// `rules`: below its stop speed it loses stopSpeed times braking a second, so the root of twice that times
/// the distance; faster, it loses braking times its speed a second, down to its stop speed within how much
/// faster it goes over braking. Braking times the distance alone undercut a slow body: 12 units short of a
/// stop, at rest, it asked for 48 units a second, which its friction takes more off than it gains it, and
/// it crept there at 2.
[[nodiscard]] auto stoppingSpeedOf(const Rules& rules, HammerUnits distance) noexcept -> float {
    const float stopSpeed = rules.stopSpeed.value();
    const float slowStretch = stopSpeed / (2.0F * rules.braking);
    if (distance.value() <= slowStretch) {
        return std::sqrt(2.0F * rules.braking * stopSpeed * distance.value());
    }
    return stopSpeed + (rules.braking * (distance.value() - slowStretch));
}
/// The slowest walk a body gains speed at by `rules` (Rules::acceleration): twice the walk whose gain its
/// friction takes below its stop speed, stopSpeed times braking over acceleration; none without one.
[[nodiscard]] auto leastWalkOf(const Rules& rules) noexcept -> float {
    return rules.acceleration > 0.0F ? 2.0F * rules.stopSpeed.value() * rules.braking / rules.acceleration
                                     : 0.0F;
}
/// How far off its way, as a cosine, a body is in the way of another (IsFriendInTheWay's line of movement):
/// within an eighth of a turn.
constexpr float kWayConeCosine = std::numbers::sqrt2_v<float> / 2.0F;
/// Whether a body among `nearby` in the way of a body of `radius` at `feet` going `way` is nearer than it to
/// `end`, where its way ends (Condition Zero's IsFriendInTheWay): within half a body's width of touching it,
/// ahead within kWayConeCosine of its way. Bodies going to one end wait for each other in the order of how
/// near it they are, so that none waits for one that waits for it.
[[nodiscard]] auto isWayTaken(Vec3 feet, Vec3 way, Vec3 end, float radius,
                              std::span<const navigation::SteeringBody> nearby) noexcept -> bool {
    const float ownDistance = groundDistance(feet, end);
    return std::ranges::any_of(nearby, [=](const navigation::SteeringBody& body) {
        const Vec3 toBody = acrossOf(body.feet - feet);
        const float distance = length(toBody);
        return distance - radius - body.radius.value() <= radius &&
               dot(toBody, way) >= kWayConeCosine * distance && groundDistance(body.feet, end) < ownDistance;
    });
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
                rules.threatMargin >= HammerUnits{} && std::isfinite(rules.braking) &&
                rules.braking >= 0.0F && std::isfinite(rules.stopSpeed.value()) &&
                rules.stopSpeed >= UnitsPerSecond{} && std::isfinite(rules.acceleration) &&
                rules.acceleration >= 0.0F,
            "Brain: positive rules, a memory budget, and a reaction time, a replan share, a short-term "
            "memory, a threat's recency and margin, a braking, a stop speed and an acceleration of none or "
            "more");
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

void Brain::planIfNeeded(Vec3 feet, Vec3 goal, std::optional<ecs::Entity> pursued,
                         navigation::NavigationQuery& query) {
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
}

void Brain::finishArrival(const Memory* selected) {
    if (selected == nullptr) {
        givenGoal.reset();
    } else if (!selected->isVisible) {
        const ecs::Entity investigated = selected->observation.entity;
        std::erase_if(memory, [investigated](const Memory& record) -> bool {
            return record.observation.entity == investigated;
        });
    }
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
        decision = {
            .intent = {.lookAt = selected->observation.aimAt.value_or(goal)},
            .behavior = selected->isVisible ? Behavior::Chase : Behavior::Investigate,
            .target = pursued,
        };
        if (selected->isVisible && length(goal - feet) <= rules.attackDistance.value()) {
            decision.behavior = Behavior::Attack;
            decision.intent.attacks = true;
            return decision;
        }
    }
    const navigation::AgentProfile& profile = query.mesh().profile();
    planIfNeeded(feet, goal, pursued, query);
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
    // Arrived as near its end across as a corner counts as reached, and on its floor: a three-dimensional
    // measure would leave a body on a slope short of it with no corner to steer to.
    const Vec3 end = corridor->target();
    if (groundDistance(walkablePosition, end) <= rules.arrivalDistance.value() &&
        std::abs(walkablePosition.z - end.z) <= profile.stepHeight.value()) {
        finishArrival(selected);
        decision.behavior = Behavior::Idle;
        return decision;
    }
    std::optional<navigation::MeshLink> nextLink;
    if (beginCrossing(decision, feet, query, nextLink)) {
        return decision;
    }
    // Toward the first corner on from the last it reached that is farther than arrival, the turn after it
    // anticipated (calcSmoothSteerDirection). Its funnel lists a corner it reached until it crosses the edge
    // of the polygon the corner ends, which a body that cannot turn on the spot, rounding it wide, may not
    // have yet as it drifts away from it: measured anew, the corner was behind it, and it turned back to it.
    const std::vector<Vec3> corners = corridor->corners(query, kSteeringCorners);
    auto from = corners.begin();
    if (const auto reached = reachedCorner ? std::ranges::find(corners, *reachedCorner) : corners.end();
        reached != corners.end()) {
        from = std::ranges::next(reached);
    }
    const auto next = std::find_if(from, corners.end(), [this, feet](Vec3 corner) {
        return groundDistance(feet, corner) > rules.arrivalDistance.value();
    });
    if (next != from) {
        reachedCorner = *std::ranges::prev(next);
    }
    if (next != corners.end()) {
        approach(decision, feet, std::span{corners}.subspan(static_cast<std::size_t>(next - corners.begin())),
                 profile, nextLink);
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

void Brain::approach(Decision& decision, Vec3 feet, std::span<const Vec3> ahead,
                     const navigation::AgentProfile& profile,
                     const std::optional<navigation::MeshLink>& next) {
    if (rules.braking <= 0.0F || ahead.empty()) {
        return;
    }
    const auto isAt = [this, &profile](Vec3 first, Vec3 second) {
        return groundDistance(first, second) <= rules.arrivalDistance.value() &&
               std::abs(first.z - second.z) <= profile.stepHeight.value();
    };
    // Its way from its feet through the corners ahead, and on over the link whose start the last is.
    std::vector<Vec3> way{feet};
    way.insert(way.end(), ahead.begin(), ahead.end());
    const std::size_t lastCorner = way.size() - 1;
    if (next && isAt(next->start, way.back())) {
        way.push_back(next->end);
    }
    // Each corner by the turn onto the way on past it, and its way's end, where it stops unless it chases a
    // target.
    const Vec3 end = corridor->target();
    const bool stops = decision.behavior == Behavior::MoveTo || decision.behavior == Behavior::Investigate;
    const float span = kTurnSpanRadii * profile.radius.value();
    Intent& intent = decision.intent;
    float along = 0.0F;
    for (std::size_t corner = 1; corner <= lastCorner && intent.approachCount < kMostApproaches; ++corner) {
        along += groundDistance(way[corner - 1], way[corner]);
        if (corner == lastCorner && stops && isAt(way[corner], end)) {
            intent.approaches[intent.approachCount++] = {.distance = HammerUnits{along}, .passShare = 0.0F};
        } else if (corner + 1 < way.size()) {
            const Vec3 onward = pointAlong(way, corner, span);
            intent.approaches[intent.approachCount++] = {
                .distance = HammerUnits{along},
                .passShare =
                    passShareOf(acrossOf(way[corner] - way[corner - 1]), acrossOf(onward - way[corner]))};
        }
    }
}

auto Brain::beginCrossing(Decision& decision, Vec3 feet, navigation::NavigationQuery& query,
                          std::optional<navigation::MeshLink>& next) -> bool {
    next = corridor->nextLink(query);
    if (!next || groundDistance(feet, next->start) > rules.arrivalDistance.value()) {
        return false;
    }
    crossing = corridor->crossLink(query);
    if (!crossing) {
        next = corridor->nextLink(query);
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
    reachedCorner.reset();
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
    // No faster than it can still slow down for what it approaches, and no slower than it gains speed at
    // but to stop: asked for less, a GoldSrc body slowed for a sharp corner or round a body near it stopped
    // dead, and at rest gained nothing (PM_Friction takes 3 units a second each tick below sv_stopspeed, and
    // PM_Accelerate gives a twentieth of the walk), as stuck as a stuck watch finds a body.
    const float leastWalk = std::min(leastWalkOf(rules), maxSpeed.value());
    float wished = maxSpeed.value();
    bool stops = false;
    for (const Approach& approached : intent.approached()) {
        const float passing =
            (approached.passShare * maxSpeed.value()) + stoppingSpeedOf(rules, approached.distance);
        if (passing < wished) {
            wished = passing;
            stops = approached.passShare <= 0.0F;
        }
    }
    if (!stops) {
        wished = std::max(wished, leastWalk);
    }
    // Round the bodies near it, the walls weighed as it goes round them: with none near it, its way along its
    // corridor keeps it off the walls already, and the avoidance, which weighs a wall its way turns from
    // before it meets it, held a body at rest in a passage that turns where it was. It waits only behind a
    // body in its way nearer than it to where its way ends that it finds no way round: of bodies that all
    // waited when none found a way on worth a tenth of its speed, each in another's way, none went on.
    const Vec3 avoiding = nearby.empty()
                              ? intent.direction * wished
                              : acrossOf(corridor->steer(query, {.velocity = velocity,
                                                                 .desired = intent.direction * wished,
                                                                 .maxSpeed = UnitsPerSecond{wished},
                                                                 .bodies = nearby}));
    const float avoidingSpeed = length(avoiding);
    if (avoidingSpeed >= kLeastSteeredSpeedShare * wished) {
        intent.direction = avoiding / avoidingSpeed;
    } else if (isWayTaken(corridor->position(), intent.direction, corridor->target(),
                          query.mesh().profile().radius.value(), nearby)) {
        intent.direction = {};
        return;
    }
    const float speed = std::clamp(avoidingSpeed, std::min(leastWalk, wished), wished);
    const Vec3 steered = intent.direction * speed;
    intent.speedShare = speed / maxSpeed.value();
    // A body that gains speed only along the way it is driven (PM_Accelerate), and loses the rest to friction
    // alone, keeps drifting across its way when driven the way it should go, and circles round what it heads
    // for: it is to be driven the way its velocity must change to become the steered one, as DetourCrowd
    // integrates an agent's velocity toward its desired one.
    const Vec3 change = steered - acrossOf(velocity);
    const float changeLength = length(change);
    if (changeLength > kLeastSteeredSpeedShare * speed) {
        intent.drive = change / changeLength;
    }
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
