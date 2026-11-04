// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module mod.zombies;
import std;
import nx.core;
import nx.math;
import nx.data;
import nx.ecs;
import nx.assets;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

namespace zombies {
namespace {
constexpr nx::log::Category kLog{"zombies"};
constexpr float kHalfTurnDegrees = 180.0F;
constexpr float kFullTurnDegrees = kHalfTurnDegrees * 2.0F;
/// The stream of random numbers a stuck zombie's wiggles and jumps draw from, of its body.
constexpr std::string_view kStuckStream = "zombie stuck";
/// How far a body's slot is shifted from its generation in the seed of its stuck watch's draws.
constexpr int kGenerationBits = 32;
/// The stream of random numbers how far off a zombie places what it hears draws from, of its body and the
/// tick.
constexpr std::string_view kHearingStream = "zombie hearing";
/// The fewest bodies it is worth handing to a worker, each batch some tens of microseconds of work: to write
/// what they ask, to copy them, to find whose turn it is, to hold what they decided, and to perceive.
constexpr std::size_t kSmallestAskedBatch = 4096;
constexpr std::size_t kSmallestCopiedBatch = 8192;
constexpr std::size_t kSmallestAgentChunks = 128;
constexpr std::size_t kSmallestTurnBatch = 512;
constexpr std::size_t kSmallestHeldBatch = 128;
constexpr std::size_t kSmallestPerceivedBatch = 64;
/// How far from the nearest player a zombie thinks at each of its turns, past that at each second, fourth and
/// eighth: what it does there it can do later, and the first reach is past the longest it hears
/// (hearing.shot).
struct ThinkBand {
    float reach = 0.0F;
    std::uint32_t turnsPerThink = 1;
};
constexpr std::array kThinkBands{ThinkBand{.reach = 2048.0F, .turnsPerThink = 1},
                                 ThinkBand{.reach = 4096.0F, .turnsPerThink = 2},
                                 ThinkBand{.reach = 8192.0F, .turnsPerThink = 4}};
constexpr std::uint32_t kFarthestTurnsPerThink = 8;

/// How many of its turns go by between the thoughts of a zombie at `origin`, the players at `targets`; none,
/// each.
[[nodiscard]] auto turnsPerThinkAt(nx::Vec3 origin, std::span<const cs::PerceptionTarget> targets) noexcept
    -> std::uint32_t {
    if (targets.empty()) {
        return 1;
    }
    float nearest = std::numeric_limits<float>::max();
    for (const cs::PerceptionTarget& target : targets) {
        nearest = std::min(nearest, nx::lengthSquared(target.origin - origin));
    }
    for (const ThinkBand& band : kThinkBands) {
        if (nearest <= band.reach * band.reach) {
            return band.turnsPerThink;
        }
    }
    return kFarthestTurnsPerThink;
}

/// What a batch wrote at the start of its own part of a list.
struct WrittenPart {
    std::size_t first = 0;
    std::size_t count = 0;
};
/// Closes up the `parts` written of `list`, in order; how many they are.
template <class Element>
[[nodiscard]] auto closedUp(std::span<Element> list, std::span<const WrittenPart> parts) -> std::size_t {
    auto end = list.begin();
    for (const WrittenPart& part : parts) {
        const auto from = list.begin() + static_cast<std::ptrdiff_t>(part.first);
        if (from != end) {
            std::ranges::move(from, from + static_cast<std::ptrdiff_t>(part.count), end);
        }
        end += static_cast<std::ptrdiff_t>(part.count);
    }
    return static_cast<std::size_t>(end - list.begin());
}
/// How many ticks of `rate` go by between a body's turns of `interval`: at least one.
[[nodiscard]] auto ticksPerThinkOf(nx::Seconds interval, nx::TickRate rate) noexcept -> std::uint64_t {
    return std::max<std::uint64_t>(
        static_cast<std::uint64_t>(std::llround(interval / nx::tickLengthOf(rate))), 1);
}
/// The seed of the draws of `entity`'s body: its slot above its generation.
[[nodiscard]] auto seedOf(nx::ecs::Entity entity) noexcept -> std::uint64_t {
    return (std::uint64_t{std::to_underlying(entity.index())} << kGenerationBits) |
           std::to_underlying(entity.generation());
}
[[nodiscard]] auto feetOf(nx::Vec3 origin) -> nx::Vec3 {
    return origin + nx::Vec3{.z = cs::kStandingBox.min.z};
}
[[nodiscard]] auto distanceSquaredAcross(nx::Vec3 first, nx::Vec3 second) -> double {
    const double x = double{first.x} - double{second.x};
    const double y = double{first.y} - double{second.y};
    return (x * x) + (y * y);
}
struct SpawnAnchor {
    nx::Vec3 feet{};
    nx::Degrees yaw{};
};
[[nodiscard]] auto distanceSquared(nx::Vec3 first, nx::Vec3 second) -> double {
    const double z = double{first.z} - double{second.z};
    return distanceSquaredAcross(first, second) + (z * z);
}
[[nodiscard]] auto nearestAnchor(std::span<const SpawnAnchor> base, nx::Vec3 point) -> const SpawnAnchor& {
    return *std::ranges::min_element(base, {}, [point](const SpawnAnchor& anchor) -> double {
        return distanceSquared(anchor.feet, point);
    });
}
[[nodiscard]] auto spawnCandidates(const nx::navigation::NavigationMesh& mesh,
                                   std::span<const SpawnAnchor> base) -> std::vector<nx::Vec3> {
    auto outline = mesh.outline();
    std::vector<nx::Vec3> candidates = std::move(outline.corners);
    candidates.reserve(candidates.size() + outline.polygons.size() + base.size());
    for (const auto& polygon : outline.polygons) {
        const auto corners = std::span{candidates}.subspan(polygon.firstCorner, polygon.cornerCount);
        double x = 0;
        double y = 0;
        double z = 0;
        for (const nx::Vec3 corner : corners) {
            x += double{corner.x};
            y += double{corner.y};
            z += double{corner.z};
        }
        const auto count = static_cast<double>(corners.size());
        candidates.push_back({.x = static_cast<float>(x / count),
                              .y = static_cast<float>(y / count),
                              .z = static_cast<float>(z / count)});
    }
    for (const SpawnAnchor& anchor : base) {
        candidates.push_back(anchor.feet);
    }
    std::ranges::sort(candidates, [](nx::Vec3 first, nx::Vec3 second) -> bool {
        return std::tie(first.x, first.y, first.z) < std::tie(second.x, second.y, second.z);
    });
    const auto duplicates = std::ranges::unique(candidates);
    candidates.erase(duplicates.begin(), duplicates.end());
    std::ranges::stable_sort(candidates, {}, [base](nx::Vec3 point) -> double {
        return distanceSquared(point, nearestAnchor(base, point).feet);
    });
    return candidates;
}
[[nodiscard]] auto rulesOf(const Settings& settings) -> nx::agents::Rules {
    return {.memoryLifetime = settings.memoryLifetime,
            .attackDistance = settings.body.meleeReach,
            .arrivalDistance = settings.arrivalDistance,
            .replanDistance = settings.replanDistance,
            .replanShare = settings.replanShare,
            .shortTermMemory = settings.shortTermMemory,
            .threatRecency = settings.threatRecency,
            .threatMargin = settings.threatMargin,
            .memoryLimit = settings.memoryLimit,
            .braking = settings.braking,
            .stopSpeed = settings.stopSpeed,
            .acceleration = settings.acceleration};
}
} // namespace

namespace {
/// `read`, the settings of the file `source`, if they break no budget. Errors: InvalidData.
[[nodiscard]] auto validated(nx::Result<Settings, nx::data::DataError> read, std::string_view source)
    -> nx::Result<Settings, ZombieError> {
    if (!read) {
        return nx::fail(ZombieError::InvalidData, std::move(read.error()));
    }
    const auto positive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    if (!positive(read->thinkInterval.count()) || !positive(read->sightRange.value()) ||
        !positive(read->fieldOfView.value()) || read->fieldOfView.value() > kFullTurnDegrees ||
        !positive(read->memoryLifetime.count()) || !positive(read->arrivalDistance.value()) ||
        !positive(read->replanDistance.value()) || !std::isfinite(read->replanShare) ||
        read->replanShare < 0.0F || !std::isfinite(read->braking) || read->braking < 0.0F ||
        !std::isfinite(read->stopSpeed.value()) || read->stopSpeed < nx::UnitsPerSecond{} ||
        !std::isfinite(read->acceleration) || read->acceleration < 0.0F ||
        !std::isfinite(read->shortTermMemory.count()) || read->shortTermMemory < nx::Seconds{} ||
        !std::isfinite(read->threatRecency.count()) || read->threatRecency < nx::Seconds{} ||
        !std::isfinite(read->threatMargin.value()) || read->threatMargin < nx::HammerUnits{} ||
        !positive(read->spawnRadius.value()) || !positive(read->spawnSpacing.value()) ||
        read->memoryLimit == 0 || read->perceptionBudget == 0 || read->spawnBatchLimit == 0) {
        return nx::fail(
            ZombieError::InvalidData,
            std::format(
                "{}: positive perception, memory and spawn budgets and an FOV at most 360 are required",
                source));
    }
    if (!cs::isValid(read->hearing)) {
        return nx::fail(ZombieError::InvalidData,
                        std::format("{}: each of hearing's ranges more than none and its error share none or "
                                    "more are required",
                                    source));
    }
    if (!nx::agents::isValid(read->stuck)) {
        return nx::fail(
            ZombieError::InvalidData,
            std::format("{}: stuck's interval, samples, speed, distance and give-up time more than "
                        "none, its first jump none or more, and each range's shortest more than "
                        "none, its longest no shorter, are required",
                        source));
    }
    return std::move(*read);
}
} // namespace

auto readSettings(std::string_view text, std::string_view source) -> nx::Result<Settings, ZombieError> {
    return validated(nx::data::parseToml<Settings>(text, source), source);
}

auto readSettingsOver(const Settings& base, std::string_view text, std::string_view source)
    -> nx::Result<Settings, ZombieError> {
    return validated(nx::data::parseTomlOver<Settings>(base, text, source), source);
}

Horde::Horde(CreateToken /*token*/, cs::EntityTypeIndex entityType, Settings rules,
             const nx::navigation::NavigationMesh& mesh, nx::jobs::JobSystem& jobSystem, cs::Game& game)
    : type{entityType}, settings{std::move(rules)}, jobs{&jobSystem}, query{mesh}, deciders{mesh},
      owner{&game} {}

Horde::~Horde() {
    const auto cleared = clear();
    nx::invariant(cleared.has_value(),
                  "Horde: owned bodies and their borrowed corridors are destroyed while the mesh is there");
}

auto Horde::create(cs::Game& game, const nx::navigation::NavigationMesh& mesh, nx::jobs::JobSystem& jobs,
                   cs::EntityTypeIndex type, Settings settings)
    -> nx::Result<std::unique_ptr<Horde>, ZombieError> {
    if (auto registered = game.registerEntityComponent<nx::agents::Agent>("nx::agents::Agent"); !registered) {
        return nx::fail(ZombieError::SpawnFailed, std::move(registered.error()));
    }
    nx::log::info(kLog, "zombies walk {} polygons", mesh.polygonCount());
    return std::make_unique<Horde>(CreateToken{}, type, std::move(settings), mesh, jobs, game);
}

auto Horde::spawn(std::span<const cs::SpawnPoint> base, std::uint32_t count)
    -> nx::Result<void, ZombieError> {
    cs::Game& game = *owner;
    if (count == 0 || count > settings.spawnBatchLimit) {
        return nx::fail(ZombieError::SpawnFailed,
                        std::format("count must be from 1 to {}", settings.spawnBatchLimit));
    }
    if (base.empty()) {
        return nx::fail(ZombieError::SpawnFailed, "the selected base has no map spawn points");
    }
    std::vector<SpawnAnchor> anchors;
    anchors.reserve(base.size());
    for (const cs::SpawnPoint& spawn : base) {
        if (!std::isfinite(spawn.origin.x) || !std::isfinite(spawn.origin.y) ||
            !std::isfinite(spawn.origin.z) || !std::isfinite(spawn.yaw.value())) {
            return nx::fail(ZombieError::SpawnFailed, "finite base origins and yaws are required");
        }
        auto feet = query.nearestPoint(feetOf(spawn.origin));
        if (!feet) {
            return nx::fail(ZombieError::SpawnFailed, "a base spawn point is not near walkable ground",
                            std::move(feet.error()));
        }
        anchors.push_back({.feet = *feet, .yaw = spawn.yaw});
    }
    std::vector<nx::ecs::Entity> born;
    std::vector<nx::Vec3> occupied;
    for (const auto& body : game.entities()) {
        if (body.health > nx::HitPoints{}) {
            occupied.push_back(body.origin);
        }
    }
    const auto rollback = [&game, &born] -> void {
        for (const nx::ecs::Entity entity : born) {
            const auto removed = game.removeEntity(entity);
            nx::invariant(removed.has_value(), "Horde: rollback owns every newly created body");
        }
    };
    const double radius = double{settings.spawnRadius.value()};
    const double spacing = double{settings.spawnSpacing.value()};
    for (const nx::Vec3 candidate : spawnCandidates(query.mesh(), anchors)) {
        auto point = query.nearestPoint(candidate);
        if (!point) {
            rollback();
            return nx::fail(ZombieError::SpawnFailed, std::move(point.error()));
        }
        const SpawnAnchor& anchor = nearestAnchor(anchors, *point);
        if (distanceSquared(*point, anchor.feet) > radius * radius ||
            std::ranges::any_of(occupied, [&](nx::Vec3 other) -> bool {
                return distanceSquaredAcross(*point, other) < spacing * spacing;
            })) {
            continue;
        }
        auto reachable = query.isReachable(anchor.feet, *point);
        if (!reachable) {
            rollback();
            return nx::fail(ZombieError::SpawnFailed, std::move(reachable.error()));
        }
        if (!*reachable) {
            continue;
        }
        auto made = game.createEntity(
            type, {.origin = *point - nx::Vec3{.z = cs::kStandingBox.min.z}, .angles = {.yaw = anchor.yaw}});
        if (!made) {
            if (made.error().code == cs::EntityError::InvalidPlacement) {
                continue;
            }
            rollback();
            return nx::fail(ZombieError::SpawnFailed, std::move(made.error()));
        }
        born.push_back(*made);
        occupied.push_back(*point);
        if (born.size() == count) {
            break;
        }
    }
    if (born.size() != count) {
        rollback();
        return nx::fail(
            ZombieError::SpawnFailed,
            std::format("only {} of {} clear reachable positions were found; the batch was rolled back",
                        born.size(), count));
    }
    adopt(born);
    return {};
}

void Horde::adopt(std::span<const nx::ecs::Entity> bodies) {
    cs::Game& game = *owner;
    const nx::game::TickTime time = game.time();
    const cs::EntityStates states = game.entities();
    for (const nx::ecs::Entity entity : bodies) {
        const std::uint64_t seed = seedOf(entity);
        auto attached = game.attachEntityComponent(
            entity, nx::agents::Agent{
                        .brain = nx::agents::Brain{rulesOf(settings)},
                        .stuck = nx::agents::StuckWatch{settings.stuck, nx::Random{seed, kStuckStream}}});
        nx::invariant(attached.has_value(), "Horde: a newly created body accepts its brain");
        // Until its first turn it stands as it was made.
        const auto state = std::ranges::lower_bound(states, entity, {}, &cs::EntityState::entity);
        nx::invariant(state != states.end() && state->entity == entity, "Horde: a body made already");
        members.push_back(
            {.entity = entity,
             .command = {.duration = nx::tickLengthOf(time.rate), .viewAngles = state->angles}});
    }
    std::ranges::sort(members, {}, &Member::entity);
    for (const Member& member : members) {
        agentOfSlot.resize(
            std::max(agentOfSlot.size(), std::size_t{std::to_underlying(member.entity.index())} + 1));
    }
    // At a tick of its own by its slot, so that as many think each tick.
    const std::uint64_t ticksPerThink = ticksPerThinkOf(settings.thinkInterval, time.rate);
    membersOfTurn.assign(ticksPerThink, {});
    for (std::size_t index = 0; index < members.size(); ++index) {
        const std::uint64_t slot = std::to_underlying(members[index].entity.index());
        membersOfTurn[(ticksPerThink - (slot % ticksPerThink)) % ticksPerThink].push_back(
            nx::narrow<std::uint32_t>(index));
    }
}

auto Horde::commands() -> std::span<const cs::EntityCommand> {
    if (members.empty()) {
        return {};
    }
    cs::Game& game = *owner;
    const cs::EntityStates bodies = game.entities();
    const std::vector<cs::PerceptionTarget> targets = game.perceptionTargets();
    const nx::game::TickTime time = game.time();
    const std::uint64_t ticksPerThink = ticksPerThinkOf(settings.thinkInterval, time.rate);
    // The noises of the think interval it ends: each body's turn comes once in it, and hears each once.
    const std::vector<cs::GameNoise> noisesNow = game.noises();
    if (noisesOfTick.size() >= ticksPerThink) {
        noisesHeard.erase(noisesHeard.begin(), noisesHeard.begin() + noisesOfTick.front());
        noisesOfTick.erase(noisesOfTick.begin());
    }
    noisesHeard.insert(noisesHeard.end(), noisesNow.begin(), noisesNow.end());
    noisesOfTick.push_back(nx::narrow<std::uint32_t>(noisesNow.size()));
    findTurns(bodies, targets, static_cast<std::uint64_t>(time.now.tick()),
              nx::tickLengthOf(time.rate) * static_cast<float>(ticksPerThink));
    if (!turns.empty()) {
        // Nothing moves while the horde perceives: what is solid to its sights is gathered once.
        const std::vector<nx::game::SolidBrushEntity> solids = game.sightSolids();
        perceive(targets, solids);
        writeSteeredAround(bodies);
        // Nor does a decision depend on another's.
        decisions.resize(requests.size());
        deciders.decide(*jobs, requests, decisions, steeredAround);
        holdDecisions();
    }
    writeAsked(bodies);
    return asked;
}

void Horde::findTurns(const cs::EntityStates& bodies, std::span<const cs::PerceptionTarget> targets,
                      std::uint64_t tick, nx::Seconds elapsed) {
    cs::Game& game = *owner;
    const std::span<const std::uint32_t> due = membersOfTurn[tick % membersOfTurn.size()];
    // A body's agent by its slot: found once for all of them, as they stay where they are while the horde
    // thinks.
    struct Part {
        std::span<const nx::ecs::Entity> entities;
        std::span<nx::agents::Agent> agents;
    };
    std::vector<Part> parts;
    game.forEachEntityComponentChunk<nx::agents::Agent>(
        [&parts](std::span<const nx::ecs::Entity> entities, std::span<nx::agents::Agent> agents) {
            parts.push_back({.entities = entities, .agents = agents});
        });
    jobs->forBatches({.label = "zombies.agents", .priority = nx::jobs::Priority::Critical}, parts.size(),
                     kSmallestAgentChunks, [&](std::size_t /*batch*/, std::size_t first, std::size_t last) {
                         for (const Part& part : std::span{parts}.subspan(first, last - first)) {
                             for (std::size_t row = 0; row < part.entities.size(); ++row) {
                                 const auto slot =
                                     std::size_t{std::to_underlying(part.entities[row].index())};
                                 if (slot < agentOfSlot.size()) {
                                     agentOfSlot[slot] = &part.agents[row];
                                 }
                             }
                         }
                     });
    // Each batch writes those alive at the start of its own part, and the parts close up after.
    turns.resize(due.size());
    requests.resize(due.size());
    std::vector<WrittenPart> written(jobs->batchCountOf(due.size(), kSmallestTurnBatch));
    const auto findBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        std::size_t count = 0;
        for (const std::uint32_t index : due.subspan(first, last - first)) {
            Member& member = members[index];
            const auto body = std::ranges::lower_bound(bodies, member.entity, {}, &cs::EntityState::entity);
            if (body == bodies.end() || body->entity != member.entity || body->health <= nx::HitPoints{}) {
                continue;
            }
            const std::uint32_t turnsPerThink = turnsPerThinkAt(body->origin, targets);
            if (++member.turnsWaited < turnsPerThink) {
                continue;
            }
            member.turnsWaited = 0;
            nx::agents::Agent* const agent = agentOfSlot[std::to_underlying(member.entity.index())];
            nx::invariant(agent != nullptr, "Horde: an attached brain");
            turns[first + count] = {.member = &member,
                                    .body = &*body,
                                    .agent = agent,
                                    .eye = body->origin + nx::Vec3{.z = cs::kStandingViewHeight.value()}};
            requests[first + count] = {.brain = &agent->brain,
                                       .feet = feetOf(body->origin),
                                       .velocity = body->velocity,
                                       .maxSpeed = settings.body.speed,
                                       .body = member.entity,
                                       .stuck = &agent->stuck,
                                       .elapsed = elapsed * static_cast<float>(turnsPerThink)};
            ++count;
        }
        written[batch] = {.first = first, .count = count};
    };
    jobs->forBatches({.label = "zombies.turns", .priority = nx::jobs::Priority::Critical}, due.size(),
                     kSmallestTurnBatch, findBatch);
    turns.resize(closedUp(std::span{turns}, written));
    requests.resize(closedUp(std::span{requests}, written));
}

