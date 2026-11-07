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
/// How far ahead of floor it only fits on crouched, in its body's radii, it holds its duck key: at 250 units
/// a second, measured, its box is ducked 52 units on, and a decision may hold for a tenth of a second before
/// the next one.
constexpr float kCrouchLeadRadii = 6.0F;
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
/// The share of its full speed a body may jump at from a corner, turning from `toCorner` onto `beyond`, both
/// across: the cosine of the turn, none turning a right angle or more, so that it leaves the ground with
/// little speed across its way, on which it flies on in the air. At the share of a corner it walks round, a
/// body flew past the door of a train it jumped at.
[[nodiscard]] auto jumpShareOf(Vec3 toCorner, Vec3 beyond) noexcept -> float {
    const float lengths = length(toCorner) * length(beyond);
    return lengths > 0.0F ? std::clamp(dot(toCorner, beyond) / lengths, 0.0F, 1.0F) : 1.0F;
}
/// How far from what it uses across a body goes no nearer, what is used being in a wall its radius off the
/// floor: well within a player's reach of 64 units (nx::game::playerUse); and as near a volume it walks into
/// to open a door, it heads into the volume.
constexpr HammerUnits kUseReach{48.0F};
/// How far from the start of a door's link, in its body's radii, it stops to wait for the door.
constexpr float kGateWaitRadii = 2.0F;
/// How many links a brain keeps away from at a time, having failed them.
constexpr std::size_t kMostShunnedLinks = 4;
/// The speed across below which a body counts as stopped, and how many decisions in a row of that end its
/// run-up: more than the one at each end of its way back, where it turns.
constexpr UnitsPerSecond kStoppedSpeed{1.0F};
constexpr int kMostRunUpStops = 5;
/// The gate of `gates` whose middle is `middle`, by what a link keeps of it, whole units across: none when
/// there is none, which is taken as a way open, nothing being known to close it.
[[nodiscard]] auto findGate(std::span<const navigation::Gate> gates, Vec3 middle) noexcept
    -> const navigation::Gate* {
    const auto found = std::ranges::find_if(gates, [middle](const navigation::Gate& gate) {
        return navigation::isSameDoor(gate.middle, middle);
    });
    return found != gates.end() ? &*found : nullptr;
}
/// How near the start of a link, in its body's radii, it heads straight for it, as DetourCrowd moves an agent
/// onto one: on a floor no bigger than a block the walls of the mesh are all round it, and it steered away
/// from them.
constexpr float kLinkLeadRadii = 4.0F;
/// How much faster than it just gets there a body leaves the edge of a gap, so that it is over the far side
/// while well above it: at just the speed, it met the side of the block it jumped at a few units below the
/// top.
constexpr float kTakeoffMargin = 1.6F;
/// The share of that speed that takes it: a little short of it as it gets there is no reason to back off
/// again.
constexpr float kEnoughTakeoff = 0.9F;

