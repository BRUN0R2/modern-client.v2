// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Navigation meshes (docs/adr/0023-navigation.md): for a profile, the convex polygons its middle may
/// stand on, built by Recast from a geometry in tiles, each tile a job of its own, while the thread that
/// asked for it goes on (MeshBuild), and kept by Detour; and the meshes of one geometry, one for each
/// profile, which a profile's name finds.
export module nx.navigation:mesh;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import :profile;
import :geometry;

namespace nx::navigation::detail {

struct BuiltTiles;

} // namespace nx::navigation::detail

export namespace nx::navigation {

class NavigationMesh;

/// A mesh being built in jobs (NavigationMesh::startBuild): its tiles, each in a job of its own, then
/// Detour's mesh of them in one more, all of the priority Background. Its jobs read and write only what
/// it holds, so going before they have run waits for them.
class MeshBuild {
public:
    MeshBuild(MeshBuild&& other) noexcept;
    auto operator=(MeshBuild&& other) -> MeshBuild& = delete;
    MeshBuild(const MeshBuild&) = delete;
    auto operator=(const MeshBuild&) -> MeshBuild& = delete;
    /// Waits for the jobs that have not run yet, running jobs meanwhile.
    ~MeshBuild();

    /// The mesh, once its jobs have run: meanwhile the calling thread runs jobs (jobs::JobSystem::wait).
    /// Errors: TooLarge (a tile of more polygons than Detour numbers), TileRefused.
    [[nodiscard]] auto finish() && -> Result<NavigationMesh, MeshBuildError>;

private:
    friend class NavigationMesh;
    struct State; // the jobs' input and output, and the handle of the last

    explicit MeshBuild(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> buildState;
};

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
    /// Starts building the mesh of `geometry` for `profile` by `settings` in jobs of `jobs`, which must
    /// outlive the build: the same mesh with any number of workers. The triangles flat enough for the
    /// profile are floor, of their kind, where it fits standing; where it fits only crouched, of the kind
    /// Crouch. What the jobs need of `geometry` is copied: it may go at once. Errors: InvalidProfile,
    /// InvalidSettings, InvalidGeometry, TooLarge (more tiles than Detour numbers, or the geometry taller
    /// than Recast's voxels reach).
    [[nodiscard]] static auto startBuild(jobs::JobSystem& jobs, const Geometry& geometry,
                                         const AgentProfile& profile, const BuildSettings& settings)
        -> Result<MeshBuild, MeshBuildError>;

    /// The mesh startBuild starts, once built: meanwhile the calling thread runs jobs. Errors: every
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

    /// Detour's mesh of `built`, for `profile`. Errors: TooLarge.
    [[nodiscard]] static auto assemble(detail::BuiltTiles built, const AgentProfile& profile)
        -> Result<NavigationMesh, MeshBuildError>;

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

/// What the jobs that build a geometry's tiles share: what each tile is built from, and its result.
struct TileWork;

/// The jobs startTiles submitted: the work they share, and the handle that completes once they all ran.
struct TileJobs {
    /// Shared only so that a unit without TileWork's definition can hold it: one owner waits for the jobs.
    std::shared_ptr<TileWork> work{};
    jobs::JobHandle done{};
};

/// Submits to `jobs` the jobs that build the tiles of `geometry` for `profile` by `settings`, which passed
/// checkProfile, each tile in a job of its own of `priority`. Errors: InvalidGeometry, TooLarge.
[[nodiscard]] auto startTiles(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                              const BuildSettings& settings, jobs::Priority priority)
    -> Result<TileJobs, MeshBuildError>;

/// The tiles of `work` that hold floor, once its jobs have run; what they were built from goes. Errors:
/// TileRefused.
[[nodiscard]] auto takeTiles(TileWork& work) -> Result<BuiltTiles, MeshBuildError>;

} // namespace nx::navigation::detail
