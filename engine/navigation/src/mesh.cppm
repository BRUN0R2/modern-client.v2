// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Navigation meshes (docs/adr/0023-navigation.md): for a profile, the convex polygons its middle may
/// stand on, built by Recast from a geometry in tiles, each tile a job of its own, and kept by Detour;
/// and the meshes of one geometry, one for each profile, which a profile's name finds.
export module nx.navigation:mesh;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import :profile;
import :geometry;

export namespace nx::navigation {

/// Why a query of the meshes could not be answered. The ADR's Rook API names these; where it carries
/// a position or a name, the failure's detail has it.
enum class NavigationError : std::uint8_t {
    InvalidQuery,   ///< Nonfinite coordinates, invalid area costs, or an invalid sampling radius.
    NoMesh,         ///< The profile has no mesh yet: none was built of its geometry.
    OutsideMesh,    ///< A position is on no polygon near it; the detail has the position.
    Unreachable,    ///< No way on the mesh joins the two positions.
    UnknownProfile, ///< No profile has the name; the detail has it.
};

/// A polygon of a mesh's outline: its corners, MeshOutline::corners from firstCorner on, and its kind.
struct MeshPolygon {
    std::uint32_t firstCorner = 0;
    std::uint32_t cornerCount = 0;
    AreaKind kind = AreaKind::Floor;

    [[nodiscard]] inline auto operator==(const MeshPolygon&) const noexcept -> bool = default;
};

/// What a mesh is made of, to be drawn: each polygon's corners in the game's space, in order around it.
struct MeshOutline {
    std::vector<Vec3> corners{};
    std::vector<MeshPolygon> polygons{};

    [[nodiscard]] inline auto operator==(const MeshOutline&) const -> bool = default;
};

/// The polygons a profile's middle may stand on, which queries search (NavigationQuery): Detour's
/// tiles, read from any thread.
class NavigationMesh {
public:
    /// The mesh of `geometry` for `profile`, built by `settings`, its tiles built in jobs of `jobs`: the
    /// same mesh with any number of workers. The triangles flat enough for the profile are floor, of
    /// their kind, where it fits standing; where it fits only crouched, of the kind Crouch. Errors: every
    /// MeshBuildError.
    [[nodiscard]] static auto build(jobs::JobSystem& jobs, const Geometry& geometry,
                                    const AgentProfile& profile, const BuildSettings& settings)
        -> Result<NavigationMesh, MeshBuildError>;

    NavigationMesh(NavigationMesh&& other) noexcept;
    auto operator=(NavigationMesh&& other) noexcept -> NavigationMesh&;
    NavigationMesh(const NavigationMesh&) = delete;
    auto operator=(const NavigationMesh&) -> NavigationMesh& = delete;
    ~NavigationMesh();

    [[nodiscard]] inline auto profile() const noexcept -> const AgentProfile& { return agentProfile; }
    [[nodiscard]] auto tileCount() const noexcept -> std::size_t;
    [[nodiscard]] auto polygonCount() const noexcept -> std::size_t;
    /// Its polygons, tile by tile, in the order Detour numbers them.
    [[nodiscard]] auto outline() const -> MeshOutline;

private:
    friend class NavigationQuery;
    struct Detour; // Detour's mesh and the tiles it reads

    NavigationMesh(std::unique_ptr<Detour> detour, AgentProfile profile) noexcept;

    std::unique_ptr<Detour> detourState;
    AgentProfile agentProfile;
};

/// A profile of NavigationMeshes, by its index.
struct ProfileIndex {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator==(const ProfileIndex&) const noexcept -> bool = default;
};

/// The meshes of one geometry, one for each profile, built when asked.
class NavigationMeshes {
public:
    /// The meshes of `profiles`, none built yet, built by `settings`. Errors: InvalidSettings,
    /// InvalidProfile (also two profiles of one name).
    [[nodiscard]] static auto create(std::vector<AgentProfile> profiles, BuildSettings settings)
        -> Result<NavigationMeshes, MeshBuildError>;

