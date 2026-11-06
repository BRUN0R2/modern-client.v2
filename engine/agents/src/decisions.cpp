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

/// The most cells of a BodyGrid along a side: enough for a map's ground at any steering range, and bounding
/// its room however far apart its bodies are.
constexpr std::size_t kMostGridCellsAcross = 256;

/// The fewest bodies it is worth handing to a worker to find their squares: little work each.
constexpr std::size_t kSmallestGridBatch = 8192;

/// The bodies of a tick by the square of the ground each stands in, each as wide as how far bodies are looked
/// for: those near a point are in its square and those around it, however many the others are.
class BodyGrid {
public:
    /// The grid of `bodies`, which must outlive it, of squares `width` wide, in jobs of `jobs` and on the
    /// memory of `room`, which it writes.
    template <class Room>
    BodyGrid(jobs::JobSystem& jobs, std::span<const Body> bodies, float width, Room& room) : cellSize{width} {
        const jobs::JobDesc desc{.label = "agents.grid", .priority = jobs::Priority::Critical};
        // Where they stand, and then which square each is in, in jobs; the squares fill up in order.
        struct Span {
            Vec3 lowest{.x = std::numeric_limits<float>::max(), .y = std::numeric_limits<float>::max()};
            Vec3 highest{.x = std::numeric_limits<float>::lowest(),
                         .y = std::numeric_limits<float>::lowest()};
        };
        std::vector<Span> spans(jobs.batchCountOf(bodies.size(), kSmallestGridBatch));
        jobs.forBatches(desc, bodies.size(), kSmallestGridBatch,
                        [&spans, bodies](std::size_t batch, std::size_t first, std::size_t last) {
                            Span& span = spans[batch];
                            for (const Body& body : bodies.subspan(first, last - first)) {
                                span.lowest = componentMin(span.lowest, body.feet);
                                span.highest = componentMax(span.highest, body.feet);
                            }
                        });
        for (const Span& span : spans) {
            lowest = componentMin(lowest, span.lowest);
            highest = componentMax(highest, span.highest);
        }
        columns = cellsAcross(highest.x - lowest.x);
        rows = cellsAcross(highest.y - lowest.y);
        room.cellOfBody.resize(bodies.size());
        jobs.forBatches(desc, bodies.size(), kSmallestGridBatch,
                        [this, &room, bodies](std::size_t /*batch*/, std::size_t first, std::size_t last) {
                            for (std::size_t body = first; body < last; ++body) {
                                room.cellOfBody[body] = narrow<std::uint32_t>(cellOf(bodies[body].feet));
                            }
                        });
        room.firstOfCell.assign((columns * rows) + 1, 0);
        for (const std::uint32_t cell : room.cellOfBody) {
            ++room.firstOfCell[std::size_t{cell} + 1];
        }
        std::partial_sum(room.firstOfCell.begin(), room.firstOfCell.end(), room.firstOfCell.begin());
        room.cellBodies.resize(bodies.size());
        room.nextOfCell.assign(room.firstOfCell.begin(), room.firstOfCell.end() - 1);
        for (std::size_t body = 0; body < bodies.size(); ++body) {
            room.cellBodies[room.nextOfCell[room.cellOfBody[body]]++] = &bodies[body];
        }
        firstOfCell = room.firstOfCell;
        cellBodies = room.cellBodies;
    }

    /// Calls `visit` with each body in the square of `point` and those around it.
    template <class Visit>
    void forEachAround(Vec3 point, const Visit& visit) const {
        const std::size_t column = indexAlong(point.x - lowest.x, columns);
        const std::size_t row = indexAlong(point.y - lowest.y, rows);
        for (std::size_t y = row > 0 ? row - 1 : 0; y <= std::min(row + 1, rows - 1); ++y) {
            for (std::size_t x = column > 0 ? column - 1 : 0; x <= std::min(column + 1, columns - 1); ++x) {
                const std::size_t cell = (y * columns) + x;
                for (std::uint32_t body = firstOfCell[cell]; body < firstOfCell[cell + 1]; ++body) {
                    visit(*cellBodies[body]);
                }
            }
        }
    }

private:
    [[nodiscard]] auto cellsAcross(float span) const noexcept -> std::size_t {
        return span > 0.0F ? std::min(static_cast<std::size_t>(std::min(
                                          span / cellSize, static_cast<float>(kMostGridCellsAcross))) +
                                          1,
                                      kMostGridCellsAcross)
                           : 1;
    }
    [[nodiscard]] auto indexAlong(float offset, std::size_t cells) const noexcept -> std::size_t {
        const float index = offset / cellSize;
        return index > 0.0F ? static_cast<std::size_t>(std::min(index, static_cast<float>(cells - 1))) : 0;
    }
    [[nodiscard]] auto cellOf(Vec3 point) const noexcept -> std::size_t {
        return (indexAlong(point.y - lowest.y, rows) * columns) + indexAlong(point.x - lowest.x, columns);
    }

