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

/// A Recast build context that keeps the last error Recast logs, for the failure's detail.
class RecastLog final : public rcContext {
public:
    RecastLog() : rcContext{true} { enableTimer(false); }

    [[nodiscard]] auto lastError() const noexcept -> std::string_view { return lastErrorText; }

protected:
    void doLog(const rcLogCategory category, const char* message, const int length) override {
        if (category == RC_LOG_ERROR) {
            lastErrorText.assign(message, narrow<std::size_t>(length));
        }
    }

private:
    std::string lastErrorText;
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
    int clearanceCells = 0; ///< Of the wall clearance.
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
    const auto island =
        static_cast<int>(static_cast<double>(settings.smallestIsland.value()) / static_cast<double>(cell));
    const auto region =
        static_cast<int>(static_cast<double>(settings.smallestRegion.value()) / static_cast<double>(cell));
    const auto radius =
        static_cast<int>(std::ceil(static_cast<double>(profile.radius.value()) / static_cast<double>(cell)));
    const auto clearance = static_cast<int>(
        std::ceil(static_cast<double>(profile.wallClearance.value()) / static_cast<double>(cell)));
    return VoxelProfile{
        .crouchedVoxels = static_cast<int>(std::ceil(profile.crouchedHeight.value() / height)),
        .standingVoxels = static_cast<int>(std::ceil(profile.standingHeight.value() / height)),
        .climbVoxels = static_cast<int>(
            std::floor(static_cast<double>(profile.stepHeight.value()) / static_cast<double>(height))),
        .radiusCells = radius,
        .clearanceCells = clearance,
        // A tile reads past its sides as far as its floor is eroded from walls, so that the erosion
        // at its sides is its neighbors' too.
        .borderCells = radius + clearance + borderPadCells,
        .edgeCells =
            static_cast<int>(static_cast<double>(settings.longestEdge.value()) / static_cast<double>(cell)),
        .edgeErrorCells = settings.edgeError.value() / cell,
        .islandArea = island * island,
        .regionArea = region * region};
}

