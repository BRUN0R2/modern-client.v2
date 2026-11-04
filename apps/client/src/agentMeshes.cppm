// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The mesh a map's agents walk (nx.navigation) as the map loads: built in jobs, and kept in the cache from
/// run to run.
export module app.client:agentMeshes;

import std;
import nx.core;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.platform;
import nx.console;
import cs.server;

namespace app {

/// The agents' mesh of a map as it loads: read from the cache, or being built in jobs, then kept there.
struct AgentMeshLoad {
    std::variant<nx::navigation::NavigationMesh, nx::navigation::MeshBuild> mesh;
    nx::navigation::MeshKey key{};
    std::optional<std::filesystem::path> file{}; ///< Where it is kept, when the system names a cache folder.
    nx::platform::Stopwatch sinceStart{};        ///< Since its build started, when it is built.
};

/// Whether a map's agents' mesh is read from the cache when the cache keeps it, or built again
/// (nav_generate).
enum class KeptMesh : std::uint8_t {
    Read,
    Ignored,
};

/// The mesh of `game`'s map, the map `map`, that the agents walk: cs::agentProfile of `navigation`,
/// by its settings; read from the cache when it keeps it and `kept` reads it, else started in `jobs`.
/// Errors: CommandFailed, with the cause (also `navigation`'s).
[[nodiscard]] auto
startAgentMesh(nx::jobs::JobSystem& jobs, cs::Game& game, std::string_view map,
               const nx::Result<nx::game::NavigationData, nx::console::ConsoleError>& navigation,
               KeptMesh kept) -> nx::Result<AgentMeshLoad, nx::console::ConsoleError>;

/// The mesh `load` has, once built, then kept for the next time when it was built, which is logged.
/// Errors: CommandFailed, with the cause.
[[nodiscard]] auto finishAgentMesh(nx::Result<AgentMeshLoad, nx::console::ConsoleError> load)
    -> nx::Result<nx::navigation::NavigationMesh, nx::console::ConsoleError>;

} // namespace app