    float cellSize;
    Vec3 lowest{.x = std::numeric_limits<float>::max(), .y = std::numeric_limits<float>::max()};
    Vec3 highest{.x = std::numeric_limits<float>::lowest(), .y = std::numeric_limits<float>::lowest()};
    std::size_t columns = 1;
    std::size_t rows = 1;
    std::span<const std::uint32_t> firstOfCell; ///< Of each square, and past the last.
    std::span<const Body* const> cellBodies;    ///< Square after square.
};

/// Writes to `nearby` the bodies of `bodies`, in squares as wide as `range`, within `range` across and
/// `height` up or down of `request`'s feet, but its own, the nearest first, ties by entity, at most
/// kMostSteeringBodies.
void bodiesNear(const DecisionRequest& request, const BodyGrid& bodies, float range, float height,
                std::vector<std::pair<float, const Body*>>& sorted,
                std::vector<navigation::SteeringBody>& nearby) {
    sorted.clear();
    bodies.forEachAround(request.feet, [&](const Body& body) {
        const float across = std::hypot(body.feet.x - request.feet.x, body.feet.y - request.feet.y);
        if (request.body != body.entity && across <= range &&
            std::abs(body.feet.z - request.feet.z) <= height) {
            sorted.emplace_back(across, &body);
        }
    });
    // The nearest few of however many a crowd puts about it, in order: sorting them all took as long as
    // steering.
    const auto isNearer = [](const auto& first, const auto& second) {
        return first.first != second.first ? first.first < second.first
                                           : first.second->entity < second.second->entity;
    };
    const auto kept = std::ranges::next(sorted.begin(), navigation::kMostSteeringBodies, sorted.end());
    std::ranges::nth_element(sorted, kept, isNearer);
    std::sort(sorted.begin(), kept, isNearer);
    nearby.clear();
    for (const auto& [across, body] : std::ranges::subrange(sorted.begin(), kept)) {
        nearby.push_back({.feet = body->feet, .velocity = body->velocity, .radius = body->radius});
    }
}

} // namespace

void Deciders::decide(jobs::JobSystem& jobs, std::span<const DecisionRequest> requests,
                      std::span<Decision> decisions, std::span<const Body> bodies,
                      std::span<const navigation::Gate> gates) {
    expects(decisions.size() == requests.size(), "Deciders::decide: a decision for each request");
    const std::size_t count = std::min(requests.size(), decisions.size());
    if (count == 0) {
        return;
    }
    // Under observe, the doors claimed age by the first's.
    const Seconds elapsed = requests.front().elapsed;
    expects(
        std::ranges::all_of(requests.first(count),
                            [elapsed](const DecisionRequest& request) { return request.elapsed == elapsed; }),
        "Deciders::decide: the requests of one tick, each as long after its last");
    const std::size_t batchCount = jobs.batchCountOf(count, kSmallestBatch);
    while (queries.size() < batchCount) {
        queries.emplace_back(*decidedOn);
    }
    const navigation::AgentProfile& profile = decidedOn->profile();
    const float range = profile.radius.value() * navigation::kSteeringRangeRadii;
    const float height = profile.standingHeight.value();
    const BodyGrid grid{jobs, bodies, range, gridRoom};
    const std::span<navigation::NavigationQuery> batchQueries = queries;
    // A batch decides with a query of its own, so that batches never share one.
    const auto decideBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        navigation::NavigationQuery& query = batchQueries[batch];
        std::vector<std::pair<float, const Body*>> sorted;
        std::vector<navigation::SteeringBody> nearby;
        for (std::size_t index = first; index < last; ++index) {
            const DecisionRequest& request = requests[index];
            if (request.isKnockedBack) {
                request.brain->noteKnockedBack();
            }
            Decision decision = request.brain->decide(request.feet, query, request.maxSpeed,
                                                      request.isOnGround, request.velocity, gates);
            if (length(decision.intent.direction) > 0.0F && request.maxSpeed > UnitsPerSecond{}) {
                bodiesNear(request, grid, range, height, sorted, nearby);
                request.brain->steer(decision.intent, request.velocity, request.maxSpeed, nearby, query);
            }
            // Last, so that what gets a stuck body out overrides how it steers (UpdatePathMovement). A body
            // told to wait, its way taken by other bodies, is not stuck: it moves on as they make way, nor is
            // one that backs off for a run at a gap. One knocked back, held where it is by the shots at it,
            // goes nowhere of its own doing: wiggled off its way for it, it went astray. Nor does one in the
            // air, straight down a drop say; but one stuck already stays so through the jumps that get it
            // out.
            if (request.stuck != nullptr) {
                const bool isJudged =
                    !request.isKnockedBack &&
                    (request.isOnGround || decision.intent.climb || request.stuck->isStuck());
                const bool moves = isJudged && followsCorridor(decision.behavior) &&
                                   length(decision.intent.direction) > 0.0F && !request.brain->isRunningUp();
                if (request.stuck->watch(decision.intent, request.feet, request.maxSpeed, moves,
                                         request.elapsed) == Progress::GivesUp) {
                    request.brain->giveUp(request.stuck->stuckPlace(), request.stuck->reroutes());
                }
                // Drawn away from its way, a body comes back short of the corner it reached, on the wrong
                // side of what that corner rounds: heading on from it, it walked into that and along it to
                // a dead end.
                if (request.stuck->isStuck()) {
                    request.brain->noteStuck();
                }
            }
            decisions[index] = decision;
        }
    };
    jobs.forBatches({.label = "agents.decide", .priority = jobs::Priority::Critical}, count, kSmallestBatch,
                    decideBatch);
    if (!gates.empty()) {
        assignGates(requests.first(count), decisions.first(count), gates, elapsed);
    }
}

