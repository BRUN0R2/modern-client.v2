// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;
import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

namespace cs {
namespace {
constexpr nx::log::Category kLog{"entities"};
/// How far from the nearest player a body moves how often: near one, each tick; away from them all, in longer
/// steps that take it as far, as the original's monsters move at their thinks, a tenth of a second apart,
/// which its clients interpolate. It is what a crowd of a hundred thousand fits a tick by.
struct MoveBand {
    float reach = 0.0F;
    std::uint32_t ticksPerMove = 1;
};
constexpr std::array kMoveBands{MoveBand{.reach = 1024.0F, .ticksPerMove = 1},
                                MoveBand{.reach = 2048.0F, .ticksPerMove = 2},
                                MoveBand{.reach = 4096.0F, .ticksPerMove = 4}};
constexpr std::uint32_t kFarthestTicksPerMove = 8;

/// The least a body's keys come to ducked, about what a player's knife walk does (250 units a second, a third
/// of it): under 60, what they gain it each tick its friction takes (PM_Accelerate, PM_Friction), and a body
/// of 180 units a second or less crawled at 3.
constexpr float kLeastDuckedWalk = 80.0F;

/// `move` and `maxSpeed` of a body going ducked, its keys, when they ask a walk at all, asking as much more
/// as keeps it at kLeastDuckedWalk.
void keepDuckedWalk(MoveIntent& move, nx::UnitsPerSecond& maxSpeed) noexcept {
    const float asked = std::hypot(move.forward.value(), move.side.value(), move.up.value());
    const auto ducked = static_cast<float>(double{std::min(asked, maxSpeed.value())} * kDuckSpeedShare);
    if (ducked <= 0.0F || ducked >= kLeastDuckedWalk) {
        return;
    }
    const float raised = kLeastDuckedWalk / ducked;
    maxSpeed = maxSpeed * raised;
    move.forward = move.forward * raised;
    move.side = move.side * raised;
    move.up = move.up * raised;
}

/// How many ticks go by between the moves of a body at `origin`, the players at `watchers`; none, each tick.
[[nodiscard]] auto ticksPerMoveAt(nx::Vec3 origin, std::span<const nx::Vec3> watchers) noexcept
    -> std::uint32_t {
    if (watchers.empty()) {
        return 1;
    }
    float nearest = std::numeric_limits<float>::max();
    for (const nx::Vec3 watcher : watchers) {
        nearest = std::min(nearest, nx::lengthSquared(watcher - origin));
    }
    for (const MoveBand& band : kMoveBands) {
        if (nearest <= band.reach * band.reach) {
            return band.ticksPerMove;
        }
    }
    return kFarthestTicksPerMove;
}

/// The fewest chunks of bodies, some dozen each, it is worth handing to a worker: a move costs some
/// microseconds.
constexpr std::size_t kSmallestChunkBatch = 2;
/// The same to write what they are to a snapshot, little work each.
constexpr std::size_t kSmallestStatesBatch = 128;
struct EntityBodyData {
    const EntityCatalog* catalog = nullptr;
};

/// What an entity body's hitboxes were placed for: its sequence and frame, and where it stood, turned.
struct HitboxPose {
    nx::assets::SequenceIndex sequence{};
    float frame = 0.0F;
    nx::Vec3 origin{};
    nx::EulerAngles angles{};

