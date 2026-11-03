// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.game;
import cs.shared;

namespace cs {

namespace {

constexpr std::string_view kAnimationStream = "player animation";
/// The number PostThink compares a player's gait sequence with, ACT_FLY's.
constexpr std::uint32_t kFlyActivityNumber = 5;

} // namespace

auto bodyBoxOf(const Player& player) noexcept -> std::optional<nx::Aabb> {
    return player.status.isAlive() ? std::optional{player.movement.isDucked ? kDuckedBox : kStandingBox}
                                   : std::nullopt;
}

void placeBody(GameState& game, Player& player) {
    game.world.get<nx::game::Transform>(player.body)->origin = player.movement.origin;
    *game.world.get<nx::game::Body>(player.body) = bodyOf(player.movement);
    setBodySolidity(game.world, player.body, bodyBoxOf(player));
}

void poseBody(GameState& game, Player& player) {
    const PlayerAnimation& animation = player.animation;
    game.world.get<PlayerBody>(player.body)->pose =
        PlayerPose{.model = game.models.characterModel(player.character),
                   .sequence = animation.torso.sequence,
                   .frame = animation.torso.frame,
                   .gaitSequence = animation.gaitSequence,
                   .gaitYaw = animation.gait.yaw,
                   .yawBlend = animation.gait.yawBlend,
                   .pitchBlend = static_cast<float>(animation.gait.pitchBlend),
                   .angles = bodyAnglesOf(player.view)};
}

void animate(GameState& game, Player& player, PlayerAction action) {
    const PlayedModel* const model = game.models.characterModel(player.character);
    if (model == nullptr) {
        return;
    }
    const OwnedWeapon* const held = player.inventory.held();
    const MovementState& movement = player.movement;
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    nx::Random random{player.randomSeed.value, kAnimationStream};
    setPlayerAnimation(
        player.animation, action,
        PlayerAnimationContext{
            .model = model->view,
            .weaponAnimation =
                held != nullptr
                    ? std::string_view{game.content.weapons.weapon(held->weapon).models.playerAnimation}
                    : std::string_view{},
            .isDucked = movement.isDucked,
            .isOnGround = movement.isOnGround,
            .waterLevel = movement.waterLevel,
            .speed = nx::UnitsPerSecond{std::hypot(movement.velocity.x, movement.velocity.y)},
            .lastHitGroup = player.lastHitGroup,
            .now = time.now,
            .rate = time.rate},
        random);
}

void animateAfterCommand(GameState& game, Player& player, nx::Seconds frameTime) {
    const PlayedModel* const model = game.models.characterModel(player.character);
    if (model == nullptr) {
        return;
    }
    if (player.status.isAlive()) {
        const MovementState& movement = player.movement;
        if (movement.velocity.x != 0.0F || movement.velocity.y != 0.0F) {
            if (movement.isOnGround || movement.waterLevel > WaterLevel::Feet) {
                animate(game, player, PlayerAction::Walk);
            }
        } else if (player.animation.gaitSequence.value != kFlyActivityNumber) {
            animate(game, player, PlayerAction::Idle);
        }
        const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
        advanceFrame(player.animation.torso, time.now, time.rate);
    }
    processGait(player.animation.gait, *model->view, player.animation.gaitSequence, player.movement.origin,
                bodyAnglesOf(player.view), frameTime, player.animation.torso.framerate);
}

} // namespace cs
