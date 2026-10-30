// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a mesh is built for and how (docs/adr/0023-navigation.md): an agent's profile, its size and
/// its moves, which each has a mesh of its own; the kinds of area its polygons are, which each profile
/// and each query weighs; and the settings of the build, the same for every profile.
export module nx.navigation:profile;

import std;
import nx.core;
import nx.meta;

export namespace nx::navigation {

/// What a polygon of a mesh is, which a profile and a query weigh (AreaCosts).
enum class AreaKind : std::uint8_t {
    Floor,  ///< Walked on standing.
    Crouch, ///< Under too little room to stand: crossed crouched.
    Water,  ///< Under a liquid: waded or swum.
    /// Floor along walls and ledges, within the profile's wallClearance of where it may not stand: walked,
    /// and weighed, so that the ways keep off walls where there is room.
    Edge,
    Count,
};

/// What a kind of area costs: how many times its length a way across it counts.
struct AreaCost {
    AreaKind area = AreaKind::Floor;
    float cost = 1.0F;
};

/// The cost of each kind of area.
using AreaCosts = EnumArray<AreaKind, float>;

/// An agent's size and moves, which its mesh is built for: where its middle may go, what it climbs
/// and crouches under, and what each kind of area costs it.
struct AgentProfile {
    std::string name{}; ///< How data, the console and plugins name it ("player").
    /// Half its width: its middle stays this far from walls and ledges.
    HammerUnits radius{};
    /// How much farther than its radius it would rather keep from walls and ledges where there is room:
    /// the floor within it is of the kind Edge, which areaCosts weighs; none for none.
    HammerUnits wallClearance{};
    HammerUnits standingHeight{};
    /// Where less room than standingHeight but this much is above the floor, it crouches (Crouch); less
    /// room than this is no way.
    HammerUnits crouchedHeight{};
    HammerUnits stepHeight{}; ///< The highest step it walks up.
    /// The highest ledge it jumps onto: what its jump links will span (TODO.md), at least stepHeight.
    HammerUnits jumpHeight{};
    /// The highest ledge it drops from unhurt: how far below a body off the mesh the floor it drops to is
    /// looked for (NavigationQuery), and what its drop links will span (TODO.md); at least stepHeight.
    HammerUnits dropHeight{};
    /// The least upward share of a floor's unit normal it stands on: a steeper floor is a wall.
    float minGroundNormalZ = 0.0F;
    /// The kinds of area that do not cost their length, each once and at least 1: A* finds the cheapest
    /// way only when no area counts less than its length.
    std::vector<AreaCost> areaCosts{};
};

/// The costs `profile` gives each kind of area: its areaCosts, 1 for the others.
[[nodiscard]] auto areaCostsOf(const AgentProfile& profile) -> AreaCosts;

/// How meshes are built: the voxels Recast fills with the geometry, the tiles they are cut into, and
/// how closely the polygons follow the floor.
struct BuildSettings {
    HammerUnits cellSize{};      ///< A voxel's side across: what a mesh cannot tell apart.
    HammerUnits cellHeight{};    ///< A voxel's height.
    std::uint32_t tileCells = 0; ///< A tile's side, in cells: built on its own, in its own job.
    /// Floor that a square of this side would not cover, cut off from the rest, is left out.
    HammerUnits smallestIsland{};
    /// Regions that a square of this side would not cover are merged into their neighbors.
    HammerUnits smallestRegion{};
    HammerUnits longestEdge{}; ///< Edges along walls are split to be no longer.
    HammerUnits edgeError{};   ///< How far an edge may stray from the outline of the floor.
    /// How far apart the floor's height is sampled for the polygons' details; 0 for none.
    HammerUnits detailSampleDistance{};
    HammerUnits detailHeightError{}; ///< How far the details may stray from the floor.
};

/// Why a mesh could not be built.
enum class MeshBuildError : std::uint8_t {
    InvalidProfile, ///< A profile breaks its rules (AgentProfile) or the voxels' limits; the detail says how.
    InvalidSettings, ///< The settings break their rules (checkSettings); the detail says how.
    InvalidGeometry, ///< A triangle names a vertex the geometry lacks, or a vertex is not finite.
    TooLarge,    ///< More tiles, or a tile with more polygons, than Detour numbers, or the geometry taller
                 ///< than Recast's voxels reach; the detail says which.
    TileRefused, ///< Recast refused a tile, out of memory or of its own numbers; the detail has its message.
};

/// Whether `settings` can build: sizes positive, tileCells too, the rest not negative. Errors:
/// InvalidSettings.
[[nodiscard]] auto checkSettings(const BuildSettings& settings) -> Result<void, MeshBuildError>;

/// Whether `profile` can be built by `settings`, which must pass checkSettings: a name; a radius, heights
/// and a step positive, a wall clearance of none or more, crouched no taller than standing, the jump no
/// lower than the step; a floor's
/// normal from just above 0 to 1; each kind's cost once and at least 1; and its heights within the
/// voxels a span counts. Errors: InvalidProfile.
[[nodiscard]] auto checkProfile(const AgentProfile& profile, const BuildSettings& settings)
    -> Result<void, MeshBuildError>;

} // namespace nx::navigation

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
        nx::meta::field<&nx::navigation::AgentProfile::wallClearance>("wallClearance"),
        nx::meta::field<&nx::navigation::AgentProfile::standingHeight>("standingHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::crouchedHeight>("crouchedHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::stepHeight>("stepHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::jumpHeight>("jumpHeight"),
        nx::meta::field<&nx::navigation::AgentProfile::dropHeight>("dropHeight"),
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
