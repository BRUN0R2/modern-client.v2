// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// Recast, and Detour's builder of a tile's data, stay in this unit (docs/02-coding-standards.md, section
// 2): no interface shows their types.
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

module nx.navigation;

import std;
import nx.core;
import nx.math;
import nx.jobs;

namespace nx::navigation::detail {

namespace {

/// The cells a tile reads past each side of its own besides the agent's radius, for the walls and
/// ledges near its edges: those Recast's watershed looks across (its samples' 3).
constexpr int kBorderPadCells = 3;

/// A Recast build context that keeps the last error Recast logs, for the failure's detail.
class RecastLog final : public rcContext {
public:
    RecastLog() : rcContext{true} { enableTimer(false); }

    [[nodiscard]] auto lastError() const noexcept -> std::string_view { return m_lastError; }

protected:
    void doLog(const rcLogCategory category, const char* message, const int length) override {
        if (category == RC_LOG_ERROR) {
            m_lastError.assign(message, narrow<std::size_t>(length));
        }
    }

private:
    std::string m_lastError;
};

/// What frees each kind of Recast's results.
template <auto Free>
struct RecastFree {
    template <class Allocated>
    void operator()(Allocated* allocated) const noexcept {
        Free(allocated);
    }
};

using Heightfield = std::unique_ptr<rcHeightfield, RecastFree<&rcFreeHeightField>>;
using CompactHeightfield = std::unique_ptr<rcCompactHeightfield, RecastFree<&rcFreeCompactHeightfield>>;
using ContourSet = std::unique_ptr<rcContourSet, RecastFree<&rcFreeContourSet>>;
using PolygonMesh = std::unique_ptr<rcPolyMesh, RecastFree<&rcFreePolyMesh>>;
using DetailMesh = std::unique_ptr<rcPolyMeshDetail, RecastFree<&rcFreePolyMeshDetail>>;

/// The bounds of a triangle across, in Detour's space (its x and z).
struct Footprint {
    float minX = 0.0F;
    float minZ = 0.0F;
    float maxX = 0.0F;
    float maxZ = 0.0F;
};

/// A profile and settings in Recast's terms: voxels and cells.
struct VoxelProfile {
    int crouchedVoxels = 0;
    int standingVoxels = 0;
    int climbVoxels = 0;
    int radiusCells = 0;
    int borderCells = 0;
    int edgeCells = 0;
    float edgeErrorCells = 0.0F;
    int islandArea = 0; ///< In cells.
    int regionArea = 0; ///< In cells.
};

[[nodiscard]] auto voxelProfileOf(const AgentProfile& profile, const BuildSettings& settings)
    -> VoxelProfile {
    const float cell = settings.cellSize.value();
    const float height = settings.cellHeight.value();
    const auto island = static_cast<int>(settings.smallestIsland.value() / cell);
    const auto region = static_cast<int>(settings.smallestRegion.value() / cell);
    const auto radius = static_cast<int>(std::ceil(profile.radius.value() / cell));
    return VoxelProfile{
        .crouchedVoxels = static_cast<int>(std::ceil(profile.crouchedHeight.value() / height)),
        .standingVoxels = static_cast<int>(std::ceil(profile.standingHeight.value() / height)),
        .climbVoxels = static_cast<int>(std::floor(profile.stepHeight.value() / height)),
        .radiusCells = radius,
        .borderCells = radius + kBorderPadCells,
        .edgeCells = static_cast<int>(settings.longestEdge.value() / cell),
        .edgeErrorCells = settings.edgeError.value() / cell,
        .islandArea = island * island,
        .regionArea = region * region};
}

/// What every tile is built from: the geometry in Detour's space, each triangle's area (none where it
/// is no floor for the profile) and footprint, the grid of tiles over it, and the profile in voxels.
struct TileInput {
    std::vector<float> vertices{};
    std::vector<int> corners{};
    std::vector<std::uint8_t> areas{};
    std::vector<Footprint> footprints{};
    std::array<float, 3> low{};
    std::array<float, 3> high{};
    const AgentProfile* profile = nullptr;
    const BuildSettings* settings = nullptr;
    VoxelProfile voxels{};
};

/// The area of `triangle` of `geometry` for `profile`: its kind's when it is flat enough to stand on,
/// else none, a wall or a ceiling.
[[nodiscard]] auto areaOf(const Geometry& geometry, const GeometryTriangle& triangle,
                          const AgentProfile& profile) -> std::uint8_t {
    const Vec3 a = geometry.vertices[triangle.corners[0]];
    const Vec3 normal =
        cross(geometry.vertices[triangle.corners[1]] - a, geometry.vertices[triangle.corners[2]] - a);
    const float size = length(normal);
    return size > 0.0F && normal.z >= profile.minGroundNormalZ * size ? areaIdOf(triangle.kind)
                                                                      : RC_NULL_AREA;
}

/// What the tiles of `geometry` are built from. Errors: InvalidGeometry, TooLarge.
[[nodiscard]] auto tileInputOf(const Geometry& geometry, const AgentProfile& profile,
                               const BuildSettings& settings) -> Result<TileInput, MeshBuildError> {
    TileInput input{.profile = &profile, .settings = &settings, .voxels = voxelProfileOf(profile, settings)};
    input.vertices.reserve(geometry.vertices.size() * 3);
    for (const Vec3 vertex : geometry.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) {
            return fail(MeshBuildError::InvalidGeometry, std::format("a vertex at {}", vertex));
        }
        std::ranges::copy(detourOf(vertex), std::back_inserter(input.vertices));
    }
    input.corners.reserve(geometry.triangles.size() * 3);
    input.areas.reserve(geometry.triangles.size());
    input.footprints.reserve(geometry.triangles.size());
    input.low.fill(std::numeric_limits<float>::max());
    input.high.fill(std::numeric_limits<float>::lowest());
    for (const GeometryTriangle& triangle : geometry.triangles) {
        Footprint footprint{.minX = std::numeric_limits<float>::max(),
                            .minZ = std::numeric_limits<float>::max(),
                            .maxX = std::numeric_limits<float>::lowest(),
                            .maxZ = std::numeric_limits<float>::lowest()};
        for (const std::uint32_t corner : triangle.corners) {
            if (corner >= geometry.vertices.size()) {
                return fail(
                    MeshBuildError::InvalidGeometry,
                    std::format("a triangle's corner {} of {} vertices", corner, geometry.vertices.size()));
            }
            input.corners.push_back(narrow<int>(corner));
            const std::array<float, 3> point = detourOf(geometry.vertices[corner]);
            for (std::size_t axis = 0; axis < point.size(); ++axis) {
                input.low[axis] = std::min(input.low[axis], point[axis]);
                input.high[axis] = std::max(input.high[axis], point[axis]);
            }
            footprint = Footprint{.minX = std::min(footprint.minX, point[0]),
                                  .minZ = std::min(footprint.minZ, point[2]),
                                  .maxX = std::max(footprint.maxX, point[0]),
                                  .maxZ = std::max(footprint.maxZ, point[2])};
        }
        input.areas.push_back(areaOf(geometry, triangle, profile));
        input.footprints.push_back(footprint);
    }
    const float tallest = static_cast<float>(RC_SPAN_MAX_HEIGHT) * settings.cellHeight.value();
    if (!geometry.triangles.empty() && input.high[1] - input.low[1] >= tallest) {
        return fail(MeshBuildError::TooLarge,
                    std::format("the geometry is {} units tall, Recast's voxels reach {}",
                                input.high[1] - input.low[1], tallest));
    }
    return input;
}

[[nodiscard]] auto refused(const RecastLog& log, std::int32_t tileX, std::int32_t tileY,
                           std::string_view step) -> std::unexpected<Failure<MeshBuildError>> {
    return fail(MeshBuildError::TileRefused,
                std::format("tile {}, {}: {}: {}", tileX, tileY, step,
                            log.lastError().empty() ? std::string_view{"out of memory"} : log.lastError()));
}

/// Where only crouching fits, among the spans of floor of `compact`: less room above them than
/// `standingVoxels`.
void markCrouching(rcCompactHeightfield& compact, int standingVoxels) {
    const auto count = narrow<std::size_t>(compact.spanCount);
    const std::span spans{compact.spans, count};
    const std::span areas{compact.areas, count};
    for (std::size_t span = 0; span < count; ++span) {
        if (areas[span] != RC_NULL_AREA && std::cmp_less(spans[span].h, standingVoxels)) {
            areas[span] = areaIdOf(AreaKind::Crouch);
        }
    }
}

/// The bounds of tile `tileX`, `tileY` with the cells it reads past its sides, from `input.low` up.
struct TileBounds {
    std::array<float, 3> low{};
    std::array<float, 3> high{};
};

[[nodiscard]] auto boundsOf(const TileInput& input, std::int32_t tileX, std::int32_t tileY) -> TileBounds {
    const float cell = input.settings->cellSize.value();
    const auto tileCells = static_cast<float>(input.settings->tileCells);
    const auto border = static_cast<float>(input.voxels.borderCells);
    const float tileSide = tileCells * cell;
    TileBounds bounds{.low = input.low, .high = input.high};
    bounds.low[0] = input.low[0] + (static_cast<float>(tileX) * tileSide) - (border * cell);
    bounds.low[2] = input.low[2] + (static_cast<float>(tileY) * tileSide) - (border * cell);
    bounds.high[0] = input.low[0] + (static_cast<float>(tileX + 1) * tileSide) + (border * cell);
    bounds.high[2] = input.low[2] + (static_cast<float>(tileY + 1) * tileSide) + (border * cell);
    return bounds;
}

/// The voxels of tile `tileX`, `tileY`, filtered as the profile walks: what it steps onto, what it
/// cannot stand at the edge of, and where it has no room. None without a triangle there. Errors:
/// TileRefused.
[[nodiscard]] auto rasterize(RecastLog& log, const TileInput& input, std::int32_t tileX, std::int32_t tileY)
    -> Result<std::optional<Heightfield>, MeshBuildError> {
    const TileBounds bounds = boundsOf(input, tileX, tileY);
    std::vector<int> corners;
    std::vector<std::uint8_t> areas;
    for (std::size_t triangle = 0; triangle < input.footprints.size(); ++triangle) {
        const Footprint& footprint = input.footprints[triangle];
        if (footprint.maxX < bounds.low[0] || footprint.minX > bounds.high[0] ||
            footprint.maxZ < bounds.low[2] || footprint.minZ > bounds.high[2]) {
            continue;
        }
        const auto first = input.corners.begin() + narrow<std::ptrdiff_t>(triangle * 3);
        corners.insert(corners.end(), first, first + 3);
        areas.push_back(input.areas[triangle]);
    }
    if (areas.empty()) {
        return std::optional<Heightfield>{};
    }
    const int side = narrow<int>(input.settings->tileCells) + (2 * input.voxels.borderCells);
    Heightfield heightfield{rcAllocHeightfield()};
    if (!heightfield ||
        !rcCreateHeightfield(&log, *heightfield, side, side, bounds.low.data(), bounds.high.data(),
                             input.settings->cellSize.value(), input.settings->cellHeight.value())) {
        return refused(log, tileX, tileY, "its heightfield");
    }
    const int climb = input.voxels.climbVoxels;
    if (!rcRasterizeTriangles(&log, input.vertices.data(), narrow<int>(input.vertices.size() / 3),
                              corners.data(), areas.data(), narrow<int>(areas.size()), *heightfield, climb)) {
        return refused(log, tileX, tileY, "its triangles");
    }
    rcFilterLowHangingWalkableObstacles(&log, climb, *heightfield);
    rcFilterLedgeSpans(&log, input.voxels.crouchedVoxels, climb, *heightfield);
    rcFilterWalkableLowHeightSpans(&log, input.voxels.crouchedVoxels, *heightfield);
    return std::optional{std::move(heightfield)};
}

/// Detour's data of the polygons of `polygons` and `detail`, tile `tileX`, `tileY`. Errors: TileRefused.
[[nodiscard]] auto tileDataOf(const TileInput& input, rcPolyMesh& polygons, const rcPolyMeshDetail& detail,
                              std::int32_t tileX, std::int32_t tileY) -> Result<BuiltTile, MeshBuildError> {
    const auto polygonCount = narrow<std::size_t>(polygons.npolys);
    std::ranges::fill(std::span{polygons.flags, polygonCount}, kWalkablePolygon);
    dtNavMeshCreateParams params{};
    params.verts = polygons.verts;
    params.vertCount = polygons.nverts;
    params.polys = polygons.polys;
    params.polyAreas = polygons.areas;
    params.polyFlags = polygons.flags;
    params.polyCount = polygons.npolys;
    params.nvp = polygons.nvp;
    params.detailMeshes = detail.meshes;
    params.detailVerts = detail.verts;
    params.detailVertsCount = detail.nverts;
    params.detailTris = detail.tris;
    params.detailTriCount = detail.ntris;
    params.walkableHeight = input.profile->crouchedHeight.value();
    params.walkableRadius = input.profile->radius.value();
    params.walkableClimb = input.profile->stepHeight.value();
    params.tileX = tileX;
    params.tileY = tileY;
    std::ranges::copy(polygons.bmin, std::begin(params.bmin));
    std::ranges::copy(polygons.bmax, std::begin(params.bmax));
    params.cs = polygons.cs;
    params.ch = polygons.ch;
    params.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &data, &size)) {
        return fail(MeshBuildError::TileRefused,
                    std::format("tile {}, {}: Detour takes no tile of {} vertices and {} polygons", tileX,
                                tileY, polygons.nverts, polygons.npolys));
    }
    const std::span bytes{data, narrow<std::size_t>(size)};
    BuiltTile tile{.x = tileX, .y = tileY, .polygonCount = narrow<std::uint32_t>(polygonCount)};
    tile.data.resize(bytes.size());
    std::ranges::copy(std::as_bytes(bytes), tile.data.begin());
    dtFree(data);
    return tile;
}

