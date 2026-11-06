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

/// What a mesh is built from, as one number (meshKeyOf): two builds of one key make the same tiles, so a
/// mesh kept under its key (NavigationMesh::save) stands for building it again.
struct MeshKey {
    std::uint64_t value = 0;

    [[nodiscard]] inline auto operator==(const MeshKey&) const noexcept -> bool = default;
};

/// The key of the mesh of `geometry` for `profile` by `settings`: of all a build reads of the geometry (its
/// vertices and triangles, ladders, doors and body hulls, and each brush entity's hull with where a body may
/// meet it), of what of the profile shapes the tiles (its size, step, jump, drop and steepest floor, not its
/// name or its costs, which only queries weigh) and of every setting, and of the version of the build and of
/// Detour's tiles, which change with what they make.
[[nodiscard]] auto meshKeyOf(const Geometry& geometry, const AgentProfile& profile,
                             const BuildSettings& settings) -> MeshKey;

/// Why bytes could not be read back as a mesh (NavigationMesh::load).
enum class MeshFileError : std::uint8_t {
    NotAMesh,  ///< Not what NavigationMesh::save writes, or what another version of it wrote.
    OtherMesh, ///< A mesh of another key: of other geometry, profile or settings, or of another build.
    Damaged,   ///< Cut short, or changed since it was written; the detail says where.
};

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

/// The ground a body leaves the start of a link over a gap from, which how fast it leaves it depends on
/// (fastestTakeoffOf, leastLeapSpeedOf): how far that floor goes on behind the start, straight away from the
/// end, as far as a body's middle stands on the mesh; how high a body jumping there rises before its head
/// meets anything, as high as the link's kind jumps at most, as the build flies it; and whether only a
/// crouched body stands there, which runs no faster than it walks crouched.
struct Takeoff {
    HammerUnits room{};
    HammerUnits rise{};
    bool isCrouched = false;

    [[nodiscard]] inline auto operator==(const Takeoff&) const noexcept -> bool = default;
};

/// A link of a mesh between a ledge and the floor below (AreaKind::Jump, AreaKind::Drop), or two floors a
/// ladder joins (AreaKind::Ladder) or a door between (AreaKind::Door): where a body leaves the one, where it
/// lands on the other, and its kind, of which Ladder and Door go both ways, and Jump and CrouchedJump too but
/// over a gap, where each way is a link of its own, a flight of its own; of a Ladder or a Door, the middle of
/// what it goes through across, to whole units, at the height of the start; and of a link over a gap, the
/// ground a body leaves its start from, none for one that tells nothing of it, which is left as from open
/// ground.
struct MeshLink {
    Vec3 start{};
    Vec3 end{};
    AreaKind kind = AreaKind::Drop;
    Vec3 through{};
    std::optional<Takeoff> takeoff{};

    [[nodiscard]] inline auto operator==(const MeshLink&) const noexcept -> bool = default;
};

/// How a body opens a door by what opens it.
enum class OpenerKind : std::uint8_t {
    Used,    ///< It uses it from nearby, as a player presses a button, or a door that only a use opens.
    Entered, ///< It walks into it: a volume that fires the door as a body touches it (trigger_multiple).
};

/// What opens a door: its box, and how a body opens the door by it.
struct GateOpener {
    Aabb box{};
    OpenerKind kind = OpenerKind::Used;
};

/// A door of a map, as the agents whose ways go through it (AreaKind::Door) see it: the middle of its box as
/// it stands closed, as its links name it (MeshLink::through); whether a body gets through, open or opened by
/// walking into it; and what opens it, that a body uses from nearby or walks into.
struct Gate {
    Vec3 middle{};
    bool isOpen = false;
    std::vector<GateOpener> openers{};
};

/// Whether `first` and `second`, what a link or a gate has for the middle of a door, are of one door: a link
/// keeps it to whole units across.
[[nodiscard]] inline auto isSameDoor(Vec3 first, Vec3 second) noexcept -> bool {
    constexpr float kSameDoor = 1.5F;
    return std::hypot(first.x - second.x, first.y - second.y) <= kSameDoor;
}

/// What a mesh is made of, to be drawn: each polygon's corners in the game's space, in order around it,
/// and its links joined to the floor at both ends, their ends on the polygons they join.
struct MeshOutline {
    std::vector<Vec3> corners{};
    std::vector<MeshPolygon> polygons{};
    std::vector<MeshLink> links{};

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
    /// How many polygons Detour holds of it, one of them for each of its links.
    [[nodiscard]] auto polygonCount() const noexcept -> std::size_t;
    /// Its polygons, tile by tile, in the order Detour numbers them.
    [[nodiscard]] auto outline() const -> MeshOutline;

    /// The bytes that keep it as the mesh of `key`, which load reads back without building anything:
    /// Detour's tiles as it holds them, row by row, after a header of the key and of a hash of the rest.
    /// Detour rebuilds the links it wrote into them as it takes them again.
    [[nodiscard]] auto save(MeshKey key) const -> std::vector<std::byte>;