/// What every tile is built from: the geometry in Detour's space, each triangle's area (none where it
/// is no floor for the profile) and footprint, the grid of tiles over it, and the profile and settings,
/// as given and in voxels. Its own copies: the jobs that read it may run after its geometry is gone.
struct TileInput {
    std::vector<float> vertices{};
    std::vector<int> corners{};
    std::vector<std::uint8_t> areas{};
    std::vector<Footprint> footprints{};
    std::array<float, 3> low{};
    std::array<float, 3> high{};
    AgentProfile profile{};
    BuildSettings settings{};
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
    TileInput input{.profile = profile, .settings = settings, .voxels = voxelProfileOf(profile, settings)};
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
    if (!geometry.triangles.empty()) {
        for (const std::size_t axis : {std::size_t{0}, std::size_t{2}}) {
            const double cells =
                (static_cast<double>(input.high[axis]) - static_cast<double>(input.low[axis])) /
                static_cast<double>(settings.cellSize.value());
            if (cells + settings.tileCells > std::numeric_limits<int>::max()) {
                return fail(MeshBuildError::TooLarge, "the geometry exceeds Recast's integer grid range");
            }
        }
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

/// Marks the floor of `compact`, eroded by the profile's radius already, that lies within `clearanceCells`
/// of where the profile may not stand as the kind Edge: Recast erodes a copy of the areas that much farther,
/// and each span of floor the copy loses lies along a wall or a ledge. Crouching and water keep their
/// kinds. Returns whether Recast eroded the copy.
[[nodiscard]] auto markEdges(RecastLog& log, rcCompactHeightfield& compact, int clearanceCells) -> bool {
    if (clearanceCells <= 0) {
        return true;
    }
    const auto count = narrow<std::size_t>(compact.spanCount);
    const std::span areas{compact.areas, count};
    const std::vector<std::uint8_t> kept(areas.begin(), areas.end());
    if (!rcErodeWalkableArea(&log, clearanceCells, compact)) {
        return false;
    }
    for (std::size_t span = 0; span < count; ++span) {
        const bool isAlongWall = areas[span] == RC_NULL_AREA && kept[span] == areaIdOf(AreaKind::Floor);
        areas[span] = isAlongWall ? areaIdOf(AreaKind::Edge) : kept[span];
    }
    return true;
}

/// The bounds of tile `tileX`, `tileY` with the cells it reads past its sides, from `input.low` up.
struct TileBounds {
    std::array<float, 3> low{};
    std::array<float, 3> high{};
};

[[nodiscard]] auto boundsOf(const TileInput& input, std::int32_t tileX, std::int32_t tileY) -> TileBounds {
    const float cell = input.settings.cellSize.value();
    const auto tileCells = static_cast<float>(input.settings.tileCells);
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
    const int side = narrow<int>(input.settings.tileCells) + (2 * input.voxels.borderCells);
    Heightfield heightfield{rcAllocHeightfield()};
    if (!heightfield ||
        !rcCreateHeightfield(&log, *heightfield, side, side, bounds.low.data(), bounds.high.data(),
                             input.settings.cellSize.value(), input.settings.cellHeight.value())) {
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

/// How far apart the links along a ledge are, in the profile's radii: a body's two widths, so that one is
/// within a width of anywhere along it.
constexpr float kLinkSpacingRadii = 4.0F;
/// What a ray's direction and a triangle's plane are taken as parallel below, by Möller and Trumbore's
/// determinant.
constexpr float kParallel = 1.0e-6F;
/// What a triangle's area across is taken as a wall's below: a ray down meets no such triangle.
constexpr float kFlatEnough = 1.0e-4F;

/// A triangle of the geometry about a tile, in the game's space, its bounds across, and its area:
/// Recast's, none for a wall or a ceiling (areaOf).
struct NearTriangle {
    std::array<Vec3, 3> corners{};
    Vec3 low{};
    Vec3 high{};
    std::uint8_t area = RC_NULL_AREA;
};

/// The triangles of `input` whose footprints come within `margin` across of the tile `bounds` holds.
[[nodiscard]] auto trianglesAbout(const TileInput& input, const TileBounds& bounds, float margin)
    -> std::vector<NearTriangle> {
    std::vector<NearTriangle> near;
    for (std::size_t triangle = 0; triangle < input.footprints.size(); ++triangle) {
        const Footprint& footprint = input.footprints[triangle];
        if (footprint.maxX < bounds.low[0] - margin || footprint.minX > bounds.high[0] + margin ||
            footprint.maxZ < bounds.low[2] - margin || footprint.minZ > bounds.high[2] + margin) {
            continue;
        }
        NearTriangle found{.low = Vec3{.x = std::numeric_limits<float>::max(),
                                       .y = std::numeric_limits<float>::max(),
                                       .z = std::numeric_limits<float>::max()},
                           .high = Vec3{.x = std::numeric_limits<float>::lowest(),
                                        .y = std::numeric_limits<float>::lowest(),
                                        .z = std::numeric_limits<float>::lowest()},
                           .area = input.areas[triangle]};
        for (std::size_t corner = 0; corner < found.corners.size(); ++corner) {
            const auto vertex =
                narrow<std::size_t>(input.corners[(triangle * found.corners.size()) + corner]);
            const Vec3 point = positionOf(std::span<const float, 3>{input.vertices.data() + (vertex * 3), 3});
            found.corners[corner] = point;
            found.low = componentMin(found.low, point);
            found.high = componentMax(found.high, point);
        }
        near.push_back(found);
    }
    return near;
}

/// Whether the segment from `from` to `to` meets `triangle` (Möller and Trumbore's test).
[[nodiscard]] auto meets(Vec3 from, Vec3 to, const NearTriangle& triangle) -> bool {
    const Vec3 low = componentMin(from, to);
    const Vec3 high = componentMax(from, to);
    if (triangle.high.x < low.x || triangle.low.x > high.x || triangle.high.y < low.y ||
        triangle.low.y > high.y || triangle.high.z < low.z || triangle.low.z > high.z) {
        return false;
    }
    const auto& [a, b, c] = triangle.corners;
    const Vec3 way = to - from;
    const Vec3 side = b - a;
    const Vec3 other = c - a;
    const Vec3 across = cross(way, other);
    const float determinant = dot(side, across);
    if (std::abs(determinant) < kParallel) {
        return false;
    }
    const float inverse = 1.0F / determinant;
    const Vec3 fromCorner = from - a;
    const float u = dot(fromCorner, across) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return false;
    }
    const Vec3 up = cross(fromCorner, side);
    const float v = dot(way, up) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return false;
    }
    const float along = dot(other, up) * inverse;
    return along >= 0.0F && along <= 1.0F;
}

/// The height of `triangle` above `point` across, when it is over it and not a wall's; none.
[[nodiscard]] auto heightAbove(Vec3 point, const NearTriangle& triangle) -> std::optional<float> {
    if (point.x < triangle.low.x || point.x > triangle.high.x || point.y < triangle.low.y ||
        point.y > triangle.high.y) {
        return std::nullopt;
    }
    const auto& [a, b, c] = triangle.corners;
    const float area = ((b.x - a.x) * (c.y - a.y)) - ((c.x - a.x) * (b.y - a.y));
    if (std::abs(area) < kFlatEnough) {
        return std::nullopt;
    }
    const float u = (((b.x - point.x) * (c.y - point.y)) - ((c.x - point.x) * (b.y - point.y))) / area;
    const float v = (((c.x - point.x) * (a.y - point.y)) - ((a.x - point.x) * (c.y - point.y))) / area;
    const float w = 1.0F - u - v;
    if (u < 0.0F || v < 0.0F || w < 0.0F) {
        return std::nullopt;
    }
    return (u * a.z) + (v * b.z) + (w * c.z);
}

/// The links of a tile, as Detour's off-mesh connections take them: each one's start and end one after
/// another, in Detour's space, its radius, flags, area and way.
struct TileLinks {
    std::vector<float> points{};
    std::vector<float> radii{};
    std::vector<std::uint16_t> flags{};
    std::vector<std::uint8_t> areas{};
    std::vector<std::uint8_t> ways{};
    std::vector<std::uint32_t> ids{};

    void add(Vec3 start, Vec3 end, float radius, AreaKind kind) {
        std::ranges::copy(detourOf(start), std::back_inserter(points));
        std::ranges::copy(detourOf(end), std::back_inserter(points));
        radii.push_back(radius);
        flags.push_back(kWalkablePolygon);
        areas.push_back(areaIdOf(kind));
        ways.push_back(kind == AreaKind::Jump ? std::uint8_t{DT_OFFMESH_CON_BIDIR} : std::uint8_t{0});
        ids.push_back(narrow<std::uint32_t>(ids.size()));
    }
};

/// The height of the highest triangle of `near` over `point` within `lowest` and `highest`, when it is a
/// floor the profile stands on; none when there is none, or the highest is no such floor (a slope too
/// steep, a ceiling).
[[nodiscard]] auto floorAt(Vec3 point, std::span<const NearTriangle> near, float lowest, float highest)
    -> std::optional<float> {
    std::optional<float> top;
    std::uint8_t topArea = RC_NULL_AREA;
    for (const NearTriangle& triangle : near) {
        const std::optional<float> height = heightAbove(point, triangle);
        if (height && *height >= lowest && *height <= highest && (!top || *height > *top)) {
            top = height;
            topArea = triangle.area;
        }
    }
    return topArea != RC_NULL_AREA ? top : std::nullopt;
}

/// The link of a body leaving the floor at `start`, across the ledge its outward way `outward` faces, to
/// the floor below `near` holds, the profile of `input` dropping from it unhurt: of the kind Jump, both
/// ways, within the jump height, else Drop; none where a wall or a step is there instead, or no floor it
/// lands on. It lands its radius and a cell past where the floor ends, the mesh's floor below starting its
/// radius from the ledge, so that it lands on the mesh. How high the ledge is is the geometry's, on both
/// sides: a polygon's corners stand up to a cell above the floor they cover.
[[nodiscard]] auto linkFrom(Vec3 start, Vec3 outward, const TileInput& input,
                            std::span<const NearTriangle> near) -> std::optional<std::pair<Vec3, AreaKind>> {
    const AgentProfile& profile = input.profile;
    const float step = profile.stepHeight.value();
    // The floor it leaves within a step of its polygon's corner, as Detour finds the polygon a link starts
    // on (walkableClimb).
    const std::optional<float> ledge = floorAt(start, near, start.z - step, start.z + step);
    if (!ledge) {
        return std::nullopt;
    }
    const Vec3 leaves{.x = start.x, .y = start.y, .z = *ledge};
    const float across = profile.radius.value() + profile.radius.value() + input.settings.cellSize.value();
    const Vec3 beyond = leaves + (outward * across);
    // A wall across the way, above a step, is a wall's edge, no ledge.
    const Vec3 aboveStep{.z = step + input.settings.cellHeight.value()};
    if (std::ranges::any_of(near, [&](const NearTriangle& triangle) {
            return meets(leaves + aboveStep, beyond + aboveStep, triangle);
        })) {
        return std::nullopt;
    }
    // The highest floor below, within a step up and the drop height down.
    const std::optional<float> landing =
        floorAt(beyond, near, *ledge - profile.dropHeight.value(), *ledge + step);
    if (!landing || *ledge - *landing <= step) {
        return std::nullopt;
    }
    const AreaKind kind = *ledge - *landing <= profile.jumpHeight.value() ? AreaKind::Jump : AreaKind::Drop;
    return std::pair{Vec3{.x = beyond.x, .y = beyond.y, .z = *landing}, kind};
}

/// The links of the polygons of `polygons`, tile `tileX`, `tileY` of `input`: along each edge of a polygon
/// no other polygon shares (a wall's or a ledge's, not the side of the tile), one a body's two widths or
/// less apart, its middle first, where a body walking off the edge lands on floor below (linkFrom).
[[nodiscard]] auto linksOf(const TileInput& input, const rcPolyMesh& polygons, std::int32_t tileX,
                           std::int32_t tileY) -> TileLinks {
    const AgentProfile& profile = input.profile;
    const float spacing = profile.radius.value() * kLinkSpacingRadii;
    const float reach = profile.radius.value() + profile.radius.value() + input.settings.cellSize.value();
    const std::vector<NearTriangle> near = trianglesAbout(input, boundsOf(input, tileX, tileY), reach);
    const auto vertexOf = [&polygons](std::uint16_t vertex) {
        const std::array<float, 3> point{
            polygons.bmin[0] + (static_cast<float>(polygons.verts[(vertex * 3) + 0]) * polygons.cs),
            polygons.bmin[1] + (static_cast<float>(polygons.verts[(vertex * 3) + 1]) * polygons.ch),
            polygons.bmin[2] + (static_cast<float>(polygons.verts[(vertex * 3) + 2]) * polygons.cs)};
        return positionOf(point);
    };
    const auto sides = narrow<std::size_t>(polygons.nvp);
    TileLinks links;
    for (std::size_t polygon = 0; polygon < narrow<std::size_t>(polygons.npolys); ++polygon) {
        const std::span<const std::uint16_t> record{polygons.polys + (polygon * sides * 2), sides * 2};
        const std::span<const std::uint16_t> corners = record.first(sides);
        const auto count = static_cast<std::size_t>(
            std::ranges::find(corners, std::uint16_t{RC_MESH_NULL_IDX}) - corners.begin());
        Vec3 middle{};
        for (std::size_t corner = 0; corner < count; ++corner) {
            middle = middle + vertexOf(record[corner]);
        }
        middle = middle / static_cast<float>(count);
        for (std::size_t edge = 0; edge < count; ++edge) {
            if (record[sides + edge] != RC_MESH_NULL_IDX) {
                continue; // shared, or the side of the tile
            }
            const Vec3 from = vertexOf(record[edge]);
            const Vec3 to = vertexOf(record[(edge + 1) % count]);
            const Vec3 along{.x = to.x - from.x, .y = to.y - from.y};
            const float length = nx::length(along);
            if (length <= 0.0F) {
                continue;
            }
            Vec3 outward{.x = along.y / length, .y = -along.x / length};
            const Vec3 fromMiddle = ((from + to) * 0.5F) - middle;
            if ((outward.x * fromMiddle.x) + (outward.y * fromMiddle.y) < 0.0F) {
                outward = outward * -1.0F;
            }
            const auto samples =
                std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(length / spacing)));
            for (std::size_t sample = 0; sample < samples; ++sample) {
                const float share = (static_cast<float>(sample) + 0.5F) / static_cast<float>(samples);
                const Vec3 start = from + ((to - from) * share);
                if (const auto link = linkFrom(start, outward, input, near)) {
                    links.add(start, link->first, profile.radius.value(), link->second);
                }
            }
        }
    }
    return links;
}

