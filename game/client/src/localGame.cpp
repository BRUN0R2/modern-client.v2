// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.render;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// A user command each this long, the classic rate of 100 a second (docs/07, section 2.2), a tick of
/// the game's clock with each; a frame catches up on this much time at most, so that a stalled one
/// does not run away.
constexpr nx::Milliseconds kCommandInterval = nx::tickLengthOf(nx::kDefaultTickRate);
constexpr nx::Seconds kMaxCatchUp{0.25F};

[[nodiscard]] auto eyeOf(const MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

} // namespace

LocalGame::LocalGame(CreateToken /*token*/, const Level& level, nx::Vec3 eye)
    : m_level{&level}, m_player{.origin = eye - nx::Vec3{.z = kStandingViewHeight.value()},
                                .maxSpeed = level.heldWeapon().maxSpeed},
      m_previousEye{eye} {}

auto LocalGame::start(const Level& level, nx::Vec3 eye)
    -> nx::Result<std::unique_ptr<LocalGame>, LevelError> {
    auto game = std::make_unique<LocalGame>(CreateToken{}, level, eye);
    if (auto spawned = spawnGame(level.map(), level.entityClasses(), game->m_world); !spawned) {
        return std::unexpected{std::move(spawned.error())};
    }
    game->m_body =
        game->m_world.spawn(nx::game::Transform{.origin = game->m_player.origin}, bodyOf(game->m_player));
    return game;
}

auto LocalGame::play(PlayerInput& input, nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3 {
    const nx::collision::BrushHulls& hulls = m_level->hulls();
    m_pending = std::min(m_pending + elapsed, kMaxCatchUp);
    while (m_pending >= kCommandInterval) {
        const UserCommand command = input.nextCommand(view, kCommandInterval, m_player.maxSpeed);
        m_previousEye = eyeOf(m_player);
        if (command.buttons.test(Button::Use) && !m_lastButtons.test(Button::Use)) {
            // Before the command moves the player, as the original's PreThink uses.
            std::ignore = nx::game::playerUse(
                m_world, hulls, m_body, {.eye = m_previousEye, .forward = nx::angleVectors(view).forward});
        }
        m_lastButtons = command.buttons;
        const std::vector<BrushEntity> entities = brushEntitiesOf(m_world);
        const MovementResult moved = simulateMovement(m_player, command, MovementSettings{}, hulls, entities);
        m_player = moved.state;
        nx::game::touchBrushEntities(m_world, hulls, moved.touchedModels, m_body);
        m_world.get<nx::game::Transform>(m_body)->origin = m_player.origin;
        *m_world.get<nx::game::Body>(m_body) = bodyOf(m_player);
        nx::game::touchTriggers(m_world, hulls, m_body);
        nx::game::runThinks(m_world, hulls);
        nx::game::runPushers(m_world, hulls);
        m_player.origin = m_world.get<nx::game::Transform>(m_body)->origin;
        nx::game::advanceTick(m_world);
        m_pending -= kCommandInterval;
    }
    return nx::lerp(m_previousEye, eyeOf(m_player), m_pending / kCommandInterval);
}

auto LocalGame::toggleNoclip() -> bool {
    const bool isFlying = m_player.moveType == MoveType::Walk;
    m_player.moveType = isFlying ? MoveType::Noclip : MoveType::Walk;
    return isFlying;
}

auto LocalGame::placements() -> std::vector<nx::render::ModelPlacement> {
    std::vector<nx::render::ModelPlacement> placed;
    nx::ecs::Query<const nx::game::BrushModel&, const nx::game::Transform&,
                   nx::ecs::Optional<const nx::game::Waves>,
                   nx::ecs::Optional<const nx::game::AlternateTextures>>{m_world}
        .forEach([&placed](const nx::game::BrushModel& model, const nx::game::Transform& transform,
                           const nx::game::Waves* waves, const nx::game::AlternateTextures* textures) {
            placed.push_back({.model = model.index,
                              .origin = transform.origin,
                              .angles = transform.angles,
                              .waveHeight = waves != nullptr ? waves->height : 0.0F,
                              .showsAlternateTextures = textures != nullptr && textures->isShown});
        });
    return placed;
}

} // namespace cs
