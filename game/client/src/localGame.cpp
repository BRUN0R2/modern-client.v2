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
/// The stream of random numbers a spawn's weapons draw from.
constexpr std::string_view kSpawnStream = "spawn";
/// How far a bullet goes: FireBullets3's distance for every gun the game has.
constexpr nx::HammerUnits kBulletReach{8192.0F};

[[nodiscard]] auto eyeOf(const MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

} // namespace

LocalGame::LocalGame(CreateToken /*token*/, const GameContent& content, const Level& level, nx::Vec3 eye)
    : m_level{&level}, m_player{.origin = eye - nx::Vec3{.z = kStandingViewHeight.value()}},
      m_inventory{content.weapons}, m_previousEye{eye} {}

auto LocalGame::start(const GameContent& content, const Level& level, Team team, nx::Vec3 eye,
                      AutoSwitch autoSwitch) -> nx::Result<std::unique_ptr<LocalGame>, LevelError> {
    auto game = std::make_unique<LocalGame>(CreateToken{}, content, level, eye);
    if (auto spawned = spawnGame(level.map(), level.entityClasses(), game->m_world); !spawned) {
        return std::unexpected{std::move(spawned.error())};
    }
    game->m_body =
        game->m_world.spawn(nx::game::Transform{.origin = game->m_player.origin}, bodyOf(game->m_player));
    if (const Loadout* const loadout = content.loadouts.find(team)) {
        const nx::game::TickTime& time = nx::game::tickTimeOf(game->m_world);
        nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
        for (const std::string& weapon : loadout->weapons) {
            // readLoadouts checked that the catalog has each.
            std::ignore = game->m_inventory.give(*content.weapons.indexOf(weapon), autoSwitch, time, random);
        }
        for (const LoadoutAmmo& ammo : loadout->ammo) {
            std::ignore = game->m_inventory.giveAmmo(ammo.id, ammo.rounds);
        }
    }
    game->m_player.maxSpeed = game->m_inventory.maxSpeed();
    return game;
}

void LocalGame::give(WeaponIndex weapon, AutoSwitch autoSwitch) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    std::ignore = m_inventory.give(weapon, autoSwitch, time, random);
    m_player.maxSpeed = m_inventory.maxSpeed();
}

auto LocalGame::play(PlayerInput& input, nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3 {
    m_pending = std::min(m_pending + elapsed, kMaxCatchUp);
    return run([&input, view, this] { return input.nextCommand(view, kCommandInterval, m_player.maxSpeed); });
}

auto LocalGame::wait(nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3 {
    m_pending += elapsed;
    return run([view] { return UserCommand{.duration = kCommandInterval, .viewAngles = view}; });
}

auto LocalGame::viewModel() const -> std::optional<ViewModelPose> {
    const std::optional<ViewAnimation>& animation = m_inventory.animation();
    if (!animation) {
        return std::nullopt;
    }
    return ViewModelPose{
        .weapon = animation->weapon,
        .sequence = animation->sequence,
        .start = animation->start,
        .intoIt =
            std::max(shownTime().since(animation->start, nx::game::tickTimeOf(m_world).rate), nx::Seconds{})};
}

auto LocalGame::hearing() const -> Hearing {
    return Hearing{.from = shownTime(), .rate = nx::game::tickTimeOf(m_world).rate};
}

auto LocalGame::shownTime() const -> nx::GameTime {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    return time.now.after(m_pending - kCommandInterval, time.rate);
}

auto LocalGame::run(const std::function<UserCommand()>& nextCommand) -> nx::Vec3 {
    while (m_pending >= kCommandInterval) {
        UserCommand command = nextCommand();
        m_lastCommand = CommandNumber{m_lastCommand.value + 1};
        command.number = m_lastCommand;
        tick(command);
        m_pending -= kCommandInterval;
    }
    return nx::lerp(m_previousEye, eyeOf(m_player), m_pending / kCommandInterval);
}

void LocalGame::tick(const UserCommand& command) {
    const nx::collision::BrushHulls& hulls = m_level->hulls();
    m_previousEye = eyeOf(m_player);
    // The weapon the command selects is drawn before it moves the player, at the speed it allows, as
    // the original's CmdStart selects it.
    m_inventory.select(command, nx::game::tickTimeOf(m_world));
    m_player.maxSpeed = m_inventory.maxSpeed();
    if (command.buttons.test(Button::Use) && !m_lastButtons.test(Button::Use)) {
        // Before the command moves the player, as the original's PreThink uses.
        std::ignore = nx::game::playerUse(
            m_world, hulls, m_body,
            {.eye = m_previousEye, .forward = nx::angleVectors(command.viewAngles).forward});
    }
    m_lastButtons = command.buttons;
    const std::vector<BrushEntity> entities = brushEntitiesOf(m_world);
    const MovementResult moved = simulateMovement(m_player, command, MovementSettings{}, hulls, entities);
    m_player = moved.state;
    nx::game::touchBrushEntities(m_world, hulls, moved.touchedModels, m_body);
    m_world.get<nx::game::Transform>(m_body)->origin = m_player.origin;
    *m_world.get<nx::game::Body>(m_body) = bodyOf(m_player);
    nx::game::touchTriggers(m_world, hulls, m_body);
    // The weapon in hand fires, is reloaded or idles once the command moved the player, as the
    // original's PostThink runs it; each shot goes as far as a bullet does, and where it meets the
    // world or a brush entity it leaves its mark (its damage and how far it goes through come with
    // damage).
    Shooter shooter{
        .eye = eyeOf(m_player),
        .view = command.viewAngles,
        .punch = m_player.punch,
        .horizontalSpeed = nx::UnitsPerSecond{static_cast<float>(std::sqrt(double{
            (m_player.velocity.x * m_player.velocity.x) + (m_player.velocity.y * m_player.velocity.y)}))},
        .isOnGround = m_player.isOnGround,
        .isDucked = m_player.isDucked};
    const WeaponTracer tracer = [this, &hulls](nx::collision::Hull hull, nx::Vec3 start, nx::Vec3 end) {
        return nx::game::traceSolids(m_world, hulls, hull, start, end);
    };
    for (const Shot& shot : m_inventory.frame(command, shooter, tracer, nx::game::tickTimeOf(m_world))) {
        // As FireBullets3 traces it: its direction as it strays, not of length 1.
        const nx::game::SolidHit hit =
            nx::game::traceSolidsHit(m_world, hulls, nx::collision::Hull::Point, shot.origin,
                                     shot.origin + (shot.direction * kBulletReach.value()));
        if (hit.trace.fraction < 1.0F && !hit.trace.startsSolid) {
            m_impacts.push_back(Impact{.position = hit.trace.end, .on = decalTargetOf(m_world, hit.entity)});
        }
    }
    m_player.punch = shooter.punch;
    nx::game::runThinks(m_world, hulls);
    nx::game::runPushers(m_world, hulls);
    m_player.origin = m_world.get<nx::game::Transform>(m_body)->origin;
    nx::game::advanceTick(m_world);
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
                              .waveHeight = waves != nullptr ? waves->height : nx::HammerUnits{},
                              .showsAlternateTextures = textures != nullptr && textures->isShown});
        });
    return placed;
}

} // namespace cs