/// Detour's data of the polygons of `polygons` and `detail`, and of their links (linksOf), tile `tileX`,
/// `tileY`. Errors: TileRefused.
[[nodiscard]] auto tileDataOf(const TileInput& input, rcPolyMesh& polygons, const rcPolyMeshDetail& detail,
                              std::int32_t tileX, std::int32_t tileY) -> Result<BuiltTile, MeshBuildError> {
    const auto polygonCount = narrow<std::size_t>(polygons.npolys);
    std::ranges::fill(std::span{polygons.flags, polygonCount}, kWalkablePolygon);
    const TileLinks links = linksOf(input, polygons, tileX, tileY);
    dtNavMeshCreateParams params{};
    params.offMeshConVerts = links.points.data();
    params.offMeshConRad = links.radii.data();
    params.offMeshConFlags = links.flags.data();
    params.offMeshConAreas = links.areas.data();
    params.offMeshConDir = links.ways.data();
    params.offMeshConUserID = links.ids.data();
    params.offMeshConCount = narrow<int>(links.radii.size());
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
    params.walkableHeight = input.profile.crouchedHeight.value();
    params.walkableRadius = input.profile.radius.value();
    params.walkableClimb = input.profile.stepHeight.value();
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
    // Its polygons, and one for each link Detour kept, a link that starts in the tile (offMeshConCount).
    dtMeshHeader header{};
    std::memcpy(&header, data, sizeof(header));
    BuiltTile tile{.x = tileX, .y = tileY, .polygonCount = narrow<std::uint32_t>(header.polyCount)};
    tile.data.resize(bytes.size());
    std::ranges::copy(std::as_bytes(bytes), tile.data.begin());
    dtFree(data);
    return tile;
}

