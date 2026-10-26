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
struct nx::meta::Description<nx::navigation::AreaCost> {
    static constexpr std::string_view kName = "nx::navigation::AreaCost";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::navigation::AreaCost::area>("area"),
        nx::meta::field<&nx::navigation::AreaCost::cost>("cost"),
    };
};

template <>
struct nx::meta::Description<nx::navigation::AgentProfile> {
    static constexpr std::string_view kName = "nx::navigation::AgentProfile";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::navigation::AgentProfile::name>("name"),
        nx::meta::field<&nx::navigation::AgentProfile::radius>("radius"),
        nx::meta::field<&nx::navigation::AgentProfile::standingHeight>("standingHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::crouchedHeight>("crouchedHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::stepHeight>("stepHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::jumpHeight>("jumpHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::minGroundNormalZ>("minGroundNormalZ"),
        nx::meta::field<&nx::navigation::AgentProfile::areaCosts>("areaCosts"),
    };
};

template <>
struct nx::meta::Description<nx::navigation::BuildSettings> {
    static constexpr std::string_view kName = "nx::navigation::BuildSettings";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::navigation::BuildSettings::cellSize>("cellSize"),
        nx::meta::field<&nx::navigation::BuildSettings::cellHeight>("cellHeight"),
        nx::meta::field<&nx::navigation::BuildSettings::tileCells>("tileCells"),
        nx::meta::field<&nx::navigation::BuildSettings::smallestIsland>("smallestIsland"),
        nx::meta::field<&nx::navigation::BuildSettings::smallestRegion>("smallestRegion"),
        nx::meta::field<&nx::navigation::BuildSettings::longestEdge>("longestEdge"),
        nx::meta::field<&nx::navigation::BuildSettings::edgeError>("edgeError"),
        nx::meta::field<&nx::navigation::BuildSettings::detailSampleDistance>("detailSampleDistance"),
        nx::meta::field<&nx::navigation::BuildSettings::detailHeightError>("detailHeightError"),
    };
};

template <>
struct nx::meta::Description<nx::game::NavigationData> {
    static constexpr std::string_view kName = "nx::game::NavigationData";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::game::NavigationData::profiles>("profiles"),
        nx::meta::field<&nx::game::NavigationData::build>("build"),
    };
};