/// How fast across a body is to jump from the start of `link`, up to `early` short of it, for its box to come
/// down over the floor at its end well above it: kTakeoffMargin times what the gap takes
/// (navigation::leastLeapSpeedOf), as fast as a body of `body` leaves that start at most
/// (navigation::fastestTakeoffOf). None for a ladder, or with no gap.
[[nodiscard]] auto takeoffSpeedOf(const navigation::MeshLink& link, const navigation::AgentProfile& profile,
                                  HammerUnits early, const navigation::BodyMotion& body) noexcept -> float {
    return std::min(navigation::leastLeapSpeedOf(link, profile, early).value() * kTakeoffMargin,
                    navigation::fastestTakeoffOf(link, profile, body).value());
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
/// How far below level a body looks climbing down a ladder it walks over to from behind, and one it has in
/// front of it, as far as its view turns: past 45 degrees what looks down outweighs what goes into the
/// ladder's face, and from behind, what goes into it brings it round to the ladder's front.
constexpr Degrees kLookDownFromBehind{60.0F};
constexpr Degrees kLookDownInFront{89.0F};

/// `way`, a unit direction on the ground plane, looking `below` under level.
[[nodiscard]] auto lookingDown(Vec3 way, Degrees below) noexcept -> Vec3 {
    const float radians = toRadians(below).value();
    return (way * std::cos(radians)) - Vec3{.z = std::sin(radians)};
}

/// `vector` as a unit direction across the ground, `otherwise` when it has no length across.
[[nodiscard]] auto wayAcross(Vec3 vector, Vec3 otherwise) noexcept -> Vec3 {
    const Vec3 across = acrossOf(vector);
    const float size = length(across);
    return size > 0.0F ? across / size : otherwise;
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
/// Heads `intent` over a link for `end`, its body at `feet` moving at `velocity`, as headFor does, driven
/// (Intent::drive) the way its velocity must change for it to go there at the speed it has across, or at
/// `airSpeed` if more. In the air a GoldSrc body gains nothing along the way it flies faster than its air
/// speed already (PM_AirAccelerate's cap), and across that way as much as it wishes for: wishing for the end
/// alone, a body that took off at an angle flew on beside the floor it was to land on.
void flyFor(Intent& intent, Vec3 feet, Vec3 end, Vec3 velocity, UnitsPerSecond airSpeed) noexcept {
    headFor(intent, feet, end);
    const Vec3 steered = intent.direction * std::max(length(acrossOf(velocity)), airSpeed.value());
    intent.steered = steered;
    intent.drive = driveToward(steered, velocity);
}
/// How far across into a volume a body goes to be in it: the game finds it in by the volume's hull, which a
/// box that only meets its side may stay out of, a body standing for good at a door it never opened.
constexpr HammerUnits kEntryDepth{4.0F};
/// The part of `box` at least kEntryDepth inside it across, its middle where it is thinner than twice that.
[[nodiscard]] auto innerOf(const Aabb& box) noexcept -> Aabb {
    const auto inner = [](float low, float high) {
        const float depth = kEntryDepth.value();
        return high - low > 2.0F * depth ? std::pair{low + depth, high - depth}
                                         : std::pair{(low + high) * 0.5F, (low + high) * 0.5F};
    };
    const auto [lowX, highX] = inner(box.min.x, box.max.x);
    const auto [lowY, highY] = inner(box.min.y, box.max.y);
    return Aabb{.min = {.x = lowX, .y = lowY, .z = box.min.z},
                .max = {.x = highX, .y = highY, .z = box.max.z}};
}
/// The point of `box` nearest `feet` across, at the height of `feet`.
[[nodiscard]] auto nearestAcross(const Aabb& box, Vec3 feet) noexcept -> Vec3 {
    const Vec3 nearest = box.closestPoint(feet);
    return Vec3{.x = nearest.x, .y = nearest.y, .z = feet.z};
}
/// Whether the standing box of a body of `profile` with its feet at `feet` is in `volume`, kEntryDepth into
/// it across, as the game finds a body in a volume it touches.
[[nodiscard]] auto isIn(const Aabb& volume, Vec3 feet, const navigation::AgentProfile& profile) noexcept
    -> bool {
    const float radius = profile.radius.value();
    const Aabb body{.min = feet - Vec3{.x = radius, .y = radius},
                    .max = feet + Vec3{.x = radius, .y = radius, .z = profile.standingHeight.value()}};
    return body.intersects(innerOf(volume));
}
/// Climbs `intent` across `link`, a ladder, from `feet`, as Brain::decide says, a body of `radius` and
/// `stepHeight`.
void climbAcross(Intent& intent, Vec3 feet, const navigation::MeshLink& link, float radius,
                 float stepHeight) noexcept {
    const Vec3 span = acrossOf(link.end - link.start);
    const Vec3 way = wayAcross(link.through - feet, wayAcross(span, Vec3{.x = 1.0F}));
    if (link.end.z > link.start.z) {
        const bool isUp = feet.z + stepHeight >= link.end.z;
        const Vec3 going = isUp ? wayAcross(link.end - feet, way) : way;
        intent.direction = going;
        intent.ahead = going;
        intent.climb = going;
        return;
    }
    // A floor on the ladder's far side, which it walks over to from: its way stays the same as it comes round
    // to the ladder's front. One on the same side has it in front of it all the way.
    const bool isFromBehind = length(span) > radius;
    const Vec3 going = isFromBehind ? span / length(span) : way;
    intent.direction = going;
    intent.ahead = going;
    intent.climb = lookingDown(going, isFromBehind ? kLookDownFromBehind : kLookDownInFront);
}

/// Whether a body at `feet` crossed `link`: it stands within `stepHeight` of the height of the link's end,
/// as far from its start across as the end is, but `slack`, the way it drops or jumps over.
[[nodiscard]] auto hasCrossed(const navigation::MeshLink& link, Vec3 feet, float stepHeight,
                              float slack) noexcept -> bool {
    return std::abs(feet.z - link.end.z) <= stepHeight &&
           groundDistance(feet, link.start) + slack >= groundDistance(link.end, link.start);
}
} // namespace

auto driveToward(Vec3 steered, Vec3 velocity) noexcept -> std::optional<Vec3> {
    // A body that gains speed only along the way it is driven (PM_Accelerate), and loses the rest to friction
    // alone, keeps drifting across its way when driven the way it should go, and circles round what it heads
    // for: it is to be driven the way its velocity must change to become the steered one, as DetourCrowd
    // integrates an agent's velocity toward its desired one.
    const Vec3 change = steered - acrossOf(velocity);
    const float changeLength = length(change);
    if (changeLength > kLeastSteeredSpeedShare * length(steered)) {
        return change / changeLength;
    }
    return std::nullopt;
}

Brain::Brain(Rules settings) : rules{settings} {
    expects(
        rules.memoryLimit > 0 && rules.memoryLifetime > Seconds{} && rules.attackDistance > HammerUnits{} &&
            rules.arrivalDistance > HammerUnits{} && rules.replanDistance > HammerUnits{} &&
            rules.reactionTime >= Seconds{} && std::isfinite(rules.replanShare) &&
            rules.replanShare >= 0.0F && rules.shortTermMemory >= Seconds{} &&
            rules.threatRecency >= Seconds{} && rules.threatMargin >= HammerUnits{} &&
            std::isfinite(rules.braking) && rules.braking >= 0.0F && std::isfinite(rules.stopSpeed.value()) &&
            rules.stopSpeed >= UnitsPerSecond{} && std::isfinite(rules.acceleration) &&
            rules.acceleration >= 0.0F && std::isfinite(rules.crouchedShare) && rules.crouchedShare > 0.0F &&
            rules.crouchedShare <= 1.0F && std::isfinite(rules.airSpeed.value()) &&
            rules.airSpeed >= UnitsPerSecond{},
        "Brain: positive rules, a memory budget, and a reaction time, a replan share, a short-term "
        "memory, a threat's recency and margin, a braking, a stop speed, an acceleration and an air speed of "
        "none or more, and a crouched share of more than none to one");
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

void Brain::planIfNeeded(Vec3 feet, bool isOnGround, Vec3 goal, std::optional<ecs::Entity> pursued,
                         navigation::NavigationQuery& query, std::span<const navigation::Gate> gates,
                         const navigation::BodyMotion& body) {
    const navigation::AgentProfile& profile = query.mesh().profile();
    if (crossing) {
        leftGround = leftGround || !isOnGround;
        const float stepHeight = profile.stepHeight.value();
        // Only on the ground: flying along the face of a ledge it jumped at, a body was as high as its top
        // and as far from the start as the end, with all its way still to go.
        if (isOnGround && hasCrossed(*crossing, feet, stepHeight, rules.arrivalDistance.value())) {
            crossing.reset(); // it walks along its corridor again, from the link's end
        } else if (isOnGround && leftGround && std::abs(feet.z - crossing->end.z) > stepHeight) {
            // Back on the ground below the link's end, as it fell off a ledge it jumped at. Its corridor goes
            // on from up there, and its one jump is spent: pushing on at the ledge's face, it stayed until
            // its watch gave its way up, and then its goal. Shot off it, it failed nothing of the link's.
            if (!isKnockedOffCrossing) {
                noteLinkFailed(*crossing);
            }
            forgetCorridor();
            plannedGoal.reset();
        }
    }
    // A body away from its corridor (spawned again, teleported, pushed off the mesh) searches its way anew
    // from where it is, rather than walking straight at a corridor left behind: away from where it stands
    // on the mesh, so that a body standing past the end of the mesh's floor, or on something it drops from,
    // keeps the way it walks back on; one nowhere near the mesh searches, and is blocked. One across a
    // link searches none until it lands, as DetourCrowd plans none for an agent over a link.
    const bool walksAlong = corridor && !crossing;
    const auto standsAt = walksAlong ? query.nearestPoint(feet) : Result<Vec3, navigation::NavigationError>{};
    const bool isAwayFromCorridor = [&] {
        if (!walksAlong) {
            return false;
        }
        if (!standsAt) {
            return true;
        }
        const Vec3 alongCorridor = corridor->moveAgent(query, feet);
        // On the ground on another floor than its corridor, as it fell off a block it stood on: right below
        // it that is no way off across, and pushing on at the block's face it stayed.
        return groundDistance(alongCorridor, *standsAt) > rules.replanDistance.value() ||
               (isOnGround &&
                std::abs(alongCorridor.z - standsAt->z) > query.mesh().profile().stepHeight.value());
    }();
    // A body off the mesh may land on it at any moment; how far a way is not there changes only as it or its
    // goal moves.
    const bool wasOffTheMesh = pathError == navigation::NavigationError::OutsideMesh;
    if (!crossing &&
        (target != pursued || !plannedGoal || isAwayFromCorridor || movedFarEnough(feet, goal) ||
         wasOffTheMesh || body != plannedBody ||
         (pathError && plannedFrom && groundDistance(*plannedFrom, feet) >= rules.replanDistance.value()))) {
        if (target != pursued) {
            avoided.clear(); // another pursuit, which got stuck nowhere yet
            shunnedLinks.clear();
        }
        target = pursued;
        plannedGoal = goal;
        plannedFrom = feet;
        plannedBody = body;
        forgetCorridor();
        pathError.reset();
        const navigation::PathRequest request{.from = feet,
                                              .to = goal,
                                              .avoided = avoided,
                                              .gates = gates,
                                              .shunned = shunnedLinks,
                                              .body = body};
        // By a way no door closes, however long: that is what it comes down to on its way to open one.
        // Failing that, through what opens to it, to wait there for one of them to open it.
        auto planned = navigation::Corridor::plan(query, request);
        if (!planned && !opening) {
            navigation::PathRequest throughDoors = request;
            throughDoors.opensGates = true;
            planned = navigation::Corridor::plan(query, throughDoors);
        }
        if (!planned && !shunnedLinks.empty()) {
            // No way but over what it failed: what stopped it may be gone.
            shunnedLinks.clear();
            navigation::PathRequest overAny = request;
            overAny.opensGates = !opening;
            overAny.shunned = {};
            planned = navigation::Corridor::plan(query, overAny);
        }
        if (planned) {
            corridor.emplace(std::move(*planned));
        } else {
            pathError = planned.error().code;
        }
    }
}

void Brain::noteLinkFailed(const navigation::MeshLink& link) {
    if (failedLink != link) {
        // Once may be another body in the way, or a push: it goes at it again.
        failedLink = link;
        return;
    }
    failedLink.reset();
    if (shunnedLinks.size() == kMostShunnedLinks) {
        shunnedLinks.erase(shunnedLinks.begin());
    }
    shunnedLinks.push_back(link);
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

void Brain::openGate(Vec3 door, navigation::GateOpener opener) noexcept {
    opening = OpeningGate{.door = door, .opener = opener};
    plannedGoal.reset();
}

auto Brain::gateItOpens() const noexcept -> std::optional<Vec3> {
    return opening ? std::optional{opening->door} : std::nullopt;
}

auto Brain::decide(Vec3 feet, navigation::NavigationQuery& query, UnitsPerSecond maxSpeed, bool isOnGround,
                   Vec3 velocity, std::span<const navigation::Gate> gates) -> Decision {
    if (opening) {
        const navigation::Gate* const gate = findGate(gates, opening->door);
        if (gate == nullptr || gate->isOpen) {
            // Open, by it or another: on with what it pursues.
            opening.reset();
            plannedGoal.reset();
        }
    }
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
    const Vec3 pursuedGoal = selected != nullptr ? selected->observation.position : *givenGoal;
    const Vec3 goal = opening ? opening->opener.box.center() : pursuedGoal;
    const std::optional<ecs::Entity> pursued =
        selected != nullptr ? std::optional{selected->observation.entity} : std::nullopt;
    Decision decision{.behavior = Behavior::MoveTo};
    if (selected != nullptr) {
        decision = {
            .intent = {.lookAt = selected->observation.aimAt.value_or(goal)},
            .behavior = selected->isVisible ? Behavior::Chase : Behavior::Investigate,
            .target = pursued,
        };
        // Near its feet, or its eyes, what is under it: from up on a body's head its feet are out of reach,
        // while the strike goes from its own eyes down.
        const Vec3 aimedAt = selected->observation.aimAt.value_or(pursuedGoal);
        if (selected->isVisible &&
            std::min(length(pursuedGoal - feet), length(aimedAt - feet)) <= rules.attackDistance.value()) {
            decision.behavior = Behavior::Attack;
            decision.intent.attacks = true;
            return decision;
        }
    }
    const navigation::AgentProfile& profile = query.mesh().profile();
    planIfNeeded(feet, isOnGround, goal, pursued, query, gates, bodyMotion(maxSpeed));
    if (pathError) {
        decision.behavior = Behavior::Blocked;
        decision.navigationError = pathError;
        // Off the mesh it may be holding a ladder, which holds it there for good: a jump lets go of it.
        decision.intent.jumps = pathError == navigation::NavigationError::OutsideMesh;
        return decision;
    }
    invariant(corridor.has_value(), "Brain: a planned corridor or a reported path error");
    std::ignore = corridor->moveTarget(query, goal);
    if (opening && !crossing) {
        const navigation::GateOpener& opener = opening->opener;
        const bool isThere = groundDistance(feet, corridor->target()) <= rules.arrivalDistance.value();
        if (opener.kind == navigation::OpenerKind::Used &&
            (isThere || groundDistance(feet, opener.box.center()) <= kUseReach.value())) {
            // Near what opens the door: it uses it, looking at it.
            decision.intent.lookAt = opener.box.center();
            decision.intent.uses = true;
            decision.intent.direction = {};
            return decision;
        }
        if (opener.kind == navigation::OpenerKind::Entered && (isThere || isIn(opener.box, feet, profile))) {
            // In what opens the door as it touches it: it stands there, looking at the door, for it to open.
            decision.intent.lookAt = opening->door;
            decision.intent.direction = {};
            return decision;
        }
    }
    const HammerUnits crouchLead{kCrouchLeadRadii * profile.radius.value()};
    // At the end of a link onto floor it only fits on crouched: down a ladder into a vent, standing, its head
    // kept it in the shaft. Off the ground a duck is at once, so as it gets there: going up, as near as its
    // head meets what is over that floor; going down, within a step, since ducked at the top of a ladder a
    // body walks to it at a third of its speed.
    const auto crouchesAcross = [&] {
        // From the jump on, measured, from when it leaves the ground to its top alike, until it is back on
        // the ground: ducked on the very edge of a ledge, on the friction of an edge (PM_Friction's
        // edgefriction), a body gains no speed at all. Up a crouched jump, and over a gap down or across one,
        // which a ducked body flies alone, its head under what a standing one meets: jumping across it
        // standing, a body met the ceiling over a gap and fell short.
        if (crossing->kind == navigation::AreaKind::CrouchedJump &&
            (crossing->end.z > crossing->start.z || crossing->takeoff) && (!isOnGround || !leftGround)) {
            return true;
        }
        const float heightLeft = crossing->end.z - feet.z;
        const bool isThere = crossing->end.z > crossing->start.z
                                 ? heightLeft <= profile.standingHeight.value()
                                 : -heightLeft <= profile.stepHeight.value();
        return isThere && groundDistance(crossing->end, feet) <= crouchLead.value() &&
               corridor->crouchesWithin(query, crouchLead);
    };
    if (crossing) {
        if (crossing->kind == navigation::AreaKind::Ladder) {
            climbAcross(decision.intent, feet, *crossing, profile.radius.value(), profile.stepHeight.value());
        } else {
            flyFor(decision.intent, feet, crossing->end, velocity, rules.airSpeed);
        }
        decision.intent.crouches = crouchesAcross();
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
    if (beginCrossing(decision, feet, isOnGround, velocity, maxSpeed, query, gates, nextLink)) {
        decision.intent.crouches = crouchesAcross();
        return decision;
    }
    headsForLink =
        nextLink && groundDistance(feet, nextLink->start) <= kLinkLeadRadii * profile.radius.value();
    if (nextLink && nextLink->kind == navigation::AreaKind::Door) {
        const navigation::Gate* const gate = findGate(gates, nextLink->through);
        if (gate != nullptr && !gate->isOpen) {
            decision.closedGate = gate->middle;
            // What opens it there, as a door used only is opened by itself, it goes on to and uses, facing
            // it; what opens it as it walks into it on its side of the door, it walks into and stands in,
            // facing the door; else it waits at the door, facing it, for it to open.
            const auto isOnItsSide = [&](const navigation::GateOpener& opens) {
                return dot(acrossOf(opens.box.center() - gate->middle),
                           acrossOf(nextLink->start - gate->middle)) > 0.0F;
            };
            const auto opener = std::ranges::find_if(gate->openers, [&](const navigation::GateOpener& opens) {
                return opens.kind == navigation::OpenerKind::Used
                           ? groundDistance(nextLink->start, opens.box.center()) <= kUseReach.value()
                           : isOnItsSide(opens) &&
                                 groundDistance(nextLink->start, nearestAcross(opens.box, nextLink->start)) <=
                                     kUseReach.value();
            });
            const bool hasOpener = opener != gate->openers.end();
            if (hasOpener && opener->kind == navigation::OpenerKind::Entered &&
                groundDistance(feet, nearestAcross(opener->box, feet)) <= kUseReach.value()) {
                decision.intent.lookAt = gate->middle;
                if (isIn(opener->box, feet, profile)) {
                    decision.intent.direction = {};
                } else {
                    headFor(decision.intent, feet, nearestAcross(innerOf(opener->box), feet));
                }
                return decision;
            }
            if (hasOpener && opener->kind == navigation::OpenerKind::Used &&
                groundDistance(feet, opener->box.center()) <= kUseReach.value()) {
                decision.intent.ahead = wayAcross(opener->box.center() - feet, decision.intent.ahead);
                decision.intent.lookAt = opener->box.center();
                decision.intent.uses = true;
                return decision;
            }
            if (!hasOpener &&
                groundDistance(feet, nextLink->start) <= kGateWaitRadii * profile.radius.value()) {
                decision.intent.lookAt = decision.intent.lookAt.value_or(gate->middle);
                decision.intent.ahead = wayAcross(gate->middle - feet, decision.intent.ahead);
                return decision;
            }
        }
    }
    if (runUpFrom) {
        // Stopped on its way back, by what the mesh does not tell of: that is all the run it has.
        runUpStops = length(acrossOf(velocity)) < kStoppedSpeed.value() ? runUpStops + 1 : 0;
        if (nextLink && groundDistance(feet, *runUpFrom) > rules.arrivalDistance.value() &&
            runUpStops < kMostRunUpStops) {
            headFor(decision.intent, feet, *runUpFrom);
            return decision;
        }
        runUpFrom.reset();
        runUpStops = 0;
        hasRunUp = true;
    }
    // Up to a crouched jump it goes standing, its link's start room enough for that: what lifts its feet is
    // ducking in the air, and from the ground ducked already it fell short.
    const bool jumpsCrouchedNext = nextLink && nextLink->kind == navigation::AreaKind::CrouchedJump &&
                                   nextLink->end.z > nextLink->start.z &&
                                   groundDistance(feet, nextLink->start) <= crouchLead.value();
    decision.intent.crouches = !jumpsCrouchedNext && corridor->crouchesWithin(query, crouchLead);
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
    const auto isAhead = [this, feet](Vec3 corner) {
        return groundDistance(feet, corner) > rules.arrivalDistance.value();
    };
    auto next = std::find_if(from, corners.end(), isAhead);
    if (next == corners.end() && from != corners.begin()) {
        // Back past the corner it reached, as it wiggled out of where it got stuck, the corners it sees ahead
        // end there: heading on from it, it stood with nothing to head for, so moving nowhere and not stuck.
        from = corners.begin();
        next = std::find_if(from, corners.end(), isAhead);
    }
    if (next != from) {
        reachedCorner = *std::ranges::prev(next);
    }
    if (next != corners.end()) {
        approach(decision, feet, std::span{corners}.subspan(static_cast<std::size_t>(next - corners.begin())),
                 profile, nextLink, maxSpeed);
        const Vec3 toNext = acrossOf(*next - feet);
        const Vec3 toAfter = acrossOf(*std::ranges::next(next, 1, corners.end() - 1) - feet);
        const float afterLength = length(toAfter);
        const Vec3 anticipated = afterLength > 0.0F
                                     ? toNext - (toAfter * (length(toNext) * kTurnAnticipation / afterLength))
                                     : toNext;
        const Vec3 direction = length(anticipated) > 0.0F ? anticipated : toNext;
        decision.intent.direction = direction / length(direction);
        decision.intent.ahead = decision.intent.direction;
    } else if (!corners.empty()) {
        // All it sees of a way that goes on within arrival of it, corners of small polygons just short of a
        // link's start: on to the last of them, as it stood for good with nothing to head for, moving nowhere
        // and so not stuck.
        headFor(decision.intent, feet, corners.back());
    }
    return decision;
}

void Brain::approach(Decision& decision, Vec3 feet, std::span<const Vec3> ahead,
                     const navigation::AgentProfile& profile, const std::optional<navigation::MeshLink>& next,
                     UnitsPerSecond maxSpeed) {
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
            const Vec3 into = acrossOf(way[corner] - way[corner - 1]);
            const Vec3 beyond = acrossOf(pointAlong(way, corner, span) - way[corner]);
            const bool jumpsThere = corner == lastCorner && next &&
                                    next->kind != navigation::AreaKind::Ladder && next->end.z > next->start.z;
            // Over a gap, as fast as the flight takes, and no faster: it lands past a small floor else. A gap
            // that takes a run is in its corridor only for a body that runs faster than the air gives it
            // (navigation::fastestTakeoffOf), so that its share of that body's speed is one of a speed.
            const float takeoff =
                corner == lastCorner && next
                    ? takeoffSpeedOf(*next, profile, rules.arrivalDistance, bodyMotion(maxSpeed))
                    : 0.0F;
            const float passShare = takeoff > rules.airSpeed.value() ? takeoff / maxSpeed.value()
                                    : jumpsThere                     ? jumpShareOf(into, beyond)
                                                                     : passShareOf(into, beyond);
            intent.approaches[intent.approachCount++] = {.distance = HammerUnits{along},
                                                         .passShare = passShare};
        }
    }
}

auto Brain::beginCrossing(Decision& decision, Vec3 feet, bool isOnGround, Vec3 velocity,
                          UnitsPerSecond maxSpeed, navigation::NavigationQuery& query,
                          std::span<const navigation::Gate> gates, std::optional<navigation::MeshLink>& next)
    -> bool {
    next = corridor->nextLink(query);
    if (!next || groundDistance(feet, next->start) > rules.arrivalDistance.value()) {
        return false;
    }
    if (next->kind == navigation::AreaKind::Door) {
        const navigation::Gate* const gate = findGate(gates, next->through);
        if (gate != nullptr && !gate->isOpen) {
            return false;
        }
    }
    const navigation::AgentProfile& linkProfile = query.mesh().profile();
    const navigation::BodyMotion body = bodyMotion(maxSpeed);
    const float takeoff = takeoffSpeedOf(*next, linkProfile, rules.arrivalDistance, body);
    // A link up it jumps once, from its start, and one over a gap; one down it walks off.
    const bool jumps = next->kind != navigation::AreaKind::Ladder &&
                       (next->end.z > next->start.z || takeoff > rules.airSpeed.value());
    if (jumps && !isOnGround) {
        // In the air, as a body running down stairs flies from step to step, a jump is nothing, and it would
        // spend the one it has for the link: it walked off the step it was to leap from into the gap.
        return false;
    }
    const Vec3 across = wayAcross(next->end - next->start, Vec3{.x = 1.0F});
    if (takeoff > rules.airSpeed.value() && !hasRunUp &&
        dot(acrossOf(velocity), across) < takeoff * kEnoughTakeoff) {
        // Too slow for the gap, it backs off along the way across it for a run at it, as far as its run gains
        // it the speed, no farther than the floor behind the start goes, straight back from it on the mesh:
        // open ground, as a link that tells nothing of its start is left, has no end of it.
        const std::optional<HammerUnits> run =
            navigation::runUpOf(*next, linkProfile, body, UnitsPerSecond{takeoff});
        const HammerUnits room = next->takeoff ? next->takeoff->room : run.value_or(linkProfile.radius);
        runUpFrom = next->start - (across * std::min(run.value_or(room), room).value());
        return false;
    }
    hasRunUp = false;
    runUpFrom.reset();
    crossing = corridor->crossLink(query);
    if (!crossing) {
        next = corridor->nextLink(query);
        return false;
    }
    leftGround = false;
    isKnockedOffCrossing = false;
    if (crossing->kind == navigation::AreaKind::Ladder) {
        const navigation::AgentProfile& profile = query.mesh().profile();
        climbAcross(decision.intent, feet, *crossing, profile.radius.value(), profile.stepHeight.value());
        return true;
    }
    decision.intent.jumps = jumps;
    flyFor(decision.intent, feet, crossing->end, velocity, rules.airSpeed);
    return true;
}

void Brain::forgetCorridor() noexcept {
    corridor.reset();
    crossing.reset();
    reachedCorner.reset();
    runUpFrom.reset();
    runUpStops = 0;
    hasRunUp = false;
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
    // Crouched, it goes at a third of what it asks, which it may stop from anywhere, and any less is too
    // little to gain it speed.
    for (const Approach& approached : intent.crouches ? std::span<const Approach>{} : intent.approached()) {
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
    const Vec3 avoiding = nearby.empty() || headsForLink
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
    const float speed =
        intent.crouches ? wished : std::clamp(avoidingSpeed, std::min(leastWalk, wished), wished);
    const Vec3 steered = intent.direction * speed;
    // Short of that speed it asks all it has, what a GoldSrc body gains going by what it asks
    // (PM_Accelerate): next to a drop, on the friction of an edge (PM_Friction's edgefriction), a walk asked
    // for at the speed it wants gained it nothing, and a body landed on a small block crept off it.
    intent.speedShare = (length(acrossOf(velocity)) < speed ? maxSpeed.value() : speed) / maxSpeed.value();
    intent.steered = steered;
    intent.drive = driveToward(steered, velocity);
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