/// Tile `tileX`, `tileY` of `input`, as Recast builds it: its voxels, the floor the profile's radius
/// keeps away from walls and ledges, that floor along them within its wall clearance (markEdges), its
/// regions, their outlines, the polygons of those and their details. None when it holds no floor. Errors:
/// TileRefused.
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
    if (!markEdges(log, *compact, voxels.clearanceCells)) {
        return refused(log, tileX, tileY, "its floor along walls");
    }
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
        !rcBuildPolyMeshDetail(&log, *polygons, *compact, input.settings.detailSampleDistance.value(),
                               input.settings.detailHeightError.value(), *detail)) {
        return refused(log, tileX, tileY, "its polygons' details");
    }
    if (polygons->npolys == 0) {
        return std::optional<BuiltTile>{};
    }
    return tileDataOf(input, *polygons, *detail, tileX, tileY).transform([](BuiltTile tile) {
        return std::optional{std::move(tile)};
    });
}

/// The grid of tiles over `input`, with none built yet: none for no triangle. Errors: TooLarge.
[[nodiscard]] auto gridOf(const TileInput& input, bool isEmpty) -> Result<BuiltTiles, MeshBuildError> {
    const BuildSettings& settings = input.settings;
    const float tileSide = static_cast<float>(settings.tileCells) * settings.cellSize.value();
    if (isEmpty) {
        return BuiltTiles{.tileSide = tileSide};
    }
    BuiltTiles grid{.origin = input.low, .tileSide = tileSide};
    // tileInputOf checked the integer grid range. Keep the subtraction and rounding in double
    // so that a representable cell count cannot round up past INT_MAX in float.
    constexpr double halfCell = 0.5;
    const auto gridCells = [&input, &settings](std::size_t axis) {
        return static_cast<int>(
            std::floor((static_cast<double>(input.high[axis]) - static_cast<double>(input.low[axis])) /
                           static_cast<double>(settings.cellSize.value()) +
                       halfCell));
    };
    const int cellsAcross = gridCells(0);
    const int cellsDown = gridCells(2);
    const int tileCells = narrow<int>(settings.tileCells);
    grid.tilesAcross = (cellsAcross + tileCells - 1) / tileCells;
    grid.tilesDown = (cellsDown + tileCells - 1) / tileCells;
    const auto tileCount = narrow<std::size_t>(grid.tilesAcross) * narrow<std::size_t>(grid.tilesDown);
    if (tileCount > (std::size_t{1} << kMostTileBits)) {
        return fail(MeshBuildError::TooLarge,
                    std::format("{} by {} tiles, more than Detour's {}", grid.tilesAcross, grid.tilesDown,
                                std::size_t{1} << kMostTileBits));
    }
    return grid;
}

} // namespace

