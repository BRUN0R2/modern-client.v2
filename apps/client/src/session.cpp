// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.console;
import nx.settings;
import cs.shared;
import cs.server;
import cs.client;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

/// The seeds of the streams the temporary entities and the particles of each map's client draw their
/// chances from.
constexpr std::uint64_t kTempEntitySeed = 0;
constexpr std::uint64_t kParticleSeed = 0;
constexpr std::uint64_t kBloodSeed = 0;

} // namespace

/// Whether the game draws a weapon given heavier than the one held, as `_cl_autowepswitch` says.
auto autoSwitchOf(const nx::settings::PlayerCvars& player) -> cs::AutoSwitch {
    return player.autoWeaponSwitch.value() ? cs::AutoSwitch::ToHeavier : cs::AutoSwitch::Off;
}

/// The map `name` of `content`, its game started by the rules `cvars` set, the client's player joined
/// in the team of its first spawn point, asking for the name `player` holds and drawing its weapons as
/// `player` says, and its client's level read; meanwhile the workers of `jobs` build the map's mesh of
/// `navigation` the agents walk, unless the cache keeps it and `kept` reads it, which a map plays without
/// when it cannot be built. Errors: CommandFailed, with the cause.
auto startSession(const cs::GameContent& content, std::string_view name,
                  const nx::settings::PlayerCvars& player, const cs::GameCvars& cvars,
                  nx::jobs::JobSystem& jobs,
                  const nx::Result<nx::game::NavigationData, nx::console::ConsoleError>& navigation,
                  KeptMesh kept) -> nx::Result<std::unique_ptr<Session>, nx::console::ConsoleError> {
    const std::string failed = std::format("map {}", name);
    auto map = cs::GameMap::load(content.vfs, name);
    if (!map) {
        return nx::fail(nx::console::ConsoleError::CommandFailed, failed, std::move(map.error()));
    }
    auto game = cs::Game::start(content, **map, cvars);
    if (!game) {
        return nx::fail(nx::console::ConsoleError::CommandFailed, failed, std::move(game.error()));
    }
    const cs::PlayerIndex joined =
        (*game)->join(cs::spawnTeam((*map)->map()), autoSwitchOf(player), player.name.value());
    auto agentMesh = startAgentMesh(jobs, **game, (*map)->name(), navigation, kept);
    auto level = cs::Level::load(content, **map); // while the workers build the mesh
    if (!level) {
        return nx::fail(nx::console::ConsoleError::CommandFailed, failed, std::move(level.error()));
    }
    auto mesh = finishAgentMesh(std::move(agentMesh));
    if (!mesh) {
        nx::log::warning(kLog, "{} plays without bots and zombies: {}", failed, nx::describe(mesh.error()));
    }
    cs::ClientGame known{(*level)->models(), (*level)->players(), (*game)->snapshotFor(joined)};
    return std::make_unique<Session>(Session{.map = std::move(*map),
                                             .level = std::move(*level),
                                             .game = std::move(*game),
                                             .player = joined,
                                             .client = std::move(known),
                                             .effects = cs::TempEntities{kTempEntitySeed},
                                             .particles = cs::Particles{kParticleSeed},
                                             .blood = cs::BloodEffects{kBloodSeed},
                                             .agentMesh = std::move(mesh)});
}

} // namespace app