    [[nodiscard]] auto operator==(const HitboxPose&) const noexcept -> bool = default;
};

/// An entity body's hitboxes as a trace last placed them on its posed bones, and the pose they were
/// placed for: a trace through the body places them again only when its pose changed, as the original's
/// studio cache keeps a pose (R_CheckStudioCache), so that the sights of many eyes through one body pose
/// it once a tick.
struct PlacedEntityHitboxes {
    std::optional<HitboxPose> pose{};
    std::vector<nx::collision::PlacedBox> boxes{};
};

[[nodiscard]] auto catalogOf(const nx::ecs::World& world) -> const EntityCatalog& {
    const EntityBodyData* data = world.resource<EntityBodyData>();
    nx::expects(data != nullptr && data->catalog != nullptr, "entities: registered body data");
    return *data->catalog;
}

void play(EntityBody& body, const EntityType& type, EntityMotion motion, nx::GameTime now,
          const EntityCatalog& catalog) {
    const EntitySequences& sequences = type.sequences;
    nx::assets::SequenceIndex sequence = sequences.idle;
    switch (motion) {
    case EntityMotion::Idle:
        sequence = body.movement.isDucked ? sequences.crouchIdle : sequences.idle;
        break;
    case EntityMotion::Walking:
        sequence = body.movement.isDucked ? sequences.crouchWalk : sequences.walk;
        break;
    case EntityMotion::Attacking:
        sequence = sequences.attack;
        break;
    case EntityMotion::Dead:
        sequence = sequences.death;
        break;
    }
    if (body.motion != motion || body.animation.sequence != sequence || motion == EntityMotion::Attacking) {
        body.motion = motion;
        body.animation.sequence = sequence;
        body.animation.frame = 0.0F;
        resetSequence(body.animation, catalog.model(type.model).files->view->sequences()[sequence.value],
                      now);
    }
}

[[nodiscard]] auto bonesOf(const nx::ecs::World& world, nx::ecs::Entity entity) -> std::vector<nx::Mat3x4> {
    const EntityBody& body = *world.get<EntityBody>(entity);
    const EntityCatalog& catalog = catalogOf(world);
    const EntityType& type = catalog.type(body.type);
    const AnimatedModel& model = catalog.model(type.model);
    const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
    const nx::assets::StudioSequence& sequence =
        model.files->view->sequences()[body.animation.sequence.value];
    return poseBones(
        skeletonOf(*model.files->view), sequence, model.animations[body.animation.sequence.value],
        {.frame = serverFrameOf(sequence, body.animation.frame),
         .placement = nx::affine(nx::rotationMatrix(transform.angles),
                                 transform.origin + modelOffsetOf(type.definition, body.movement.isDucked))});
}

/// The hitboxes of the model of entity body `entity` of `world`.
[[nodiscard]] auto hitboxesOf(const nx::ecs::World& world, nx::ecs::Entity entity)
    -> std::span<const nx::assets::Hitbox> {
    const EntityCatalog& catalog = catalogOf(world);
    return catalog.model(catalog.type(world.get<EntityBody>(entity)->type).model).files->view->hitboxes();
}

/// The hitboxes of entity body `entity` of `world` on its posed bones: as a trace last placed them,
/// placed anew when its pose changed since.
[[nodiscard]] auto placedHitboxes(nx::ecs::World& world, nx::ecs::Entity entity)
    -> std::span<const nx::collision::PlacedBox> {
    const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
    const EntityBody& body = *world.get<EntityBody>(entity);
    PlacedEntityHitboxes& placed = *world.get<PlacedEntityHitboxes>(entity);
    const HitboxPose pose{.sequence = body.animation.sequence,
                          .frame = body.animation.frame,
                          .origin = transform.origin,
                          .angles = transform.angles};
    if (placed.pose != pose) {
        placed.boxes = placedHitboxesOf(hitboxesOf(world, entity), bonesOf(world, entity));
        placed.pose = pose;
    }
    return placed.boxes;
}

[[nodiscard]] auto clipEntity(nx::ecs::World& world, nx::ecs::Entity entity, const nx::Aabb& traced,
                              nx::Vec3 start, nx::Vec3 end) -> nx::collision::Trace {
    if (traced.min != traced.max) {
        const nx::Aabb& box = world.get<nx::game::BoxSolidity>(entity)->box;
        return nx::collision::traceBox({.min = box.min - traced.max, .max = box.max - traced.min},
                                       world.get<nx::game::Transform>(entity)->origin, start, end);
    }
    const auto hitboxes = hitboxesOf(world, entity);
    const nx::collision::HitboxTrace hit =
        nx::collision::traceHitboxes(placedHitboxes(world, entity), start, end);
    nx::collision::Trace trace = hit.trace;
    if (trace.fraction < 1.0F || trace.startsSolid) {
        trace.hitGroup = hitboxes[hit.hitbox].group;
    }
    return trace;
}

[[nodiscard]] auto harmEntity(nx::ecs::World& world, nx::ecs::Entity entity, const nx::game::Attack& attack)
    -> nx::game::Harm {
    EntityBody& body = *world.get<EntityBody>(entity);
    if (body.health <= nx::HitPoints{}) {
        return {};
    }
    nx::expects(std::isfinite(attack.damage.value()) && attack.damage >= nx::Damage{},
                "entities: finite nonnegative damage");
    const EntityCatalog& catalog = catalogOf(world);
    const EntityType& type = catalog.type(body.type);
    const double scaled = static_cast<double>(attack.damage.value()) *
                          type.definition.hitGroups.of(hitGroupOf(attack.trace)).value();
    const nx::Damage dealt{static_cast<float>(scaled)};
    const nx::Damage taken{std::min(dealt.value(), body.health.value())};
    body.health = body.health - taken;
    const bool isKilled = body.health <= nx::HitPoints{};
    if (!isKilled && dealt > nx::Damage{} && type.definition.knockback) {
        // Knocked back along the way the attack went, its walking held back, then slowed (walkedBy).
        body.movement.velocity =
            pushedBy(*type.definition.knockback, body.movement.velocity, attack.direction, dealt);
        body.knockedBack.hitAt = nx::game::tickTimeOf(world).now;
    }
    if (isKilled) {
        play(body, type, EntityMotion::Dead, nx::game::tickTimeOf(world).now, catalog);
        body.movement.isDead = true;
        world.get<nx::game::Body>(entity)->isPushed = false;
        world.remove<nx::game::BoxSolidity>(entity);
        nx::log::debug(kLog, "entity {:?} {}:{} dies", type.definition.id, std::to_underlying(entity.index()),
                       std::to_underlying(entity.generation()));
    }
    return {.damage = taken, .dealt = dealt, .isKilled = isKilled};
}
} // namespace

auto registerEntityBodies(nx::ecs::World& world, const EntityCatalog& catalog)
    -> nx::Result<void, nx::ecs::ComponentError> {
    for (auto&& registered : {world.registerComponent<EntityBody>("cs::EntityBody"),
                              world.registerComponent<PlacedEntityHitboxes>("cs::PlacedEntityHitboxes")}) {
        if (!registered) {
            return std::unexpected{registered.error()};
        }
    }
    world.insertResource(EntityBodyData{.catalog = &catalog});
    return {};
}

namespace {
/// What every body's move of a tick is given, as many threads as move them reading it.
struct BodyMoveWorld {
    nx::game::TickTime time;
    const EntityCatalog& catalog;
    const nx::collision::BrushHulls& hulls;
    const nx::game::SolidBoxes& stoppers;
    std::span<const LinkedBrushEntity> brushes;
    std::span<const nx::Vec3> watchers;
};

/// What a body's tick made that is not its own to write, for after all of them have moved.
struct BodyMade {
    nx::ecs::Entity entity{};
    std::optional<EntityStep> step{};
    std::optional<nx::Vec3> strikesAlong{};
    std::vector<nx::collision::ModelIndex> touched{};
    std::optional<nx::Aabb> box{}; ///< The box it ducked or stood up into.
};

/// A tick of `body` of `entity`: asked what `commands`, by entity, ask of it, or at rest; `near` and
/// `nearBrushes` are kept for their room; adds to `made` what it made.
void moveBody(const BodyMoveWorld& moving, nx::ecs::Entity entity, std::span<const EntityCommand> commands,
              EntityBody& body, nx::game::Transform& transform, nx::game::Body& solid,
              std::vector<SolidBox>& near, std::vector<BrushEntity>& nearBrushes,
              std::vector<BodyMade>& made) {
    const nx::game::TickTime& time = moving.time;
    if (body.health <= nx::HitPoints{}) {
        advanceFrame(body.animation, time.now, time.rate);
        return;
    }
    // At a tick of its own by its slot, so that as many move each tick.
    const auto slot = std::uint64_t{std::to_underlying(entity.index())};
    if ((slot + static_cast<std::uint64_t>(time.now.tick())) % body.ticksPerMove != 0) {
        return;
    }
    advanceFrame(body.animation, time.now, time.rate);
    const EntityType& type = moving.catalog.type(body.type);
    const auto asked = std::ranges::lower_bound(commands, entity, {}, &EntityCommand::entity);
    UserCommand command =
        asked != commands.end() && asked->entity == entity
            ? asked->command
            : UserCommand{.duration = nx::tickLengthOf(time.rate), .viewAngles = transform.angles};
    // The server owns the body's time; an agent cannot accelerate it by sending a longer command. It goes as
    // far as all the ticks since its last move take it.
    const std::int64_t ticks = std::max<std::int64_t>(time.now.tick() - body.movedAt.tick() + 1, 1);
    command.duration = nx::tickLengthOf(time.rate) * static_cast<float>(ticks);
    // Knocked back, it walks no faster than its share of its speed, held back while staggered: the
    // speed it may walk at, as a weapon caps a player's (pmove->maxspeed), rather than a share of the
    // walk it asks for, which left one asked to walk slowly where it was: a body at rest loses
    // sv_stopspeed's worth to friction each tick, more than a walk under 60 units a second gains it
    // (PM_Friction, PM_Accelerate).
    body.movement.maxSpeed = type.definition.speed;
    if (type.definition.knockback) {
        const float share = walkShareOf(*type.definition.knockback, body.knockedBack, time.now, time.rate);
        if (share > 0.0F) {
            body.movement.maxSpeed = type.definition.speed * share;
        } else {
            command.move = {};
        }
    }
    if (command.buttons.test(Button::Duck) || body.movement.isDucked || body.movement.isDucking) {
        keepDuckedWalk(command.move, body.movement.maxSpeed);
    }
    const nx::Vec3 before = transform.origin;
    const bool wasDucked = body.movement.isDucked;
    body.movement.origin = before;
    boxesNear(moving.stoppers, before, entity, near);
    brushEntitiesNear(moving.brushes, before, nearBrushes);
    MovementResult moved =
        simulateMovement(body.movement, command, MovementSettings{}, moving.hulls, nearBrushes, near);
    body.movement = moved.state;
    solid = bodyOf(body.movement);
    std::optional<EntityStep> step;
    if (type.definition.stride && body.movement.isOnGround) {
        // A step each stride it walks on the ground, as its feet meet it.
        body.walked = body.walked + nx::HammerUnits{std::hypot(body.movement.origin.x - before.x,
                                                               body.movement.origin.y - before.y)};
        if (body.walked >= *type.definition.stride) {
            body.walked = nx::HammerUnits{std::fmod(body.walked.value(), type.definition.stride->value())};
            step = EntityStep{.entity = entity,
                              .position = body.movement.origin + nx::Vec3{.z = solid.box.min.z}};
        }
    }
    transform.origin = body.movement.origin;
    transform.angles = {.yaw = command.viewAngles.yaw};
    body.movedFrom = before;
    body.movedAt = time.now.ticksLater(1);
    body.ticksPerMove = ticksPerMoveAt(transform.origin, moving.watchers);
    std::optional<nx::Vec3> strikesAlong;
    if (command.buttons.test(Button::Attack) && time.now >= body.attacksAgainAt) {
        body.attacksAgainAt = time.now.after(type.definition.meleeInterval, time.rate);
        play(body, type, EntityMotion::Attacking, time.now, moving.catalog);
        strikesAlong = nx::angleVectors(command.viewAngles).forward;
    } else if (body.motion != EntityMotion::Attacking || body.animation.isFinished ||
               time.now >= body.attacksAgainAt) {
        play(body, type,
             nx::length(body.movement.velocity) > 0.0F ? EntityMotion::Walking : EntityMotion::Idle, time.now,
             moving.catalog);
    }
    const std::optional<nx::Aabb> box =
        body.movement.isDucked != wasDucked ? std::optional{solid.box} : std::nullopt;
    if (step || strikesAlong || !moved.touchedModels.empty() || box) {
        made.push_back({.entity = entity,
                        .step = step,
                        .strikesAlong = strikesAlong,
                        .touched = std::move(moved.touchedModels),
                        .box = box});
    }
}
} // namespace

auto spawnEntityBody(nx::ecs::World& world, const nx::collision::BrushHulls& hulls, EntityTypeIndex typeIndex,
                     nx::game::Transform placement) -> nx::Result<nx::ecs::Entity, EntityError> {
    return spawnEntityBody(world, hulls, nx::game::solidsOf(world, hulls), typeIndex, placement);
}

auto spawnEntityBody(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                     const nx::game::Solids& solids, EntityTypeIndex typeIndex, nx::game::Transform placement)
    -> nx::Result<nx::ecs::Entity, EntityError> {
    if (!std::isfinite(placement.origin.x) || !std::isfinite(placement.origin.y) ||
        !std::isfinite(placement.origin.z) || !std::isfinite(placement.angles.yaw.value()) ||
        placement.angles.pitch != nx::Degrees{} || placement.angles.roll != nx::Degrees{}) {
        return nx::fail(EntityError::InvalidPlacement,
                        "a finite origin and yaw, and an upright body are required");
    }
    const EntityCatalog& catalog = catalogOf(world);
    const EntityType& type = catalog.type(typeIndex);
    placement.origin =
        nx::game::droppedToFloor(world, hulls, solids, kStandingBox, placement.origin, std::nullopt);
    const nx::game::SolidHit met = nx::game::traceSolidsHit(
        world, hulls, solids, {.box = kStandingBox, .start = placement.origin, .end = placement.origin});
    if (met.trace.startsSolid || met.trace.isAllSolid) {
        return nx::fail(EntityError::InvalidPlacement, "the standing body overlaps something solid");
    }
    EntityBody body{.type = typeIndex,
                    .movement = {.origin = placement.origin, .maxSpeed = type.definition.speed},
                    .health = type.definition.health,
                    .movedFrom = placement.origin,
                    .movedAt = nx::game::tickTimeOf(world).now};
    body.animation.sequence = type.sequences.idle;
    resetSequence(body.animation,
                  catalog.model(type.model).files->view->sequences()[type.sequences.idle.value],
                  nx::game::tickTimeOf(world).now);
    const nx::ecs::Entity entity =
        world.spawn(placement, body, bodyOf(body.movement), nx::game::BoxSolidity{.box = kStandingBox},
                    nx::game::ClipMethod{.clip = &clipEntity}, PlacedEntityHitboxes{},
                    nx::game::DamageMethod{.takeAttack = &harmEntity},
                    nx::game::Classified{.classification = nx::game::Classification::Organic});
    nx::log::debug(kLog, "entity {:?} {}:{} spawns at {}", type.definition.id,
                   std::to_underlying(entity.index()), std::to_underlying(entity.generation()),
                   placement.origin);
    return entity;
}

auto runEntityBodies(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                     std::span<const EntityCommand> commands, nx::jobs::JobSystem* jobs,
                     std::span<const nx::Vec3> watchers) -> EntityBodiesTick {
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    const EntityCatalog& catalog = catalogOf(world);
    // What each is asked, by entity as a horde asks, the first asked of it if asked twice: looked for only by
    // those that move this tick.
    std::vector<EntityCommand> inOrder;
    if (!std::ranges::is_sorted(commands, {}, &EntityCommand::entity)) {
        inOrder.assign(commands.begin(), commands.end());
        std::ranges::stable_sort(inOrder, {}, &EntityCommand::entity);
    }
    const std::span<const EntityCommand> asked =
        inOrder.empty() ? commands : std::span<const EntityCommand>{inOrder};
    // What the moves meet, gathered once for the tick, as the original links its entities once and keeps
    // them linked where they move, each move given those near it (SV_AddLinksToPM): the brush entities, which
    // no body moves; and the boxes of all but these bodies, which pass through each other, as a mod's horde
    // does (its semiclip), so that a crowd of them flows through a doorway and round what they strike at
    // rather than jam: they jammed, flailed and struck each other. The players' bodies stop them. So no
    // body's move depends on another's.
    BrushEntityQuery brushQuery{world};
    std::vector<LinkedBrushEntity> brushes;
    linkedBrushEntitiesOf(brushQuery, hulls, brushes);
    nx::game::SolidBoxes stoppers;
    nx::ecs::Query<nx::ecs::Entity, const nx::game::Transform&, const nx::game::BoxSolidity&,
                   nx::ecs::Optional<const nx::game::ClipMethod>, nx::ecs::Without<EntityBody>>{world}
        .forEach([&stoppers](nx::ecs::Entity entity, const nx::game::Transform& transform,
                             const nx::game::BoxSolidity& solidity, const nx::game::ClipMethod* clip) {
            stoppers.add({.entity = entity,
                          .origin = transform.origin,
                          .box = solidity.box,
                          .clip = clip != nullptr ? *clip : nx::game::ClipMethod{}});
        });
    // The rows of a chunk of them.
    struct Part {
        std::span<const nx::ecs::Entity> entities;
        std::span<EntityBody> bodies;
        std::span<nx::game::Transform> transforms;
        std::span<nx::game::Body> solids;
    };
    std::vector<Part> parts;
    nx::ecs::Query<nx::ecs::Entity, EntityBody&, nx::game::Transform&, nx::game::Body&>{world}.forEachChunk(
        [&parts](std::span<const nx::ecs::Entity> entities, std::span<EntityBody> bodies,
                 std::span<nx::game::Transform> transforms, std::span<nx::game::Body> solids) {
            parts.push_back(
                {.entities = entities, .bodies = bodies, .transforms = transforms, .solids = solids});
        });
    const BodyMoveWorld moving{.time = time,
                               .catalog = catalog,
                               .hulls = hulls,
                               .stoppers = stoppers,
                               .brushes = brushes,
                               .watchers = watchers};
    std::vector<std::vector<BodyMade>> madeOfBatch(
        jobs != nullptr ? jobs->batchCountOf(parts.size(), kSmallestChunkBatch) : 1);
    const auto moveBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        std::vector<SolidBox> near;
        std::vector<BrushEntity> nearBrushes;
        std::vector<BodyMade>& made = madeOfBatch[batch];
        for (const Part& part : std::span{parts}.subspan(first, last - first)) {
            for (std::size_t row = 0; row < part.entities.size(); ++row) {
                moveBody(moving, part.entities[row], asked, part.bodies[row], part.transforms[row],
                         part.solids[row], near, nearBrushes, made);
            }
        }
    };
    if (jobs != nullptr) {
        jobs->forBatches({.label = "entities.move", .priority = nx::jobs::Priority::Critical}, parts.size(),
                         kSmallestChunkBatch, moveBatch);
    } else {
        moveBatch(0, 0, parts.size());
    }
    // What they made that is the world's, in the order of their entities.
    std::vector<BodyMade> made;
    for (std::vector<BodyMade>& ofBatch : madeOfBatch) {
        std::ranges::move(ofBatch, std::back_inserter(made));
    }
    std::ranges::sort(made, {}, &BodyMade::entity);
    EntityBodiesTick done;
    // Strikes pass through the bodies they pass through; gathered for the first, and again once a touch may
    // have changed what is solid.
    std::optional<std::vector<nx::game::SolidBrushEntity>> strikesMeet;
    for (const BodyMade& move : made) {
        // Killed by a touch earlier among these, it has none.
        nx::game::BoxSolidity* const solidity = world.get<nx::game::BoxSolidity>(move.entity);
        if (move.box && solidity != nullptr) {
            solidity->box = *move.box;
        }
        if (move.step) {
            done.steps.push_back(*move.step);
        }
        if (move.strikesAlong) {
            if (!strikesMeet) {
                strikesMeet = nx::game::solidBrushesOf(world, hulls);
            }
            const EntityBody& body = *world.get<EntityBody>(move.entity);
            const EntityType& type = catalog.type(body.type);
            const nx::Vec3 eye = body.movement.origin + nx::Vec3{.z = body.movement.viewHeight.value()};
            const nx::game::SolidHit hit = nx::game::traceSolidsHit(
                world, hulls, *strikesMeet, stoppers,
                {.start = eye,
                 .end = eye + (*move.strikesAlong * type.definition.meleeReach.value()),
                 .ignored = move.entity});
            if (hit.entity) {
                done.attacks.push_back({.target = *hit.entity,
                                        .attack = {.attacker = move.entity,
                                                   .damage = type.definition.meleeDamage,
                                                   .direction = *move.strikesAlong,
                                                   .trace = hit.trace}});
            }
        }
        if (!move.touched.empty()) {
            nx::game::touchBrushEntities(world, hulls, move.touched, move.entity);
            strikesMeet.reset();
        }
    }
    return done;
}

