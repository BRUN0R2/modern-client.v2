// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.collision;
import nx.ecs;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers the sounds of a command's steps draw from.
constexpr std::string_view kFootstepStream = "footsteps";
constexpr std::string_view kBreathStream = "breath";
/// How much less slow a player slowed down by a hit is each command it starts on the ground (PreThink).
constexpr double kSpeedShareRecovery = 0.01;

/// Whether what a trace met in `world`, `entity`, is the world (none) or a brush entity (IsBSPModel).
[[nodiscard]] auto isBrushEntityOrWorld(const nx::ecs::World& world, std::optional<nx::ecs::Entity> entity)
    -> bool {
    return !entity || world.get<nx::game::BrushModel>(*entity) != nullptr;
}

} // namespace

auto boxesMetBy(GameState& game, const Player& mover) -> std::vector<SolidBox> {
    std::vector<SolidBox> near;
    boxesNear(solidBoxesOf(game.world, mover.body), mover.movement.origin, mover.body, near);
    return near;
}

void use(GameState& game, Player& player, const UserCommand& command) {
    const std::optional<nx::ecs::Entity> used = nx::game::playerUse(
        game.world, game.map.hulls(), player.body,
        {.eye = eyeOf(player.movement), .forward = nx::angleVectors(command.viewAngles).forward});
    makeSound(game, player, used ? game.content.use.found : game.content.use.nothing);
}

void hearMovement(GameState& game, Player& player, std::span<const MovementSound> sounds,
                  std::span<const BrushEntity> entities, const UserCommand& command) {
    const nx::GameTime now = nx::game::tickTimeOf(game.world).now;
    nx::Random random{command.number.value, kFootstepStream};
    for (const MovementSound& sound : sounds) {
        if (std::holds_alternative<Splash>(sound)) {
            makeSound(game, player, game.content.footsteps.splash);
            continue;
        }
        if (!game.cvars.doStepsSound()) {
            continue;
        }
        const Footstep& step = std::get<Footstep>(sound);
        const TextureMaterial ground =
            step.ground ? groundMaterial(game, *step.ground, entities) : TextureMaterial::Concrete;
        if (const std::optional<SoundMade> made = player.stepVoice.soundOf(step, ground, now, random)) {
            player.sounds.push_back(*made);
        }
    }
}

auto groundMaterial(const GameState& game, const StepGround& ground, std::span<const BrushEntity> entities)
    -> TextureMaterial {
    const nx::Vec3 below = ground.origin - nx::Vec3{.z = kStepTextureDepth.value()};
    if (std::holds_alternative<BoxSolid>(ground.ground)) {
        return TextureMaterial::Concrete; // a box has no model, so no texture (PM_TraceTexture)
    }
    const std::optional<nx::collision::ModelIndex> model = modelOf(ground.ground);
    if (!model) {
        return game.map.materialAlong(game.content.materials, nx::assets::kWorldModel, ground.origin, below);
    }
    const auto entity = std::ranges::find(entities, *model, &BrushEntity::model);
    if (entity == entities.end()) {
        return TextureMaterial::Concrete; // what it stood on is gone: the original finds no texture
    }
    // Into the space of the entity's model, as TraceTexture turns the segment.
    const auto inModelSpace = [&entity](nx::Vec3 point) {
        const nx::Vec3 moved = point - entity->origin;
        return entity->angles == nx::EulerAngles{} ? moved
                                                   : nx::intoAxes(nx::angleVectors(entity->angles), moved);
    };
    return game.map.materialAlong(game.content.materials, entity->model, inModelSpace(ground.origin),
                                  inModelSpace(below));
}

auto materialOfHit(const GameState& game, const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end)
    -> std::optional<TextureMaterial> {
    if (hit.isBody) {
        return TextureMaterial::Flesh;
    }
    if (!hit.entity) {
        return game.map.materialAlong(game.content.materials, nx::assets::kWorldModel, start, end);
    }
    const nx::game::BrushModel* const model = game.world.get<nx::game::BrushModel>(*hit.entity);
    const nx::game::Transform* const transform = game.world.get<nx::game::Transform>(*hit.entity);
    if (model == nullptr || transform == nullptr) {
        return std::nullopt; // no texture is named on what is not a brush model
    }
    // Into the space of the entity's model, as TraceTexture turns the segment.
    const auto inModelSpace = [transform](nx::Vec3 point) {
        const nx::Vec3 moved = point - transform->origin;
        return transform->angles == nx::EulerAngles{}
                   ? moved
                   : nx::intoAxes(nx::angleVectors(transform->angles), moved);
    };
    return game.map.materialAlong(game.content.materials, model->index, inModelSpace(start),
                                  inModelSpace(end));
}