/// Tile `tileX`, `tileY` of `input`, as Recast builds it: its voxels, the floor the profile's radius
/// keeps away from walls and ledges, its regions, their outlines, the polygons of those and their
/// details. None when it holds no floor. Errors: TileRefused.
[[nodiscard]] auto buildTile(const TileInput& input, std::int32_t tileX, std::int32_t tileY)
    -> Result<std::optional<BuiltTile>, MeshBuildError> {
    RecastLog log;
    auto rasterized = rasterize(log, input, tileX, tileY);
    if (!rasterized) {
        return std::unexpected{std::move(rasterized.error())};
    }
    if (!*rasterized) {
        return std::optional<BuiltTile>{};
    }
    Heightfield heightfield = std::move(**rasterized);
    const VoxelProfile& voxels = input.voxels;
    CompactHeightfield compact{rcAllocCompactHeightfield()};
    if (!compact ||
        !rcBuildCompactHeightfield(&log, voxels.crouchedVoxels, voxels.climbVoxels, *heightfield, *compact)) {
        return refused(log, tileX, tileY, "its compact heightfield");
    }
    heightfield.reset(); // what is left is in the compact one
    if (!rcErodeWalkableArea(&log, voxels.radiusCells, *compact)) {
        return refused(log, tileX, tileY, "its floor kept from walls");
    }
    markCrouching(*compact, voxels.standingVoxels);
    if (!rcBuildDistanceField(&log, *compact) ||
        !rcBuildRegions(&log, *compact, voxels.borderCells, voxels.islandArea, voxels.regionArea)) {
        return refused(log, tileX, tileY, "its regions");
    }
    ContourSet contours{rcAllocContourSet()};
    if (!contours || !rcBuildContours(&log, *compact, voxels.edgeErrorCells, voxels.edgeCells, *contours)) {
        return refused(log, tileX, tileY, "its outlines");
    }
    if (contours->nconts == 0) {
        return std::optional<BuiltTile>{};
    }
    PolygonMesh polygons{rcAllocPolyMesh()};
    if (!polygons || !rcBuildPolyMesh(&log, *contours, DT_VERTS_PER_POLYGON, *polygons)) {
        return refused(log, tileX, tileY, "its polygons");
    }
    DetailMesh detail{rcAllocPolyMeshDetail()};
    if (!detail ||
        !rcBuildPolyMeshDetail(&log, *polygons, *compact, input.settings->detailSampleDistance.value(),
                               input.settings->detailHeightError.value(), *detail)) {
        return refused(log, tileX, tileY, "its polygons' details");
    }
    if (polygons->npolys == 0) {
        return std::optional<BuiltTile>{};
    }
    return tileDataOf(input, *polygons, *detail, tileX, tileY).transform([](BuiltTile tile) {
        return std::optional{std::move(tile)};
    });
}

} // namespace

