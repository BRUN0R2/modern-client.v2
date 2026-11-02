// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.platform;
import nx.console;
import cs.server;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

/// The folder of the cache (nx::platform::cacheDirectory) that keeps the maps' navigation meshes: a file
/// for each map and profile, which holds its mesh's key, so that a map whose content, profile or settings
/// changed has its mesh built again and kept in place of the old one.
constexpr std::string_view kMeshCacheFolder = "navigation";
constexpr std::string_view kMeshFileExtension = ".nxnav";

/// Whether a file's name keeps `letter` as it is.
[[nodiscard]] auto isFileNameLetter(char letter) noexcept -> bool {
    return std::isalnum(static_cast<unsigned char>(letter)) != 0 || letter == '_' || letter == '-' ||
           letter == '.';
}

/// The file of the cache that keeps the mesh of `profile` of the map `map`, the characters a file's name
/// may not hold (a map in a folder of maps/, say) turned into '_'; none when the system names no cache
/// folder.
[[nodiscard]] auto meshFileOf(std::string_view map, std::string_view profile)
    -> std::optional<std::filesystem::path> {
    const auto folder = nx::platform::cacheDirectory();
    if (!folder) {
        return std::nullopt;
    }
    std::string name = std::format("{}.{}{}", map, profile, kMeshFileExtension);
    std::ranges::replace_if(name, std::not_fn(isFileNameLetter), '_');
    return *folder / kMeshCacheFolder / nx::platform::fromUtf8(name);
}

/// The mesh `file` keeps for `key`, for `profile`; none when it keeps none, or another (logged).
[[nodiscard]] auto readKeptMesh(const std::filesystem::path& file, nx::navigation::MeshKey key,
                                const nx::navigation::AgentProfile& profile)
    -> std::optional<nx::navigation::NavigationMesh> {
    auto bytes = nx::platform::readFile(file);
    if (!bytes) {
        if (bytes.error().code != nx::platform::FileError::NotFound) {
            nx::log::warning(kLog, "the kept navigation mesh cannot be read: {}",
                             nx::describe(bytes.error()));
        }
        return std::nullopt;
    }
    auto mesh = nx::navigation::NavigationMesh::load(*bytes, key, profile);
    if (!mesh) {
        nx::log::info(kLog, "the kept navigation mesh {} is built again: {}", nx::platform::toUtf8(file),
                      nx::describe(mesh.error()));
        return std::nullopt;
    }
    return std::move(*mesh);
}

/// Keeps `mesh`, of `key`, in `file`, for the next time its map loads; logs what fails.
void keepMesh(const std::filesystem::path& file, const nx::navigation::NavigationMesh& mesh,
              nx::navigation::MeshKey key) {
    auto written = nx::platform::createDirectories(file.parent_path()).and_then([&] {
        return nx::platform::writeFile(file, mesh.save(key));
    });
    if (!written) {
        nx::log::warning(kLog, "the navigation mesh is not kept: {}", nx::describe(written.error()));
    }
}

} // namespace

/// The mesh of `game`'s map, the map `map`, that the agents walk: cs::standingPlayerProfile of `navigation`,
/// by its settings; read from the cache when it keeps it and `kept` reads it, else started in `jobs`.
/// Errors: CommandFailed, with the cause (also `navigation`'s).
auto startAgentMesh(nx::jobs::JobSystem& jobs, cs::Game& game, std::string_view map,
                    const nx::Result<nx::game::NavigationData, nx::console::ConsoleError>& navigation,
                    KeptMesh kept) -> nx::Result<AgentMeshLoad, nx::console::ConsoleError> {
    if (!navigation) {
        return std::unexpected{navigation.error()};
    }
    auto profile = cs::standingPlayerProfile(*navigation);
    if (!profile) {
        return nx::fail(nx::console::ConsoleError::CommandFailed, std::move(profile.error()));
    }
    const nx::navigation::Geometry geometry = game.navigationGeometry();
    const nx::navigation::MeshKey key = nx::navigation::meshKeyOf(geometry, *profile, navigation->build);
    std::optional<std::filesystem::path> file = meshFileOf(map, profile->name);
    if (file && kept == KeptMesh::Read) {
        if (auto read = readKeptMesh(*file, key, *profile)) {
            return AgentMeshLoad{.mesh = std::move(*read), .key = key, .file = std::move(file)};
        }
    }
    auto started = nx::navigation::NavigationMesh::startBuild(jobs, geometry, *profile, navigation->build);
    if (!started) {
        return nx::fail(nx::console::ConsoleError::CommandFailed, std::move(started.error()));
    }
    return AgentMeshLoad{.mesh = std::move(*started), .key = key, .file = std::move(file)};
}

/// The mesh `load` has, once built, then kept for the next time when it was built, which is logged.
/// Errors: CommandFailed, with the cause.
auto finishAgentMesh(nx::Result<AgentMeshLoad, nx::console::ConsoleError> load)
    -> nx::Result<nx::navigation::NavigationMesh, nx::console::ConsoleError> {
    if (!load) {
        return std::unexpected{std::move(load.error())};
    }
    if (auto* const kept = std::get_if<nx::navigation::NavigationMesh>(&load->mesh)) {
        return std::move(*kept);
    }
    auto mesh = std::get<nx::navigation::MeshBuild>(std::move(load->mesh)).finish();
    if (!mesh) {
        return nx::fail(nx::console::ConsoleError::CommandFailed, std::move(mesh.error()));
    }
    nx::log::info(kLog, "the agents' navigation mesh was built in {}: {} polygons in {} tiles",
                  std::chrono::duration_cast<std::chrono::milliseconds>(load->sinceStart.elapsed()),
                  mesh->polygonCount(), mesh->tileCount());
    if (load->file) {
        keepMesh(*load->file, *mesh, load->key);
    }
    return std::move(*mesh);
}

} // namespace app
