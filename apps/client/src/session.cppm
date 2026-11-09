// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A map being played, as a listen server plays it, and how it starts.
export module app.client:session;

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
import cs.bots;
import mod.zombies;
import :agentMeshes;

namespace app {

/// A map being played, as a listen server plays it (docs/01-architecture.md, section 3): the map as the
/// game plays it; the level the client draws of it; the game its server plays of it, and the player the
/// client plays in it; the game as the client knows it, its temporary entities and particles, and what its
/// view model shows of the frame played; and the test zombies and the bots in the game, once asked for. Each
/// goes before what it points into.
struct Session {
    std::unique_ptr<cs::GameMap> map;
    std::unique_ptr<cs::Level> level;
    std::unique_ptr<cs::Game> game;
    cs::PlayerIndex player;
    cs::ClientGame client;
    cs::TempEntities effects;
    cs::Particles particles;
    cs::BloodEffects blood; ///< The enhanced look's.
    cs::ViewModelEvents viewModelEvents{};
    std::optional<cs::ViewModelPose> viewPose{}; ///< Of the frame played.
    std::vector<cs::PassedEvent> clientEvents{}; ///< Its view model passed in the frame.
    /// The map's mesh of cs::agentProfile, built as it loaded, which the horde and the bots walk:
    /// they go before it. Else why it could not be built.
    nx::Result<nx::navigation::NavigationMesh, nx::console::ConsoleError> agentMesh;
    /// What nav_draw draws of agentMesh, made as it is first drawn, its mesh then kept by the scene: it
    /// goes before the mesh.
    std::optional<cs::NavigationDrawing> navigationDrawing{};
    std::unique_ptr<zombies::Horde> horde{};
    std::unique_ptr<cs::Bots> bots{};
};

/// The session a command acts on; none without one.
using PlayedSession = std::function<Session*()>;

/// Whether the game draws a weapon given heavier than the one held, as `_cl_autowepswitch` says.
[[nodiscard]] auto autoSwitchOf(const nx::settings::PlayerCvars& player) -> cs::AutoSwitch;

/// The map `name` of `content`, its game started by the rules `cvars` set, the client's player joined
/// in the team of its first spawn point, asking for the name `player` holds and drawing its weapons as
/// `player` says, and its client's level read; meanwhile the workers of `jobs` build the map's mesh of
/// `navigation` the agents walk, unless the cache keeps it and `kept` reads it, which a map plays without
/// when it cannot be built. Errors: CommandFailed, with the cause.
[[nodiscard]] auto
startSession(const cs::GameContent& content, std::string_view name, const nx::settings::PlayerCvars& player,
             const cs::GameCvars& cvars, nx::jobs::JobSystem& jobs,
             const nx::Result<nx::game::NavigationData, nx::console::ConsoleError>& navigation, KeptMesh kept)
    -> nx::Result<std::unique_ptr<Session>, nx::console::ConsoleError>;

} // namespace app