    /// The mesh `bytes` keep, which save wrote for `key`, for `profile`, the one it was built for.
    /// Errors: NotAMesh, OtherMesh, Damaged.
    [[nodiscard]] static auto load(std::span<const std::byte> bytes, MeshKey key, const AgentProfile& profile)
        -> Result<NavigationMesh, MeshFileError>;

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

/// How far inside its tile a link leaving from the tile's side starts: Detour keeps a link in one tile alone.
inline constexpr float kLinkInsetFromTileSide = 1.0F;

/// How far across a body of radius `radius` flies over nothing from `start` to `end`: what its two sides do
/// not span, none or less for ends its box spans at once. A link over a gap goes one way
/// (TileLinks::addFlight), and takes a leap's speed across (leastLeapSpeedOf).
[[nodiscard]] inline auto gapAcross(Vec3 start, Vec3 end, HammerUnits radius) noexcept -> float {
    return std::hypot(end.x - start.x, end.y - start.y) - (2.0F * radius.value());
}

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

/// How many bits each coordinate across of the middle of a ladder or a door takes of the id of a link through
/// it, the rest of the other.
inline constexpr std::uint32_t kThroughIdCoordinateBits = 16;
inline constexpr std::uint32_t kThroughIdCoordinateMask = (std::uint32_t{1} << kThroughIdCoordinateBits) - 1U;

/// The id of a link through the ladder or door whose middle across is `middle`, to whole units: a map's reach
/// within 16 bits. Detour keeps it with the link, and nothing else of them need be kept with a mesh.
[[nodiscard]] inline auto throughIdOf(Vec3 middle) noexcept -> std::uint32_t {
    const auto coordinate = [](float value) {
        return static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(value)));
    };
    return (std::uint32_t{coordinate(middle.x)} << kThroughIdCoordinateBits) | coordinate(middle.y);
}

/// The middle across of what the link of `id` goes through (throughIdOf), at height `height`.
[[nodiscard]] inline auto throughOfId(std::uint32_t id, float height) noexcept -> Vec3 {
    return Vec3{.x = static_cast<float>(static_cast<std::int16_t>(id >> kThroughIdCoordinateBits)),
                .y = static_cast<float>(static_cast<std::int16_t>(id & kThroughIdCoordinateMask)),
                .z = height};
}

/// What the id of a link over a gap keeps of the ground a body leaves its start from (Takeoff), the floor
/// behind it being the mesh's to tell: that it is one, whether only a crouched body stands there, and how
/// high a body rises there, in tenths of a unit, which a jump's height is given to.
inline constexpr std::uint32_t kRiseIdBits = 16;
inline constexpr std::uint32_t kRiseIdMask = (std::uint32_t{1} << kRiseIdBits) - 1U;
inline constexpr float kRiseIdUnitsPerUnit = 10.0F;
inline constexpr std::uint32_t kCrouchedIdBit = std::uint32_t{1} << kRiseIdBits;
inline constexpr std::uint32_t kTakeoffIdBit = kCrouchedIdBit << 1U;

/// The id of a link over a gap whose start a body rises `rise` from, only crouched where `isCrouched`.
[[nodiscard]] inline auto takeoffIdOf(HammerUnits rise, bool isCrouched) noexcept -> std::uint32_t {
    const float tenths =
        std::clamp(std::round(rise.value() * kRiseIdUnitsPerUnit), 0.0F, static_cast<float>(kRiseIdMask));
    return kTakeoffIdBit | (isCrouched ? kCrouchedIdBit : 0U) | static_cast<std::uint32_t>(tenths);
}

/// The ground the start of the link of `id` is, of a link over a gap (takeoffIdOf), its floor going on `room`
/// behind it; none for another link's.
[[nodiscard]] inline auto takeoffOfId(std::uint32_t id, HammerUnits room) noexcept -> std::optional<Takeoff> {
    if ((id & kTakeoffIdBit) == 0) {
        return std::nullopt;
    }
    return Takeoff{.room = room,
                   .rise = HammerUnits{static_cast<float>(id & kRiseIdMask) / kRiseIdUnitsPerUnit},
                   .isCrouched = (id & kCrouchedIdBit) != 0};
}

/// The flag of the polygons agents may cross: every one. A door closes no polygon of a mesh: a way's filter
/// keeps off the links through it as it stands (PathRequest::gates).
inline constexpr std::uint16_t kWalkablePolygon = 1;

/// Detour's 32-bit references number a tile and a polygon of it in 22 bits, the rest the salt, at least
/// the 10 dtNavMesh::init asks; the tiles take the bits their count needs, up to 14, as Recast's samples
/// do, and the polygons of each the rest.
inline constexpr std::uint32_t kTileAndPolygonBits = 22;
inline constexpr std::uint32_t kMostTileBits = 14;

/// A tile Recast built: where it is in the grid, how many polygons Detour holds of it, one of them for each
/// of its links, and Detour's data of it.
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
