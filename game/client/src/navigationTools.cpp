// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.jobs;
import nx.ecs;
import nx.assets;
import nx.game;
import nx.navigation;
import nx.render;

namespace cs {

namespace {

using nx::console::ConsoleError;
using nx::navigation::AreaKind;
using nx::render::OverlayColor;

/// The color of each kind of area nav_draw draws: its edges as they are, its polygons see-through.
constexpr nx::EnumArray<AreaKind, OverlayColor> kAreaColors{nx::EnumArray<AreaKind, OverlayColor>::Storage{
    OverlayColor{.red = 64, .green = 208, .blue = 64},    // Floor: green
    OverlayColor{.red = 240, .green = 200, .blue = 32},   // Crouch: yellow
    OverlayColor{.red = 48, .green = 128, .blue = 255}}}; // Water: blue
constexpr std::uint8_t kPolygonAlpha = 64;

} // namespace

NavigationTools::NavigationTools(const nx::game::EntityClasses& classes,
                                 nx::navigation::NavigationMeshes meshes) noexcept
    : m_classes{&classes}, m_meshes{std::move(meshes)} {}

auto NavigationTools::create(const nx::game::NavigationData& navigation,
                             const nx::game::EntityClasses& classes)
    -> nx::Result<NavigationTools, nx::navigation::MeshBuildError> {
    auto meshes = nx::navigation::NavigationMeshes::create(navigation.profiles, navigation.build);
    if (!meshes) {
        return std::unexpected{std::move(meshes.error())};
    }
    return NavigationTools{classes, std::move(*meshes)};
}

auto NavigationTools::registerIn(nx::console::Console& console) -> nx::Result<void, ConsoleError> {
    using nx::console::CommandLine;
    using nx::console::Console;
    const auto add = [&console](nx::console::CommandDesc desc, nx::console::CommandHandler handler) {
        return console.registerCommand(desc, std::move(handler));
    };
    for (auto&& registered : {
             add({.name = "nav_generate",
                  .description =
                      "Builds the navigation meshes of the map played, one for each agent profile."},
                 [this](Console& runner, const CommandLine&) { return generate(runner); }),
             add({.name = "nav_draw",
                  .description = "Draws a navigation mesh over the scene, the first profile's when none is "
                                 "named, or stops: nav_draw [profile]."},
                 [this](Console&, const CommandLine& line) { return draw(line.arguments()); }),
         }) {
        if (!registered) {
            return registered;
        }
    }
    return {};
}

void NavigationTools::play(const nx::assets::BspView& map) noexcept {
    m_map = &map;
    m_meshes.clear();
    m_drawn.reset();
    m_polygons.clear();
    m_edges.clear();
}

auto NavigationTools::overlay() const noexcept -> SceneOverlay {
    return SceneOverlay{.triangles = m_polygons, .lines = m_edges};
}

auto NavigationTools::generate(nx::console::Console& console) -> nx::Result<void, ConsoleError> {
    if (m_map == nullptr) {
        return nx::fail(ConsoleError::CommandFailed, "nav_generate: not playing a map");
    }
    // The brush entities as the game makes them when the map starts: at rest.
    nx::ecs::World world;
    if (auto registered = nx::game::registerComponents(world); !registered) {
        return nx::fail(ConsoleError::CommandFailed, "nav_generate", std::move(registered.error()));
    }
    if (auto spawned = nx::game::spawnBrushEntities(world, m_map->entities(), m_map->models(), *m_classes);
        !spawned) {
        return nx::fail(ConsoleError::CommandFailed, "nav_generate", std::move(spawned.error()));
    }
    const nx::navigation::Geometry geometry = nx::game::navigationGeometryOf(*m_map, world);
    auto jobs = nx::jobs::JobSystem::create();
    if (!jobs) {
        return nx::fail(ConsoleError::CommandFailed, "nav_generate", std::move(jobs.error()));
    }
    for (std::uint32_t index = 0; index < m_meshes.profiles().size(); ++index) {
        const nx::navigation::ProfileIndex profile{index};
        const nx::platform::Stopwatch stopwatch;
        if (auto built = m_meshes.build(**jobs, geometry, profile); !built) {
            return nx::fail(ConsoleError::CommandFailed, "nav_generate", std::move(built.error()));
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(stopwatch.elapsed());
        auto mesh = m_meshes.meshOf(profile);
        if (!mesh) {
            return nx::fail(ConsoleError::CommandFailed, "nav_generate", std::move(mesh.error()));
        }
        console.print(std::format("The navigation mesh of {:?}: {} polygons in {} tiles, built in {}",
                                  m_meshes.profiles()[index].name, mesh->get().polygonCount(),
                                  mesh->get().tileCount(), elapsed));
        if (m_drawn == profile) {
            outline(mesh->get());
        }
    }
    return {};
}

auto NavigationTools::draw(std::span<const std::string> words) -> nx::Result<void, ConsoleError> {
    if (words.size() > 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: nav_draw [profile]");
    }
    if (words.empty() && m_drawn) {
        m_drawn.reset();
        m_polygons.clear();
        m_edges.clear();
        return {};
    }
    if (m_meshes.profiles().empty()) {
        return nx::fail(ConsoleError::InvalidArguments, "nav_draw: no profile to draw");
    }
    const std::string_view name =
        words.empty() ? std::string_view{m_meshes.profiles().front().name} : std::string_view{words.front()};
    auto profile = m_meshes.profileNamed(name);
    if (!profile) {
        return nx::fail(ConsoleError::InvalidArguments, "nav_draw", std::move(profile.error()));
    }
    auto mesh = m_meshes.meshOf(*profile);
    if (!mesh) {
        return nx::fail(ConsoleError::CommandFailed, "nav_draw: nav_generate builds it",
                        std::move(mesh.error()));
    }
    m_drawn = *profile;
    outline(mesh->get());
    return {};
}

void NavigationTools::outline(const nx::navigation::NavigationMesh& mesh) {
    const nx::navigation::MeshOutline outline = mesh.outline();
    // A cell above the polygons, whose corners lie up to that much under the floor they cover.
    const nx::Vec3 lift{.z = m_meshes.settings().cellHeight.value()};
    m_polygons.clear();
    m_edges.clear();
    for (const nx::navigation::MeshPolygon& polygon : outline.polygons) {
        const std::span<const nx::Vec3> corners =
            std::span{outline.corners}.subspan(polygon.firstCorner, polygon.cornerCount);
        const OverlayColor edge = kAreaColors[polygon.kind];
        const OverlayColor fill{
            .red = edge.red, .green = edge.green, .blue = edge.blue, .alpha = kPolygonAlpha};
        for (std::size_t corner = 2; corner < corners.size(); ++corner) {
            for (const nx::Vec3 point : {corners.front(), corners[corner - 1], corners[corner]}) {
                m_polygons.push_back({.position = point + lift, .color = fill});
            }
        }
        for (std::size_t corner = 0; corner < corners.size(); ++corner) {
            m_edges.push_back({.position = corners[corner] + lift, .color = edge});
            m_edges.push_back({.position = corners[(corner + 1) % corners.size()] + lift, .color = edge});
        }
    }
}

} // namespace cs
