// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's navigation tools (docs/adr/0023-navigation.md), by Condition Zero's names:
/// `nav_generate` builds the meshes of the map played, one for each profile of
/// data/cstrike/navigation.toml, from the faces of its world and of its brush entities as the map
/// starts, in jobs; `nav_draw` draws a mesh's polygons and edges over the scene.
export module cs.client:navigationTools;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.game;
import nx.navigation;
import nx.render;
import :scene;

export namespace cs {

/// The navigation tools of the map played: its meshes, and the one nav_draw draws. They stay where they
/// are once registered: the console's commands point into them.
class NavigationTools {
public:
    /// The tools of the profiles of `navigation`, the map's brush entities made by `classes`, which must
    /// outlive them; no map played yet. Errors: those of nx::navigation::NavigationMeshes::create.
    [[nodiscard]] static auto create(const nx::game::NavigationData& navigation,
                                     const nx::game::EntityClasses& classes)
        -> nx::Result<NavigationTools, nx::navigation::MeshBuildError>;

    /// Registers in `console` `nav_generate` and `nav_draw`, which act on these tools. Errors: those of
    /// nx::console::Console::registerCommand.
    [[nodiscard]] auto registerIn(nx::console::Console& console)
        -> nx::Result<void, nx::console::ConsoleError>;

    /// Plays `map` from now on, which must outlive the next play: the meshes of the last map are
    /// forgotten, none drawn.
    void play(const nx::assets::BspView& map) noexcept;

    /// What nav_draw draws over the frame: the polygons and edges of a mesh, a cell above it, or nothing.
    [[nodiscard]] auto overlay() const noexcept -> SceneOverlay;

    /// Builds the meshes of the map played, telling `console` what each holds. Errors: CommandFailed (no
    /// map played, or a mesh that could not be built, its failure the cause).
    [[nodiscard]] auto generate(nx::console::Console& console) -> nx::Result<void, nx::console::ConsoleError>;

    /// Draws the mesh of the profile `words` names, the first profile's when it names none; with no name
    /// while one is drawn, stops. Errors: InvalidArguments (more than a name, no profile, or one no
    /// profile has: UnknownProfile the cause), CommandFailed (a profile with no mesh: NoMesh the cause).
    [[nodiscard]] auto draw(std::span<const std::string> words)
        -> nx::Result<void, nx::console::ConsoleError>;

private:
    NavigationTools(const nx::game::EntityClasses& classes, nx::navigation::NavigationMeshes meshes) noexcept;

    /// Makes nav_draw's polygons and edges of `mesh`.
    void outline(const nx::navigation::NavigationMesh& mesh);

    const nx::game::EntityClasses* m_classes;
    nx::navigation::NavigationMeshes m_meshes;
    const nx::assets::BspView* m_map = nullptr;          ///< Played; none before the first map.
    std::optional<nx::navigation::ProfileIndex> m_drawn; ///< Whose mesh nav_draw draws.
    std::vector<nx::render::OverlayVertex> m_polygons;   ///< Every three a triangle.
    std::vector<nx::render::OverlayVertex> m_edges;      ///< Every two a line.
};

} // namespace cs