void Horde::perceive(std::span<const cs::PerceptionTarget> targets,
                     std::span<const nx::game::SolidBrushEntity> solids) {
    cs::Game& game = *owner;
    const nx::game::TickTime time = game.time();
    const float minimumDot =
        std::cos(settings.fieldOfView.value() * std::numbers::pi_v<float> / kFullTurnDegrees);
    // The players there still are, living: a zombie forgets the others (nx::agents::Brain::forgetAbsent).
    std::vector<nx::ecs::Entity> present =
        targets | std::views::transform(&cs::PerceptionTarget::entity) | std::ranges::to<std::vector>();
    std::ranges::sort(present);
    // What a body perceives does not depend on what another perceives or decides: in batches in jobs, each
    // with a query of its own.
    while (queries.size() < jobs->batchCountOf(turns.size(), kSmallestPerceivedBatch)) {
        queries.emplace_back(query.mesh());
    }
    const auto perceiveBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        std::vector<nx::agents::Observation> seen;
        for (std::size_t index = first; index < last; ++index) {
            const Turn& turn = turns[index];
            const nx::ecs::Entity entity = turn.member->entity;
            const nx::Vec3 eye = turn.eye;
            nx::agents::Agent& agent = *turn.agent;
            seen.clear();
            const nx::Vec3 forward = nx::angleVectors(turn.body->angles).forward;
            std::optional<nx::assets::LeafSet>
                leaves; // what its eye may see, found once a candidate needs it
            const std::size_t budget = std::min(targets.size(), std::size_t{settings.perceptionBudget});
            for (std::size_t checked = 0; checked < budget; ++checked) {
                const cs::PerceptionTarget& candidate =
                    targets[(agent.nextCandidate + checked) % targets.size()];
                const nx::Vec3 difference = candidate.eye - eye;
                const float distance = nx::length(difference);
                if (distance > settings.sightRange.value() ||
                    (distance > 0.0F && nx::dot(forward, difference / distance) < minimumDot)) {
                    continue;
                }
                if (!leaves) {
                    leaves = game.leavesSeenFrom(eye);
                }
                if (game.isInLeaves(*leaves, candidate.eye) && game.seesSpot(solids, eye, candidate.eye)) {
                    seen.push_back(
                        {.entity = candidate.entity, .position = candidate.feet, .aimAt = candidate.eye});
                }
            }
            agent.nextCandidate = targets.empty() ? 0 : (agent.nextCandidate + budget) % targets.size();
            agent.brain.forgetAbsent(present);
            agent.brain.perceive(seen, time.now, time.rate);
            // A zombie plays in no team: it hears every player.
            nx::Random hearing{seedOf(entity) ^ std::bit_cast<std::uint64_t>(time.now.tick()),
                               kHearingStream};
            agent.brain.sense(
                cs::heardBy({.ear = eye}, noisesHeard, settings.hearing, queries[batch], hearing), time.now,
                time.rate);
            // Felt where they are, last: the players its body touches.
            agent.brain.sense(
                cs::touchedBy({.entity = entity, .origin = turn.body->origin, .box = cs::kStandingBox},
                              targets),
                time.now, time.rate);
        }
    };
    jobs->forBatches({.label = "zombies.perceive", .priority = nx::jobs::Priority::Critical}, turns.size(),
                     kSmallestPerceivedBatch, perceiveBatch);
}