void preThink(GameState& game, Player& player, const UserCommand& command) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    if (player.status.isAlive()) {
        nx::Random breathRandom{command.number.value, kBreathStream};
        for (const BreathEvent& event :
             player.breath.breathe(player.movement.waterLevel, player.movement.waterType,
                                   player.movement.moveType, player.timedHarms, time, breathRandom)) {
            if (const auto* const sound = std::get_if<SoundMade>(&event)) {
                player.sounds.push_back(*sound);
            } else {
                hurt(game, player, std::get<PlayerHarm>(event));
            }
        }
    }
    if (player.movement.isOnGround) {
        // Slowed down by a hit, a hundredth less slow each command, in double as the original adds it.
        if (player.speedShare < 1.0F) {
            player.speedShare =
                static_cast<float>(static_cast<double>(player.speedShare) + kSpeedShareRecovery);
            player.movement.velocity = player.movement.velocity * player.speedShare;
        }
        player.speedShare = std::min(player.speedShare, 1.0F);
    }
    if (player.status.isAlive()) {
        // Jump: the button going down on the ground, out of deep water and of a jump out of water.
        const MovementState& movement = player.movement;
        if (command.buttons.test(Button::Jump) && !player.lastButtons.test(Button::Jump) &&
            movement.isOnGround && movement.waterLevel < WaterLevel::Waist &&
            movement.waterJumpTime <= nx::Milliseconds{}) {
            animate(game, player, PlayerAction::Jump);
        }
        // Duck.
        if (command.buttons.test(Button::Duck)) {
            animate(game, player, PlayerAction::Walk);
        }
    } else if (!player.animation.torso.isFinished) {
        // PlayerDeathThink: its death plays on.
        advanceFrame(player.animation.torso, time.now, time.rate);
    }
    // Dead or not, as the original's PreThink looks at the kept harms before it weighs a death; the dead
    // are given back none.
    healPlayer(player.status,
               player.timedHarms.check(game.content.playerHarm.drowning.givenBack, time.now, time.rate));
}