struct TileWork {
    TileInput input{};
    BuiltTiles grid{}; ///< Its tiles left out: they are in results.
    std::vector<Result<std::optional<BuiltTile>, MeshBuildError>> results{}; ///< Row by row.
};

auto startTiles(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                const BuildSettings& settings, jobs::Priority priority) -> Result<TileJobs, MeshBuildError> {
    auto input = tileInputOf(geometry, profile, settings);
    if (!input) {
        return std::unexpected{std::move(input.error())};
    }
    auto grid = gridOf(*input, geometry.triangles.empty());
    if (!grid) {
        return std::unexpected{std::move(grid.error())};
    }
    const auto tileCount = narrow<std::size_t>(grid->tilesAcross) * narrow<std::size_t>(grid->tilesDown);
    auto work = std::make_shared<TileWork>(
        TileWork{.input = std::move(*input), .grid = std::move(*grid), .results = {}});
    work->results.resize(tileCount);
    // Each tile in its own job, its result at its own index: the same mesh with any number of workers. The
    // work outlives the jobs: its holder waits for them before it lets it go (MeshBuild).
    TileWork* const shared = work.get();
    const jobs::JobHandle done = jobs.parallelFor(
        {.label = "navigation.tile", .priority = priority}, std::views::iota(std::size_t{0}, tileCount),
        jobs::Grain::automatic(), [shared](std::size_t index) {
            const auto tile = narrow<std::int32_t>(index);
            const std::int32_t tilesAcross = shared->grid.tilesAcross;
            shared->results[index] = buildTile(shared->input, tile % tilesAcross, tile / tilesAcross);
        });
    return TileJobs{.work = std::move(work), .done = done};
}

