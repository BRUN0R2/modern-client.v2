// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

namespace cs {
namespace {
constexpr nx::log::Category kLog{"entities"};
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
        sequence = sequences.idle;
        break;
    case EntityMotion::Walking:
        sequence = sequences.walk;
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
    return poseBones(skeletonOf(*model.files->view), sequence,
                     model.animations[body.animation.sequence.value],
                     {.frame = serverFrameOf(sequence, body.animation.frame),
                      .placement = nx::affine(nx::rotationMatrix(transform.angles),
                                              transform.origin + type.definition.modelOffset.vector())});
}

[[nodiscard]] auto clipEntity(nx::ecs::World& world, nx::ecs::Entity entity, const nx::Aabb& traced,
                              nx::Vec3 start, nx::Vec3 end) -> nx::collision::Trace {
    const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
    if (traced.min != traced.max) {
        return nx::collision::traceBox(
            {.min = kStandingBox.min - traced.max, .max = kStandingBox.max - traced.min}, transform.origin,
            start, end);
    }
    const EntityCatalog& catalog = catalogOf(world);
    const EntityBody& body = *world.get<EntityBody>(entity);
    const auto hitboxes = catalog.model(catalog.type(body.type).model).files->view->hitboxes();
    PlacedEntityHitboxes& placed = *world.get<PlacedEntityHitboxes>(entity);
    const HitboxPose pose{.sequence = body.animation.sequence,
                          .frame = body.animation.frame,
                          .origin = transform.origin,
                          .angles = transform.angles};
    if (placed.pose != pose) {
        placed.boxes = placedHitboxesOf(hitboxes, bonesOf(world, entity));
        placed.pose = pose;
    }
    const nx::collision::HitboxTrace hit = nx::collision::traceHitboxes(placed.boxes, start, end);
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
        nx::log::info(kLog, "entity {:?} {}:{} dies", type.definition.id, std::to_underlying(entity.index()),
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

auto spawnEntityBody(nx::ecs::World& world, const nx::collision::BrushHulls& hulls, EntityTypeIndex typeIndex,
                     nx::game::Transform placement) -> nx::Result<nx::ecs::Entity, EntityError> {
    if (!std::isfinite(placement.origin.x) || !std::isfinite(placement.origin.y) ||
        !std::isfinite(placement.origin.z) || !std::isfinite(placement.angles.yaw.value()) ||
        placement.angles.pitch != nx::Degrees{} || placement.angles.roll != nx::Degrees{}) {
        return nx::fail(EntityError::InvalidPlacement,
                        "a finite origin and yaw, and an upright body are required");
    }
    const EntityCatalog& catalog = catalogOf(world);
    const EntityType& type = catalog.type(typeIndex);
    placement.origin = nx::game::droppedToFloor(world, hulls, kStandingBox, placement.origin, std::nullopt);
    const nx::game::SolidHit met = nx::game::traceSolidsHit(
        world, hulls, {.box = kStandingBox, .start = placement.origin, .end = placement.origin});
    if (met.trace.startsSolid || met.trace.isAllSolid) {
        return nx::fail(EntityError::InvalidPlacement, "the standing body overlaps something solid");
    }
    EntityBody body{.type = typeIndex,
                    .movement = {.origin = placement.origin, .maxSpeed = type.definition.speed},
                    .health = type.definition.health};
    body.animation.sequence = type.sequences.idle;
    resetSequence(body.animation,
                  catalog.model(type.model).files->view->sequences()[type.sequences.idle.value],
                  nx::game::tickTimeOf(world).now);
    const nx::ecs::Entity entity =
        world.spawn(placement, body, bodyOf(body.movement), nx::game::BoxSolidity{.box = kStandingBox},
                    nx::game::ClipMethod{.clip = &clipEntity}, PlacedEntityHitboxes{},
                    nx::game::DamageMethod{.takeAttack = &harmEntity},
                    nx::game::Classified{.classification = nx::game::Classification::Organic});
    nx::log::info(kLog, "entity {:?} {}:{} spawns at {}", type.definition.id,
                  std::to_underlying(entity.index()), std::to_underlying(entity.generation()),
                  placement.origin);
    return entity;
}

auto runEntityBodies(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                     std::span<const EntityCommand> commands) -> std::vector<EntityAttack> {
    std::vector<nx::ecs::Entity> entities;
    nx::ecs::Query<nx::ecs::Entity, const EntityBody&>{world}.forEach(
        [&entities](nx::ecs::Entity entity, const EntityBody&) { entities.push_back(entity); });
    std::ranges::sort(entities);
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    const EntityCatalog& catalog = catalogOf(world);
    std::vector<const EntityCommand*> requests;
    requests.reserve(commands.size());
    for (const EntityCommand& command : commands) {
        requests.push_back(&command);
    }
    std::ranges::stable_sort(requests, {}, &EntityCommand::entity);
    // What the moves meet, gathered once for the tick, as the original links its entities once and keeps
    // them linked where they move: the brush entities, which no body moves; and the boxes, by entity, each
    // body's kept where its move left it, each move given those near it (SV_AddLinksToPM).
    const std::vector<BrushEntity> brushes = brushEntitiesOf(world);
    std::vector<SolidBox> boxes = solidBoxesOf(world);
    std::ranges::sort(boxes, {}, &SolidBox::entity);
    std::vector<SolidBox> near;
    std::vector<EntityAttack> attacks;
    for (const nx::ecs::Entity entity : entities) {
        EntityBody& body = *world.get<EntityBody>(entity);
        advanceFrame(body.animation, time.now, time.rate);
        if (body.health <= nx::HitPoints{}) {
            continue;
        }
        const EntityType& type = catalog.type(body.type);
        UserCommand command{.duration = nx::tickLengthOf(time.rate),
                            .viewAngles = world.get<nx::game::Transform>(entity)->angles};
        const auto requested = std::ranges::lower_bound(requests, entity, {}, &EntityCommand::entity);
        if (requested != requests.end() && (*requested)->entity == entity) {
            command = (*requested)->command;
        }
        // The server owns the body's time; an agent cannot accelerate it by sending a longer command.
        command.duration = nx::tickLengthOf(time.rate);
        command.buttons.reset(Button::Duck);
        if (type.definition.knockback) {
            command.move =
                walkedBy(*type.definition.knockback, body.knockedBack, command.move, time.now, time.rate);
        }
        body.movement.origin = world.get<nx::game::Transform>(entity)->origin;
        boxesNear(boxes, body.movement.origin, entity, near);
        const MovementResult moved =
            simulateMovement(body.movement, command, MovementSettings{}, hulls, brushes, near);
        body.movement = moved.state;
        nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        transform.origin = body.movement.origin;
        if (const auto own = std::ranges::lower_bound(boxes, entity, {}, &SolidBox::entity);
            own != boxes.end() && own->entity == entity) {
            own->origin = body.movement.origin;
        }
        transform.angles = {.yaw = command.viewAngles.yaw};
        *world.get<nx::game::Body>(entity) = bodyOf(body.movement);
        const bool strikes = command.buttons.test(Button::Attack) && time.now >= body.attacksAgainAt;
        if (strikes) {
            body.attacksAgainAt = time.now.after(type.definition.meleeInterval, time.rate);
            play(body, type, EntityMotion::Attacking, time.now, catalog);
            const nx::Vec3 eye = body.movement.origin + nx::Vec3{.z = body.movement.viewHeight.value()};
            const nx::Vec3 direction = nx::angleVectors(command.viewAngles).forward;
            const nx::game::SolidHit hit =
                nx::game::traceSolidsHit(world, hulls,
                                         {.start = eye,
                                          .end = eye + direction * type.definition.meleeReach.value(),
                                          .ignored = entity});
            if (hit.entity) {
                attacks.push_back({.target = *hit.entity,
                                   .attack = {.attacker = entity,
                                              .damage = type.definition.meleeDamage,
                                              .direction = direction,
                                              .trace = hit.trace}});
            }
        } else if (body.motion != EntityMotion::Attacking || body.animation.isFinished ||
                   time.now >= body.attacksAgainAt) {
            play(body, type,
                 nx::length(body.movement.velocity) > 0.0F ? EntityMotion::Walking : EntityMotion::Idle,
                 time.now, catalog);
        }
        // Touch after releasing every borrowed component reference: opening a door may change an
        // archetype, and the next tick reacquires the body through its generational handle.
        nx::game::touchBrushEntities(world, hulls, moved.touchedModels, entity);
    }
    return attacks;
}

auto entityStatesOf(nx::ecs::World& world) -> std::vector<EntityState> {
    std::vector<EntityState> states;
    nx::ecs::Query<nx::ecs::Entity, const EntityBody&, const nx::game::Transform&>{world}.forEach(
        [&states](nx::ecs::Entity entity, const EntityBody& body, const nx::game::Transform& transform) {
            states.push_back({.entity = entity,
                              .type = body.type,
                              .origin = transform.origin,
                              .velocity = body.movement.velocity,
                              .angles = transform.angles,
                              .animation = body.animation,
                              .health = body.health});
        });
    std::ranges::sort(states, {}, &EntityState::entity);
    return states;
}
} // namespace cs