void postThink(GameState& game, Player& player, const UserCommand& command, const MovementState& before) {
    const nx::collision::BrushHulls& hulls = game.map.hulls();
    const MovementState& movement = player.movement;
    // Each shot goes as far as a bullet does, and where it meets the world or a brush entity it leaves
    // its mark.
    Shooter shooter{
        .eye = eyeOf(movement),
        .view = command.viewAngles,
        .punch = movement.punch,
        .horizontalSpeed = nx::UnitsPerSecond{static_cast<float>(std::sqrt(double{
            (movement.velocity.x * movement.velocity.x) + (movement.velocity.y * movement.velocity.y)}))},
        .isOnGround = movement.isOnGround,
        .isDucked = movement.isDucked,
        .canAttack = !game.rounds.isFrozen()};
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    // What the attacks meet: what is solid but the player itself, the world, the brush entities, the
    // hostages and the other players; a body is an entity the original's Classify makes other than none
    // (a player, a hostage, a vehicle).
    const WeaponTracer tracer = [&game, &hulls, &player](nx::collision::Hull hull, nx::Vec3 start,
                                                         nx::Vec3 end) {
        nx::game::ServerTrace way = nx::game::hullTrace(hull, start, end);
        way.ignored = player.body;
        const nx::game::SolidHit solid = nx::game::traceSolidsHit(game.world, hulls, way);
        const nx::game::Classified* const classified =
            solid.entity ? game.world.get<nx::game::Classified>(*solid.entity) : nullptr;
        const PlayerBody* const body = solid.entity ? game.world.get<PlayerBody>(*solid.entity) : nullptr;
        return WeaponHit{.trace = solid.trace,
                         .entity = solid.entity,
                         .isBrushModel = isBrushEntityOrWorld(game.world, solid.entity),
                         .isBody = classified != nullptr &&
                                   classified->classification != nx::game::Classification::None,
                         .playerAngles = body != nullptr ? std::optional{body->pose.angles} : std::nullopt};
    };
    // ItemPostFrame: unless the player is held back, the use key going down uses what is in front
    // (ImpulseCommands), before the weapon's frame.
    if (command.buttons.test(Button::Use) && !player.lastButtons.test(Button::Use) &&
        !player.inventory.isHeldBack()) {
        use(game, player, command);
    }
    const WeaponAttacks attacks = player.inventory.frame(command, shooter, tracer, time);
    // As its weapon's code asks the player's model to play it.
    if (attacks.motions.test(HolderMotion::Attacked)) {
        animate(game, player, PlayerAction::Attack1);
    }
    if (attacks.motions.test(HolderMotion::Reloaded)) {
        animate(game, player, PlayerAction::Reload);
    }
    for (const Blow& blow : attacks.blows) {
        attackEntity(game, player, blow.entity,
                     nx::game::Attack{.attacker = player.body,
                                      .damage = blow.damage,
                                      .direction = blow.direction,
                                      .trace = blow.trace});
    }
    const HitMaterial materialOf = [&game](const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) {
        return materialOfHit(game, hit, start, end);
    };
    for (const Shot& shot : attacks.shots) {
        // What its round meets, as FireBullets3 follows it: a mark on the world or a brush entity
        // (DecalGunshot), none on a hostage; and its damage to what takes it.
        for (const BulletHit& hit : followRound(shot, tracer, materialOf)) {
            if (hit.isBrushModel) {
                game.events.impacts.push_back(
                    Impact{.position = hit.trace.end, .on = stampTargetOf(game.world, hit.entity)});
            }
            if (hit.entity) {
                attackEntity(game, player, *hit.entity,
                             nx::game::Attack{.attacker = player.body,
                                              .damage = hit.damage,
                                              .direction = hit.direction,
                                              .trace = hit.trace});
            }
        }
    }
    player.movement.punch = shooter.punch;
    if (const std::optional<nx::Damage> damage =
            landingDamageOf(game.content.playerHarm.fall, before, player.movement)) {
        land(game, player, *damage);
    }
}

void runCommand(GameState& game, Player& player, const UserCommand& command) {
    const nx::collision::BrushHulls& hulls = game.map.hulls();
    // SV_RunCmd: where the player looks, and the seed of what it does.
    player.view = command.viewAngles;
    player.randomSeed = command.number;
    // The weapon the command selects is drawn before it moves the player, at the speed it allows, as
    // the original's CmdStart selects it.
    player.inventory.select(command, nx::game::tickTimeOf(game.world));
    player.movement.maxSpeed = maxSpeed(game, player);
    preThink(game, player, command);
    // After PreThink, which may kill it, as the original's returns then, and its PostThink skips the dead.
    const bool isAlive = player.status.isAlive();
    const MovementState before = player.movement;
    const std::vector<BrushEntity> entities = brushEntitiesOf(game.world);
    const MovementResult moved = simulateMovement(player.movement, command, MovementSettings{}, hulls,
                                                  entities, boxesMetBy(game, player));
    player.movement = moved.state;
    // A dead player's body is not solid (SOLID_NOT): it touches nothing.
    if (isAlive) {
        hearMovement(game, player, moved.sounds, entities, command);
        nx::game::touchBrushEntities(game.world, hulls, moved.touchedModels, player.body);
    }
    placeBody(game, player);
    if (isAlive) {
        nx::game::touchTriggers(game.world, hulls, player.body);
        touchHurtVolumes(game, player);
        // SV_RunCmd's PostThink is of a player still alive after what it touched.
        if (player.status.isAlive()) {
            postThink(game, player, command, before);
        }
    }
    // As the original's PostThink ends, dead or alive (m_afButtonLast, StudioProcessGait).
    player.lastButtons = command.buttons;
    animateAfterCommand(game, player, frameTimeOf(command));
    poseBody(game, player);
}

} // namespace cs