void Horde::holdDecisions() {
    // A body whose behavior changed, and from what, for the log after them.
    struct Transition {
        nx::ecs::Entity entity{};
        nx::agents::Behavior from{};
        nx::agents::Behavior to{};
        std::optional<nx::navigation::NavigationError> failure{};
    };
    std::vector<std::vector<Transition>> transitions(jobs->batchCountOf(turns.size(), kSmallestHeldBatch));
    const auto holdBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        for (std::size_t index = first; index < last; ++index) {
            const Turn& turn = turns[index];
            const nx::agents::Decision& decision = decisions[index];
            Member& member = *turn.member;
            nx::agents::Agent& agent = *turn.agent;
            if (agent.behavior != decision.behavior) {
                transitions[batch].push_back({.entity = member.entity,
                                              .from = agent.behavior,
                                              .to = decision.behavior,
                                              .failure = decision.navigationError});
                agent.behavior = decision.behavior;
            }
            cs::UserCommand command{.duration = member.command.duration, .viewAngles = turn.body->angles};
            // It faces the way it goes, steered or wiggling, as its model walks ahead, and is driven the way
            // its velocity must change (nx::agents::Intent::drive), ahead and to its side: faced the way it
            // is driven, it turned its back on its way as it slowed down, and lost sight of what it chased.
            member.walks = !decision.intent.attacks && nx::length(decision.intent.direction) > 0.0F;
            nx::Vec3 aim = decision.intent.direction;
            if (decision.intent.attacks && decision.intent.lookAt) {
                aim = *decision.intent.lookAt - turn.eye;
                command.buttons.set(cs::Button::Attack);
            }
            if (decision.intent.jumps) {
                command.buttons.set(cs::Button::Jump);
            }
            if (nx::length(aim) > 0.0F) {
                command.viewAngles.yaw =
                    nx::Degrees{std::atan2(aim.y, aim.x) * kHalfTurnDegrees / std::numbers::pi_v<float>};
                command.viewAngles.pitch = nx::Degrees{-std::atan2(aim.z, std::hypot(aim.x, aim.y)) *
                                                       kHalfTurnDegrees / std::numbers::pi_v<float>};
            }
            member.command = command;
            if (member.walks) {
                const nx::AngleVectors axes =
                    nx::angleVectors(nx::EulerAngles{.yaw = command.viewAngles.yaw});
                member.steered = decision.intent.steered;
                member.way = decision.intent.direction;
                member.forward = axes.forward;
                member.right = axes.right;
                member.speed = settings.body.speed * decision.intent.speedShare;
            }
        }
    };
    jobs->forBatches({.label = "zombies.hold", .priority = nx::jobs::Priority::Critical}, turns.size(),
                     kSmallestHeldBatch, holdBatch);
    for (const Transition& transition : transitions | std::views::join) {
        if (transitionLogs.allow()) {
            const std::string failure =
                transition.failure ? std::format(", {}", nx::enumName(*transition.failure)) : std::string{};
            nx::log::info(kLog, "agent {}:{}: {} -> {}{} ({} transitions suppressed)",
                          std::to_underlying(transition.entity.index()),
                          std::to_underlying(transition.entity.generation()), nx::enumName(transition.from),
                          nx::enumName(transition.to), failure, transitionLogs.takeSuppressed());
        }
    }
}