auto buildTiles(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                const BuildSettings& settings) -> Result<BuiltTiles, MeshBuildError> {
    auto input = tileInputOf(geometry, profile, settings);
    if (!input) {
        return std::unexpected{std::move(input.error())};
    }
    const float tileSide = static_cast<float>(settings.tileCells) * settings.cellSize.value();
    BuiltTiles built{.origin = input->low, .tileSide = tileSide};
    if (geometry.triangles.empty()) {
        built.origin = {};
        return built;
    }
    int cellsAcross = 0;
    int cellsDown = 0;
    rcCalcGridSize(input->low.data(), input->high.data(), settings.cellSize.value(), &cellsAcross,
                   &cellsDown);
    const int tileCells = narrow<int>(settings.tileCells);
    built.tilesAcross = (cellsAcross + tileCells - 1) / tileCells;
    built.tilesDown = (cellsDown + tileCells - 1) / tileCells;
    const auto tileCount = narrow<std::size_t>(built.tilesAcross) * narrow<std::size_t>(built.tilesDown);
    if (tileCount > (std::size_t{1} << kMostTileBits)) {
        return fail(MeshBuildError::TooLarge,
                    std::format("{} by {} tiles, more than Detour's {}", built.tilesAcross, built.tilesDown,
                                std::size_t{1} << kMostTileBits));
    }
    // Each tile in its own job, its result at its own index: the same mesh with any number of workers.
    std::vector<Result<std::optional<BuiltTile>, MeshBuildError>> results(tileCount);
    const std::span<Result<std::optional<BuiltTile>, MeshBuildError>> resultView = results;
    const TileInput& tileInput = *input;
    const std::int32_t tilesAcross = built.tilesAcross;
    jobs.wait(
        jobs.parallelFor({.label = "navigation.tile"}, std::views::iota(std::size_t{0}, tileCount),
                         jobs::Grain::automatic(), [&tileInput, resultView, tilesAcross](std::size_t index) {
                             const auto tile = narrow<std::int32_t>(index);
                             resultView[index] = buildTile(tileInput, tile % tilesAcross, tile / tilesAcross);
                         }));
    for (auto& result : results) {
        if (!result) {
            return std::unexpected{std::move(result.error())};
        }
        if (*result) {
            built.tiles.push_back(std::move(**result));
        }
    }
    return built;
}

} // namespace nx::navigation::detail
