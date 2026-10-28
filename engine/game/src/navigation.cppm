// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the game gives navigation (docs/adr/0023-navigation.md), which knows nothing of maps: the
/// agents' profiles and how their meshes are built, data/cstrike/navigation.toml; and a map's
/// geometry, the faces of its world and of its solid brush entities where they are.
export module nx.game:navigation;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.ecs;
import nx.assets;
import nx.navigation;
import :brushEntities;

export namespace nx::game {

/// data/cstrike/navigation.toml: the agents' profiles, and how their meshes are built.
struct NavigationData {
    std::vector<navigation::AgentProfile> profiles{};
    navigation::BuildSettings build{};
};

/// Reads `text`, the file `sourceName`: profiles of different names, each and the settings within
/// their rules (navigation::NavigationMeshes::create). Errors: ConfigInvalid.
[[nodiscard]] auto readNavigationData(std::string_view text, std::string_view sourceName)
    -> Result<NavigationData, GameError>;

/// What agents walk on and among in `map`, whose brush entities `world` holds (registerComponents,
/// spawnBrushEntities):
/// the faces of the world, and those of its solid brush entities (Solid::Bsp) where `world` has them,
/// at rest as the map starts; each a polygon facing where its face does. A face under a liquid (water,
/// slime, lava) is of the kind Water, the others Floor. The faces of the sky and of liquids' surfaces
/// are left out: agents walk under the one, and wade through the other.
[[nodiscard]] auto navigationGeometryOf(const assets::BspView& map, ecs::World& world)
    -> navigation::Geometry;

} // namespace nx::game

template <>
struct nx::meta::Description<nx::game::NavigationData> {
    static constexpr std::string_view kName = "nx::game::NavigationData";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::game::NavigationData::profiles>("profiles"),
        nx::meta::field<&nx::game::NavigationData::build>("build"),
    };
};