void Horde::writeSteeredAround(const cs::EntityStates& bodies) {
    const std::vector<cs::MovingBody> players = owner->movingPlayers();
    steeredAround.resize(players.size() + bodies.size());
    std::ranges::transform(players, steeredAround.begin(), [](const cs::MovingBody& moving) {
        return nx::agents::Body{.entity = moving.entity,
                                .feet = moving.feet,
                                .velocity = moving.velocity,
                                .radius = moving.radius};
    });
    // Each batch writes those alive at the start of its own part, and the parts close up after.
    const std::span<nx::agents::Body> parts = std::span{steeredAround}.subspan(players.size());
    std::vector<WrittenPart> written(jobs->batchCountOf(bodies.size(), kSmallestCopiedBatch));
    const auto writeBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        std::size_t count = 0;
        for (const cs::EntityState& body : bodies.all().subspan(first, last - first)) {
            if (body.health > nx::HitPoints{}) {
                parts[first + count++] = {.entity = body.entity,
                                          .feet = feetOf(body.origin),
                                          .velocity = body.velocity,
                                          .radius = nx::HammerUnits{cs::kStandingBox.max.x}};
            }
        }
        written[batch] = {.first = first, .count = count};
    };
    jobs->forBatches({.label = "zombies.steeredAround", .priority = nx::jobs::Priority::Critical},
                     bodies.size(), kSmallestCopiedBatch, writeBatch);
    steeredAround.resize(players.size() + closedUp(parts, written));
}