auto takeTiles(TileWork& work) -> Result<BuiltTiles, MeshBuildError> {
    BuiltTiles built = std::move(work.grid);
    for (auto& result : work.results) {
        if (!result) {
            return std::unexpected{std::move(result.error())};
        }
        if (*result) {
            built.tiles.push_back(std::move(**result));
        }
    }
    work = TileWork{};
    return built;
}

} // namespace nx::navigation::detail

namespace nx::navigation {

namespace {

/// The version of the tiles this unit builds, in every key (meshKeyOf): raise it whenever what a build
/// makes of the same geometry, profile and settings changes, here or with the recastnavigation port, so
/// that no mesh kept before is taken for one built now.
constexpr std::uint32_t kTilesVersion = 3;

/// The bytes a key hashes for each vertex (its three coordinates) and each triangle (its three corners
/// and its kind), to reserve them.
constexpr std::size_t kVertexKeyBytes = 3 * sizeof(float);
constexpr std::size_t kTriangleKeyBytes = (3 * sizeof(std::uint32_t)) + sizeof(AreaKind);

} // namespace

auto meshKeyOf(const Geometry& geometry, const AgentProfile& profile, const BuildSettings& settings)
    -> MeshKey {
    std::vector<std::byte> bytes;
    bytes.reserve((geometry.vertices.size() * kVertexKeyBytes) +
                  (geometry.triangles.size() * kTriangleKeyBytes));
    appendLittleEndian(bytes, kTilesVersion);
    appendLittleEndian(bytes, std::int32_t{DT_NAVMESH_VERSION});
    for (const float value :
         {profile.radius.value(), profile.wallClearance.value(), profile.standingHeight.value(),
          profile.crouchedHeight.value(), profile.stepHeight.value(), profile.jumpHeight.value(),
          profile.dropHeight.value(), profile.minGroundNormalZ}) {
        appendLittleEndian(bytes, value);
    }
    for (const float value :
         {settings.cellSize.value(), settings.cellHeight.value(), settings.smallestIsland.value(),
          settings.smallestRegion.value(), settings.longestEdge.value(), settings.edgeError.value(),
          settings.detailSampleDistance.value(), settings.detailHeightError.value()}) {
        appendLittleEndian(bytes, value);
    }
    appendLittleEndian(bytes, settings.tileCells);
    appendLittleEndian(bytes, std::uint64_t{geometry.vertices.size()});
    for (const Vec3 vertex : geometry.vertices) {
        appendLittleEndian(bytes, vertex.x);
        appendLittleEndian(bytes, vertex.y);
        appendLittleEndian(bytes, vertex.z);
    }
    appendLittleEndian(bytes, std::uint64_t{geometry.triangles.size()});
    for (const GeometryTriangle& triangle : geometry.triangles) {
        for (const std::uint32_t corner : triangle.corners) {
            appendLittleEndian(bytes, corner);
        }
        appendLittleEndian(bytes, std::to_underlying(triangle.kind));
    }
    return MeshKey{hash64(bytes)};
}

} // namespace nx::navigation