auto entityHeadTopOf(nx::ecs::World& world, nx::ecs::Entity entity) -> std::optional<nx::Vec3> {
    return headTopOf(hitboxesOf(world, entity), placedHitboxes(world, entity));
}

auto entityStatesOf(nx::ecs::World& world) -> std::vector<EntityState> {
    std::vector<EntityState> states;
    writeEntityStates(world, states, nullptr);
    return states;
}

void writeEntityStates(nx::ecs::World& world, std::vector<EntityState>& states, nx::jobs::JobSystem* jobs) {
    // The rows of a chunk of them, and where they go among all of them.
    struct Part {
        std::span<const nx::ecs::Entity> entities;
        std::span<const EntityBody> bodies;
        std::span<const nx::game::Transform> transforms;
        std::size_t first = 0;
    };
    std::vector<Part> parts;
    std::size_t count = 0;
    nx::ecs::Query<nx::ecs::Entity, const EntityBody&, const nx::game::Transform&>{world}.forEachChunk(
        [&parts, &count](std::span<const nx::ecs::Entity> entities, std::span<const EntityBody> bodies,
                         std::span<const nx::game::Transform> transforms) {
            if (!entities.empty()) {
                parts.push_back(
                    {.entities = entities, .bodies = bodies, .transforms = transforms, .first = count});
                count += entities.size();
            }
        });
    states.resize(count);
    // Rows keep the order of their spawning but for what took a dead one's place, which is seldom.
    std::atomic_bool isInOrder{true};
    const auto writeBatch = [&parts, &states, &isInOrder](std::size_t /*batch*/, std::size_t first,
                                                          std::size_t last) {
        for (const Part& part : std::span{parts}.subspan(first, last - first)) {
            if (!std::ranges::is_sorted(part.entities)) {
                isInOrder.store(false, std::memory_order_relaxed);
            }
            for (std::size_t row = 0; row < part.entities.size(); ++row) {
                const EntityBody& body = part.bodies[row];
                states[part.first + row] = {.entity = part.entities[row],
                                            .type = body.type,
                                            .origin = part.transforms[row].origin,
                                            .velocity = body.movement.velocity,
                                            .angles = part.transforms[row].angles,
                                            .animation = body.animation,
                                            .health = body.health,
                                            .movedFrom = body.movedFrom,
                                            .movedAt = body.movedAt,
                                            .ticksPerMove = body.ticksPerMove,
                                            .isDucked = body.movement.isDucked};
            }
        }
    };
    if (jobs != nullptr) {
        jobs->forBatches({.label = "entities.states", .priority = nx::jobs::Priority::Critical}, parts.size(),
                         kSmallestStatesBatch, writeBatch);
    } else {
        writeBatch(0, 0, parts.size());
    }
    if (!isInOrder.load(std::memory_order_relaxed) ||
        !std::ranges::all_of(parts | std::views::pairwise, [](const auto& neighbours) {
            return std::get<0>(neighbours).entities.back() < std::get<1>(neighbours).entities.front();
        })) {
        std::ranges::sort(states, {}, &EntityState::entity);
    }
}
} // namespace cs