    [[nodiscard]] inline auto profiles() const noexcept -> std::span<const AgentProfile> {
        return agentProfiles;
    }
    [[nodiscard]] inline auto settings() const noexcept -> const BuildSettings& { return buildSettings; }

    /// The profile named `name`, as data names it. Errors: UnknownProfile.
    [[nodiscard]] auto profileNamed(std::string_view name) const -> Result<ProfileIndex, NavigationError>;

    /// Builds the mesh of `profile`, one of these, from `geometry` in jobs of `jobs`, in place of the one
    /// it had. Errors: those of NavigationMesh::build; the mesh it had stays.
    [[nodiscard]] auto build(jobs::JobSystem& jobs, const Geometry& geometry, ProfileIndex profile)
        -> Result<void, MeshBuildError>;

    /// The mesh of `profile`, one of these. Errors: NoMesh.
    [[nodiscard]] auto meshOf(ProfileIndex profile) const
        -> Result<std::reference_wrapper<const NavigationMesh>, NavigationError>;

    /// Forgets every mesh: the geometry they were built from is gone.
    void clear() noexcept;

private:
    NavigationMeshes(std::vector<AgentProfile> profiles, BuildSettings settings) noexcept;

    std::vector<AgentProfile> agentProfiles;
    BuildSettings buildSettings;
    std::vector<std::optional<NavigationMesh>> profileMeshes; ///< By ProfileIndex.
};

} // namespace nx::navigation

// What the module's units share of the build, which Recast's (build.cpp) hands Detour's (detour.cpp);
// not exported.
namespace nx::navigation::detail {

/// Watershed cells read beyond a tile besides its agent-radius border (Recast's three-cell pad).
inline constexpr int borderPadCells = 3;

/// A point of the game's space (z up) in Recast's and Detour's (y up), turned about x: the same
/// handedness, so a polygon keeps its winding.
[[nodiscard]] inline auto detourOf(Vec3 position) noexcept -> std::array<float, 3> {
    return {position.x, position.z, -position.y};
}

/// A point of Detour's space in the game's.
[[nodiscard]] inline auto positionOf(std::span<const float, 3> detour) noexcept -> Vec3 {
    return Vec3{.x = detour[0], .y = -detour[2], .z = detour[1]};
}

/// The area id Recast and Detour give `kind`: after Recast's 0, no area.
[[nodiscard]] inline auto areaIdOf(AreaKind kind) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::to_underlying(kind) + 1);
}

/// The kind of area id `areaId`, one areaIdOf gave.
[[nodiscard]] inline auto kindOfArea(std::uint8_t areaId) noexcept -> AreaKind {
    return static_cast<AreaKind>(areaId - 1);
}

/// The flag of the polygons agents may cross: every one today; a door will close its own (TODO.md).
inline constexpr std::uint16_t kWalkablePolygon = 1;

/// Detour's 32-bit references number a tile and a polygon of it in 22 bits, the rest the salt, at least
/// the 10 dtNavMesh::init asks; the tiles take the bits their count needs, up to 14, as Recast's samples
/// do, and the polygons of each the rest.
inline constexpr std::uint32_t kTileAndPolygonBits = 22;
inline constexpr std::uint32_t kMostTileBits = 14;

/// A tile Recast built: where it is in the grid, how many polygons it has, and Detour's data of it.
struct BuiltTile {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t polygonCount = 0;
    std::vector<std::byte> data{};
};

/// A geometry's tiles that hold floor: the grid they are in, and its first corner, in Detour's space.
struct BuiltTiles {
    std::array<float, 3> origin{};
    float tileSide = 0.0F; ///< In units.
    std::int32_t tilesAcross = 0;
    std::int32_t tilesDown = 0;
    std::vector<BuiltTile> tiles{}; ///< Row by row.
};

/// Builds the tiles of `geometry` for `profile` by `settings`, which passed checkProfile, each in a job
/// of `jobs`. Errors: InvalidGeometry, TooLarge, TileRefused.
[[nodiscard]] auto buildTiles(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                              const BuildSettings& settings) -> Result<BuiltTiles, MeshBuildError>;

} // namespace nx::navigation::detail