void Deciders::assignGates(std::span<const DecisionRequest> requests, std::span<const Decision> decisions,
                           std::span<const navigation::Gate> gates, Seconds elapsed) {
    // What a decision of its body renews: past this, a body gone holds the door no more.
    constexpr Seconds kClaimLife{2.0F};
    // Till none is looked for again of a door none could open: a search that finds no way goes all over the
    // mesh.
    constexpr Seconds kNoneLife{1.0F};
    for (GateClaim& claim : gateClaims) {
        claim.left = claim.left - elapsed;
    }
    for (const DecisionRequest& request : requests) {
        if (!request.body) {
            continue;
        }
        const auto claim = std::ranges::find_if(
            gateClaims, [&request](const GateClaim& held) { return held.body == request.body; });
        if (claim != gateClaims.end()) {
            claim->left = request.brain->gateItOpens() ? kClaimLife : Seconds{};
        }
    }
    std::erase_if(gateClaims, [](const GateClaim& claim) { return claim.left <= Seconds{}; });
    // What opens a door, and the body nearest it as the crow flies of those its way there stops at.
    struct Candidate {
        float distance = std::numeric_limits<float>::max();
        const DecisionRequest* request = nullptr;
        navigation::GateOpener opener{};
    };
    std::vector<Candidate> candidates;
    navigation::NavigationQuery& query = queries.front();
    for (const navigation::Gate& gate : gates) {
        if (gate.isOpen || gate.openers.empty() ||
            std::ranges::any_of(gateClaims, [&gate](const GateClaim& claim) {
                return navigation::isSameDoor(claim.door, gate.middle);
            })) {
            continue;
        }
        candidates.clear();
        for (const navigation::GateOpener& opener : gate.openers) {
            Candidate nearest{.opener = opener};
            for (std::size_t index = 0; index < requests.size(); ++index) {
                const DecisionRequest& request = requests[index];
                if (!request.body || !decisions[index].closedGate ||
                    !navigation::isSameDoor(*decisions[index].closedGate, gate.middle)) {
                    continue;
                }
                const Vec3 middle = opener.box.center();
                const Vec3 toOpener{.x = middle.x - request.feet.x, .y = middle.y - request.feet.y};
                if (const float distance = length(toOpener); distance < nearest.distance) {
                    nearest.distance = distance;
                    nearest.request = &request;
                }
            }
            if (nearest.request != nullptr) {
                candidates.push_back(nearest);
            }
        }
        if (candidates.empty()) {
            continue;
        }
        std::ranges::sort(candidates, {}, &Candidate::distance);
        // By a way through no closed door: by this one, it would wait at it for itself.
        const auto chosen = std::ranges::find_if(candidates, [&](const Candidate& candidate) {
            return query
                .findPath({.from = candidate.request->feet,
                           .to = candidate.opener.box.center(),
                           .gates = gates,
                           .body = candidate.request->brain->bodyMotion(candidate.request->maxSpeed)})
                .has_value();
        });
        if (chosen != candidates.end()) {
            chosen->request->brain->openGate(gate.middle, chosen->opener);
            gateClaims.push_back({.door = gate.middle, .body = *chosen->request->body, .left = kClaimLife});
        } else {
            gateClaims.push_back({.door = gate.middle, .left = kNoneLife});
        }
    }
}

} // namespace nx::agents