void Horde::writeAsked(const cs::EntityStates& bodies) {
    asked.resize(members.size());
    const auto writeBatch = [&](std::size_t /*batch*/, std::size_t first, std::size_t last) {
        if (first == last) {
            return;
        }
        auto body = std::ranges::lower_bound(bodies, members[first].entity, {}, &cs::EntityState::entity);
        for (std::size_t index = first; index < last; ++index) {
            const Member& member = members[index];
            cs::UserCommand command = member.command;
            if (member.walks) {
                while (body != bodies.end() && body->entity < member.entity) {
                    ++body;
                }
                const bool isThere = body != bodies.end() && body->entity == member.entity;
                const nx::Vec3 driven =
                    member.steered && isThere
                        ? nx::agents::driveToward(*member.steered, body->velocity).value_or(member.way)
                        : member.way;
                command.move.forward = member.speed * nx::dot(driven, member.forward);
                command.move.side = member.speed * nx::dot(driven, member.right);
            }
            asked[index] = {.entity = member.entity, .command = command};
        }
    };
    jobs->forBatches({.label = "zombies.asked", .priority = nx::jobs::Priority::Critical}, members.size(),
                     kSmallestAskedBatch, writeBatch);
}

auto Horde::clear() -> nx::Result<void, ZombieError> {
    cs::Game& game = *owner;
    for (const Member& member : members) {
        if (auto removed = game.removeEntity(member.entity); !removed) {
            // A body removed by another native consumer already satisfies clear's postcondition.
            if (removed.error().code != cs::EntityError::UnknownEntity) {
                return nx::fail(ZombieError::SpawnFailed, std::move(removed.error()));
            }
        }
    }
    members.clear();
    membersOfTurn.clear();
    asked.clear();
    return {};
}
} // namespace zombies
