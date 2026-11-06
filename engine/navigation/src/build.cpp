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
    int liftVoxels = 0; ///< Of a body's lift (bodyLiftOf).
    int radiusCells = 0;
    int clearanceCells = 0; ///< Of the wall clearance.
    int borderCells = 0;
    int edgeCells = 0;
    float edgeErrorCells = 0.0F;
    int islandArea = 0; ///< In cells.
    int regionArea = 0; ///< In cells.
};

/// How far above where it stands on a flat floor a body's middle may be and still stand on its floor: a step,
/// onto what it climbs, or what the steepest floor lifts its square box by, resting on a corner.
[[nodiscard]] auto bodyLiftOf(const AgentProfile& profile) noexcept -> float {
    const float normalZ = profile.minGroundNormalZ;
    const float slope = std::sqrt(1.0F - (normalZ * normalZ)) / normalZ;
    return std::max(profile.stepHeight.value(),
                    profile.radius.value() * std::numbers::sqrt2_v<float> * slope);
}

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
        .liftVoxels = static_cast<int>(std::ceil(bodyLiftOf(profile) / height)),
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
    std::vector<Aabb> ladders{};
    std::vector<Aabb> doors{};
    BodyHull bodyHull{};
    BodyHull crouchedBodyHull{};
    std::vector<PlacedBodyHull> brushBodyHulls{};
    std::vector<PlacedBodyHull> crouchedBrushBodyHulls{};
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

/// Whether `hull` keeps BodyHull's order and its planes are finite, so that a way down it ends. Errors:
/// InvalidGeometry.
[[nodiscard]] auto checkBodyHull(const BodyHull& hull) -> Result<void, MeshBuildError> {
    const auto isNamedAfter = [&hull](const BodyHullChild& child, std::size_t first) {
        const auto* index = std::get_if<std::uint32_t>(&child);
        return index == nullptr || (*index >= first && *index < hull.nodes.size());
    };
    if (!isNamedAfter(hull.root, 0)) {
        return fail(MeshBuildError::InvalidGeometry, "a body hull's root is none of its nodes");
    }
    for (std::size_t index = 0; index < hull.nodes.size(); ++index) {
        const BodyHullNode& node = hull.nodes[index];
        const Vec3 normal = node.plane.normal;
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
            !std::isfinite(node.plane.distance)) {
            return fail(MeshBuildError::InvalidGeometry,
                        std::format("a body hull's node {}: its plane is not finite", index));
        }
        if (!isNamedAfter(node.children[0], index + 1) || !isNamedAfter(node.children[1], index + 1)) {
            return fail(MeshBuildError::InvalidGeometry,
                        std::format("a body hull's node {}: a child not after it of {} nodes", index,
                                    hull.nodes.size()));
        }
    }
    return {};
}

/// What the tiles of `geometry` are built from. Errors: InvalidGeometry, TooLarge.
[[nodiscard]] auto tileInputOf(const Geometry& geometry, const AgentProfile& profile,
                               const BuildSettings& settings) -> Result<TileInput, MeshBuildError> {
    std::vector<const BodyHull*> hulls{&geometry.bodyHull, &geometry.crouchedBodyHull};
    for (const auto* const placed : {&geometry.brushBodyHulls, &geometry.crouchedBrushBodyHulls}) {
        for (const PlacedBodyHull& brush : *placed) {
            hulls.push_back(&brush.hull);
        }
    }
    for (const BodyHull* const hull : hulls) {
        if (auto checked = checkBodyHull(*hull); !checked) {
            return std::unexpected{std::move(checked.error())};
        }
    }
    TileInput input{.ladders = geometry.ladders,
                    .doors = geometry.doors,
                    .bodyHull = geometry.bodyHull,
                    .crouchedBodyHull = geometry.crouchedBodyHull,
                    .brushBodyHulls = geometry.brushBodyHulls,
                    .crouchedBrushBodyHulls = geometry.crouchedBrushBodyHulls,
                    .profile = profile,
                    .settings = settings,
                    .voxels = voxelProfileOf(profile, settings)};
    for (const Aabb& ladder : geometry.ladders) {
        const auto isFiniteAt = [](Vec3 point) {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        };
        if (!isFiniteAt(ladder.min) || !isFiniteAt(ladder.max)) {
            return fail(MeshBuildError::InvalidGeometry, std::format("a ladder at {}", ladder.min));
        }
    }
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

/// A body of a profile: what stops it, the world's hull and the brush entities', and how tall it is.
struct ProfileBody {
    const BodyHull& world;
    std::span<const PlacedBodyHull> brushes;
    HammerUnits height{};

    [[nodiscard]] auto isSolidAt(Vec3 middle) const noexcept -> bool {
        return navigation::isSolidAt(world, middle) || navigation::isSolidAt(brushes, middle);
    }
};

[[nodiscard]] auto standingBodyOf(const TileInput& input) noexcept -> ProfileBody {
    return {.world = input.bodyHull, .brushes = input.brushBodyHulls, .height = input.profile.standingHeight};
}

[[nodiscard]] auto duckedBodyOf(const TileInput& input) noexcept -> ProfileBody {
    return {.world = input.crouchedBodyHull,
            .brushes = input.crouchedBrushBodyHulls,
            .height = input.profile.crouchedHeight};
}

/// Whether `body` fits on a floor at `feet`: its middle out of the solid within its lift, the floor's own
/// solid, grown by its box, reaching above that on a slope.
[[nodiscard]] auto bodyFitsAt(const TileInput& input, const ProfileBody& body, Vec3 feet) noexcept -> bool {
    const Vec3 middle = feet + Vec3{.z = body.height.value() * 0.5F};
    const float voxelHeight = input.settings.cellHeight.value();
    for (int voxel = 0; voxel <= input.voxels.liftVoxels; ++voxel) {
        if (!body.isSolidAt(middle + Vec3{.z = static_cast<float>(voxel) * voxelHeight})) {
            return true;
        }
    }
    return false;
}

/// Whether a standing body of the profile of `input` fits on a floor at `feet`.
[[nodiscard]] auto standingBodyFitsAt(const TileInput& input, Vec3 feet) noexcept -> bool {
    return bodyFitsAt(input, standingBodyOf(input), feet);
}

/// Whether a standing body of the profile of `input` fits all along the way from `from` to `to`, a cell at a
/// time, what a hull grows by a body's box being wider than a cell: its feet on the way, or as far as
/// `leeway` to either side of it.
[[nodiscard]] auto bodyPassesAlong(const TileInput& input, Vec3 from, Vec3 to, float leeway = 0.0F) noexcept
    -> bool {
    const Vec3 way = to - from;
    const float distance = length(way);
    const Vec3 aside = distance > 0.0F ? Vec3{.x = -way.y, .y = way.x} * (leeway / distance) : Vec3{};
    const auto cells = static_cast<int>(std::ceil(distance / input.settings.cellSize.value()));
    for (int cell = 0; cell <= cells; ++cell) {
        const float share = cells > 0 ? static_cast<float>(cell) / static_cast<float>(cells) : 0.0F;
        const Vec3 feet = from + (way * share);
        if (!standingBodyFitsAt(input, feet) && !standingBodyFitsAt(input, feet + aside) &&
            !standingBodyFitsAt(input, feet - aside)) {
            return false;
        }
    }
    return true;
}

/// Whether a body on `span`, at `column`, `row` of `compact`, has a wall on its side `direction`: no floor it
/// walks on to there, and something solid in `solid` where its body would go, from a step up to its crouched
/// height. Nothing there makes that side a ledge, whatever the floor below is.
[[nodiscard]] auto hasWallOn(const rcCompactHeightfield& compact, const rcHeightfield& solid, int column,
                             int row, const rcCompactSpan& span, int direction,
                             const VoxelProfile& voxels) noexcept -> bool {
    if (rcGetCon(span, direction) != RC_NOT_CONNECTED) {
        return false;
    }
    const int nextColumn = column + rcGetDirOffsetX(direction);
    const int nextRow = row + rcGetDirOffsetY(direction);
    if (nextColumn < 0 || nextRow < 0 || nextColumn >= compact.width || nextRow >= compact.height) {
        return true;
    }
    const int knees = int{span.y} + voxels.climbVoxels;
    const int head = int{span.y} + voxels.crouchedVoxels;
    const std::span columns{solid.spans, narrow<std::size_t>(solid.width * solid.height)};
    for (const rcSpan* other = columns[narrow<std::size_t>(nextColumn + (nextRow * solid.width))];
         other != nullptr; other = other->next) {
        if (int{other->smin} < head && int{other->smax} > knees) {
            return true;
        }
    }
    return false;
}

/// Takes out of `compact` the floor within the profile's radius of a wall, where a body's middle cannot be,
/// by the square its box is. Unlike rcErodeWalkableArea it leaves a ledge its floor to the very edge: a
/// body's box holds it up with its middle there, and a block no wider than a body is floor it stands on.
void keepFromWalls(rcCompactHeightfield& compact, const rcHeightfield& solid, const VoxelProfile& voxels) {
    const auto count = narrow<std::size_t>(compact.spanCount);
    const std::span cells{compact.cells, narrow<std::size_t>(compact.width * compact.height)};
    const std::span spans{compact.spans, count};
    const std::span areas{compact.areas, count};
    struct Place {
        int column = 0;
        int row = 0;
        std::size_t span = 0;
    };
    std::vector<Place> reached;
    std::vector<bool> isReached(count);
    const auto reach = [&](int column, int row, std::size_t span) {
        if (areas[span] != RC_NULL_AREA && !isReached[span]) {
            isReached[span] = true;
            reached.push_back({.column = column, .row = row, .span = span});
        }
    };
    constexpr int kSides = 4;
    for (int row = 0; row < compact.height; ++row) {
        for (int column = 0; column < compact.width; ++column) {
            const rcCompactCell& cell = cells[narrow<std::size_t>(column + (row * compact.width))];
            const std::size_t firstSpan = cell.index;
            for (std::size_t span = firstSpan; span < firstSpan + cell.count; ++span) {
                for (int side = 0; side < kSides; ++side) {
                    if (hasWallOn(compact, solid, column, row, spans[span], side, voxels)) {
                        reach(column, row, span);
                    }
                }
            }
        }
    }
    // Then the cells next to those, the corners too, a layer of cells at a time.
    const auto next = [&](const Place& from, int side) -> std::optional<Place> {
        const int connection = rcGetCon(spans[from.span], side);
        if (connection == RC_NOT_CONNECTED) {
            return std::nullopt;
        }
        const int column = from.column + rcGetDirOffsetX(side);
        const int row = from.row + rcGetDirOffsetY(side);
        const rcCompactCell& cell = cells[narrow<std::size_t>(column + (row * compact.width))];
        return Place{
            .column = column, .row = row, .span = std::size_t{cell.index} + narrow<std::size_t>(connection)};
    };
    std::size_t first = 0;
    for (int layer = 1; layer < voxels.radiusCells; ++layer) {
        const std::size_t last = reached.size();
        for (; first < last; ++first) {
            const Place from = reached[first];
            for (int side = 0; side < kSides; ++side) {
                const std::optional<Place> beside = next(from, side);
                if (!beside) {
                    continue;
                }
                reach(beside->column, beside->row, beside->span);
                if (const std::optional<Place> corner = next(*beside, (side + 1) % kSides)) {
                    reach(corner->column, corner->row, corner->span);
                }
            }
        }
    }
    for (const Place& place : reached) {
        areas[place.span] = RC_NULL_AREA;
    }
}

/// Sorts the floor of `compact`, kept from walls already, by the bodies that fit on it, which
/// no surface may show (BodyHull): where no standing one does, it is of the kind Crouch where a crouched one
/// of a profile that crouches does, else no floor.
void sortFloorByBodies(rcCompactHeightfield& compact, const TileInput& input) {
    const AgentProfile& profile = input.profile;
    const bool crouches = profile.crouchedHeight < profile.standingHeight;
    const std::span cells{compact.cells, narrow<std::size_t>(compact.width * compact.height)};
    const std::span spans{compact.spans, narrow<std::size_t>(compact.spanCount)};
    const std::span areas{compact.areas, spans.size()};
    for (int row = 0; row < compact.height; ++row) {
        for (int column = 0; column < compact.width; ++column) {
            const rcCompactCell& cell = cells[narrow<std::size_t>(column + (row * compact.width))];
            const std::size_t firstSpan = cell.index;
            for (std::size_t span = firstSpan; span < firstSpan + cell.count; ++span) {
                if (areas[span] == RC_NULL_AREA) {
                    continue;
                }
                const std::array<float, 3> floor{
                    compact.bmin[0] + ((static_cast<float>(column) + 0.5F) * compact.cs),
                    compact.bmin[1] + (static_cast<float>(spans[span].y) * compact.ch),
                    compact.bmin[2] + ((static_cast<float>(row) + 0.5F) * compact.cs)};
                const Vec3 feet = positionOf(floor);
                if (areas[span] != areaIdOf(AreaKind::Crouch) && standingBodyFitsAt(input, feet)) {
                    continue;
                }
                areas[span] = crouches && bodyFitsAt(input, duckedBodyOf(input), feet)
                                  ? areaIdOf(AreaKind::Crouch)
                                  : std::uint8_t{RC_NULL_AREA};
            }
        }
    }
}

/// Marks the floor of `compact`, kept from walls already, that lies within `clearanceCells`
/// of where the profile may not stand as the kind Edge: Recast erodes a copy of the areas that much farther,
/// and each span of floor the copy loses lies along a wall or a ledge. Crouching and water keep their
/// kinds. Errors: TileRefused, with the tile and Recast's diagnostic.
[[nodiscard]] auto markEdges(RecastLog& log, rcCompactHeightfield& compact, int clearanceCells,
                             std::int32_t tileX, std::int32_t tileY) -> Result<void, MeshBuildError> {
    if (clearanceCells <= 0) {
        return {};
    }
    const auto count = narrow<std::size_t>(compact.spanCount);
    const std::span areas{compact.areas, count};
    const std::vector<std::uint8_t> kept(areas.begin(), areas.end());
    if (!rcErodeWalkableArea(&log, clearanceCells, compact)) {
        return refused(log, tileX, tileY, "its floor along walls");
    }
    for (std::size_t span = 0; span < count; ++span) {
        const bool isAlongWall = areas[span] == RC_NULL_AREA && kept[span] == areaIdOf(AreaKind::Floor);
        areas[span] = isAlongWall ? areaIdOf(AreaKind::Edge) : kept[span];
    }
    return {};
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

/// The voxels of tile `tileX`, `tileY`, filtered as the profile walks: what it steps onto, and where it has
/// no room. None without a triangle there. Errors: TileRefused.
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
    rcFilterWalkableLowHeightSpans(&log, input.voxels.crouchedVoxels, *heightfield);
    return std::optional{std::move(heightfield)};
}

/// How far apart the links along a ledge are, in the profile's radii: a body's two widths, so that one is
/// within a width of anywhere along it.
constexpr float kLinkSpacingRadii = 4.0F;

/// How far across a body of `profile` flies at its leap's speed, landing as high as it left.
[[nodiscard]] auto farthestLeapOf(const AgentProfile& profile) noexcept -> float {
    return profile.leapSpeed.value() * 2.0F *
           std::sqrt(2.0F * profile.jumpHeight.value() / profile.gravity.value());
}
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

/// A way a flight over a gap goes (Flight): how high a body rises leaving its start (headroomAt), and whether
/// only a crouched body stands there.
struct FlightWay {
    HammerUnits rise{};
    bool isCrouched = false;
};

/// How a body crosses between two floors (kindOfFlight): the kind of the flight, and each way it goes over a
/// gap, down from the higher floor and up from the lower, none for a way it does not go; over no gap, none
/// either way, its box being over both floors at once whatever its speed.
struct Flight {
    AreaKind kind = AreaKind::Drop;
    std::optional<FlightWay> down{};
    std::optional<FlightWay> up{};
};

/// A link of a tile, as Detour's off-mesh connections take it: where a body leaves and where it lands, the
/// radius about each end Detour joins it to a polygon within, its kind, whether it goes both ways, and its
/// id.
struct TileLink {
    Vec3 start{};
    Vec3 end{};
    float radius = 0.0F;
    AreaKind kind = AreaKind::Drop;
    bool goesBothWays = false;
    std::uint32_t id = 0;
};

/// The bounds across of a tile's polygons (rcPolyMesh's bmin and bmax), in Detour's space: Detour keeps a
/// link in the tile its start is within, their low sides in and their high ones out, and in no other
/// (dtCreateNavMeshData's classifyOffMeshPoint).
struct TileExtent {
    std::array<float, 3> low{};
    std::array<float, 3> high{};

    [[nodiscard]] auto holds(Vec3 point) const noexcept -> bool {
        const std::array<float, 3> at = detourOf(point);
        return at[0] >= low[0] && at[0] < high[0] && at[2] >= low[2] && at[2] < high[2];
    }
};

[[nodiscard]] auto extentOf(const rcPolyMesh& polygons) noexcept -> TileExtent {
    TileExtent extent;
    std::ranges::copy(polygons.bmin, extent.low.begin());
    std::ranges::copy(polygons.bmax, extent.high.begin());
    return extent;
}

/// The links found about a tile: its own, which Detour keeps of those that start in it, and those that start
/// in another tile, which go on to that one before Detour takes either (routed).
class TileLinks {
public:
    explicit TileLinks(TileExtent extent) noexcept : tile{extent} {}

    /// The flight `flight` from `start`, in the tile, down to `end`: over no gap one link, both ways for one
    /// a body jumps back up; over a gap each way it goes a link of its own, judged by its own flight
    /// (leastLeapSpeedOf) from the ground its start is (takeoffIdOf), the one back starting where this one
    /// ends, in whichever tile that is.
    void addFlight(Vec3 start, Vec3 end, float radius, const Flight& flight) {
        if (gapAcross(start, end, HammerUnits{radius}) <= 0.0F) {
            const bool goesBack = flight.kind == AreaKind::Jump || flight.kind == AreaKind::CrouchedJump;
            ownLinks.push_back({.start = start,
                                .end = end,
                                .radius = radius,
                                .kind = flight.kind,
                                .goesBothWays = goesBack});
            return;
        }
        const auto overGap = [&](Vec3 from, Vec3 to, const FlightWay& way) {
            return TileLink{.start = from,
                            .end = to,
                            .radius = radius,
                            .kind = flight.kind,
                            .id = takeoffIdOf(way.rise, way.isCrouched)};
        };
        if (flight.down) {
            ownLinks.push_back(overGap(start, end, *flight.down));
        }
        if (flight.up) {
            (tile.holds(end) ? ownLinks : routedLinks).push_back(overGap(end, start, *flight.up));
        }
    }

    /// A link of the kind `kind`, both ways, through the ladder or door whose middle is `through`.
    void addThrough(Vec3 start, Vec3 end, float radius, AreaKind kind, Vec3 through) {
        ownLinks.push_back({.start = start,
                            .end = end,
                            .radius = radius,
                            .kind = kind,
                            .goesBothWays = true,
                            .id = throughIdOf(through)});
    }

    [[nodiscard]] auto own() const noexcept -> std::span<const TileLink> { return ownLinks; }
    [[nodiscard]] auto routed() const noexcept -> std::span<const TileLink> { return routedLinks; }

private:
    TileExtent tile;
    std::vector<TileLink> ownLinks;
    std::vector<TileLink> routedLinks;
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

/// The heights of the floors over `point`, from `lowest` to `highest`, that the profile stands on.
[[nodiscard]] auto floorsAt(Vec3 point, std::span<const NearTriangle> near, float lowest, float highest)
    -> std::vector<float> {
    std::vector<float> floors;
    for (const NearTriangle& triangle : near) {
        if (triangle.area == RC_NULL_AREA) {
            continue;
        }
        const std::optional<float> height = heightAbove(point, triangle);
        if (height && *height >= lowest && *height <= highest) {
            floors.push_back(*height);
        }
    }
    return floors;
}

/// The longest a flight is looked at, in seconds: past the fall from any ledge a body drops from unhurt.
constexpr float kLongestFlight = 3.0F;

/// Whether `body` moves from feet at `was` to feet at `feet` in the air: what stops it is its hull, and the
/// surfaces at its knees and at its head, which no hull has of a brush entity, a cell within its middle, its
/// top and a step above its feet.
[[nodiscard]] auto movesInTheAir(const TileInput& input, std::span<const NearTriangle> near,
                                 const ProfileBody& body, Vec3 was, Vec3 feet) -> bool {
    const float cellHeight = input.settings.cellHeight.value();
    const Vec3 middle{.z = (body.height.value() * 0.5F) + cellHeight};
    const Vec3 knees{.z = input.profile.stepHeight.value() + cellHeight};
    const Vec3 head{.z = body.height.value() - cellHeight};
    return !body.isSolidAt(feet + middle) && std::ranges::none_of(near, [&](const NearTriangle& triangle) {
        return meets(was + knees, feet + knees, triangle) || meets(was + head, feet + head, triangle);
    });
}

/// Whether `body` comes down on the floor at `to` when it leaves the floor at `from`, jumping `jump` high, at
/// the profile's speed across, under its gravity, as the game moves it (movesInTheAir): on across where
/// nothing stops it, to no farther than `to`; going on up or down along a wall that stops it there, as its
/// box slides; and falling once its head meets a ceiling.
[[nodiscard]] auto bodyFlies(const TileInput& input, std::span<const NearTriangle> near,
                             const ProfileBody& body, Vec3 from, Vec3 to, float jump) -> bool {
    const AgentProfile& profile = input.profile;
    const float gravity = profile.gravity.value();
    const float speed = profile.leapSpeed.value();
    const Vec3 span{.x = to.x - from.x, .y = to.y - from.y};
    const float across = length(span);
    const Vec3 way = across > 0.0F ? span / across : Vec3{};
    const float tick = input.settings.cellSize.value() / speed;
    const auto isOpen = [&](Vec3 was, Vec3 feet) {
        return movesInTheAir(input, near, body, was, feet);
    };
    float rising = std::sqrt(2.0F * gravity * jump);
    float gone = 0.0F;
    Vec3 at = from;
    for (float time = tick; time < kLongestFlight; time += tick) {
        // By the mean of its speed over the tick, exact under a constant pull: by its speed at the end alone,
        // a flight peaked short of what the jump reaches by half a tick's rise, a few units.
        const float wasRising = rising;
        rising -= gravity * tick;
        const float flown = at.z + ((wasRising + rising) * 0.5F * tick);
        const bool comesDown = rising < 0.0F && flown <= to.z;
        const float height = comesDown ? to.z : flown;
        const float further = std::min(gone + (speed * tick), across);
        const Vec3 aheadAndUp{.x = from.x + (way.x * further), .y = from.y + (way.y * further), .z = height};
        const Vec3 ahead{.x = aheadAndUp.x, .y = aheadAndUp.y, .z = at.z};
        const Vec3 straight{.x = at.x, .y = at.y, .z = height};
        if (isOpen(at, aheadAndUp)) {
            gone = further;
            at = aheadAndUp;
        } else if (isOpen(at, straight)) {
            at = straight;
        } else if (isOpen(at, ahead)) {
            // Its head on a ceiling, or its feet on a floor, it goes on across as it stops rising or falling.
            gone = further;
            at = ahead;
            rising = std::min(rising, 0.0F);
        } else {
            rising = std::min(rising, 0.0F);
        }
        if (comesDown && at.z <= to.z) {
            return gone >= across;
        }
    }
    return false;
}

/// How high `body` rises from the floor at `from`, jumping `jump` high, before its head meets anything
/// (movesInTheAir), as a body that leaves it slowly rises: straight up, cell by cell, from where it stands,
/// its middle out of the solid within its lift over a slope (bodyFitsAt). Faster it may get out from under
/// what is over it before its head meets it, as the build flies the fastest body (bodyFlies); a body that
/// leaves a ledge crouched leaves it at a crawl.
[[nodiscard]] auto headroomAt(const TileInput& input, std::span<const NearTriangle> near,
                              const ProfileBody& body, Vec3 from, float jump) -> HammerUnits {
    const float cellHeight = input.settings.cellHeight.value();
    const float lift = static_cast<float>(input.voxels.liftVoxels) * cellHeight;
    const Vec3 middle{.z = (body.height.value() * 0.5F) + cellHeight};
    float risen = 0.0F;
    while (risen < lift && body.isSolidAt(from + middle + Vec3{.z = risen})) {
        risen += cellHeight;
    }
    while (risen < jump) {
        const float next = std::min(risen + cellHeight, jump);
        if (!movesInTheAir(input, near, body, from + Vec3{.z = risen}, from + Vec3{.z = next})) {
            break;
        }
        risen = next;
    }
    return HammerUnits{risen};
}

/// How a body crosses between the floor at `high` and the floor at `low`, no more than a step above it: of
/// the kind Jump, both ways, when standing it jumps back up, and down over a gap where it comes down jumping;
/// CrouchedJump, the same ducking in the air, for the height or for the room there; else Drop, down only;
/// none when it does not come down on it.
[[nodiscard]] auto kindOfFlight(const TileInput& input, std::span<const NearTriangle> near, Vec3 high,
                                Vec3 low) -> std::optional<Flight> {
    const AgentProfile& profile = input.profile;
    const float height = high.z - low.z;
    const bool standsAbove = standingBodyFitsAt(input, high);
    const bool standsBelow = standingBodyFitsAt(input, low);
    const auto ducksAt = [&](Vec3 feet) {
        return profile.crouchedHeight < profile.standingHeight &&
               bodyFitsAt(input, duckedBodyOf(input), feet);
    };
    const bool ducksAbove = ducksAt(high);
    const bool ducksBelow = ducksAt(low);
    if ((!standsAbove && !ducksAbove) || (!standsBelow && !ducksBelow)) {
        return std::nullopt;
    }
    const ProfileBody standing = standingBodyOf(input);
    const ProfileBody ducked = duckedBodyOf(input);
    const bool spansGap = gapAcross(high, low, profile.radius) > 0.0F;
    // A way over a gap, from `start`, where a body stands upright or not: one that does not come down on the
    // other end goes none.
    const auto wayOf = [&](bool flies, const ProfileBody& body, Vec3 start, bool standsAtStart, float jump) {
        return spansGap && flies ? std::optional{FlightWay{.rise = headroomAt(input, near, body, start, jump),
                                                           .isCrouched = !standsAtStart}}
                                 : std::nullopt;
    };
    // Up, and down over a gap, each way flown of its own.
    const auto jumpsBetween = [&](AreaKind kind, const ProfileBody& body,
                                  float jump) -> std::optional<Flight> {
        if (!bodyFlies(input, near, body, low, high, jump)) {
            return std::nullopt;
        }
        return Flight{.kind = kind,
                      .down = wayOf(spansGap && bodyFlies(input, near, body, high, low, jump), body, high,
                                    standsAbove, jump),
                      .up = wayOf(true, body, low, standsBelow, jump)};
    };
    if (standsAbove && standsBelow && height <= profile.jumpHeight.value()) {
        if (std::optional<Flight> flight =
                jumpsBetween(AreaKind::Jump, standing, profile.jumpHeight.value())) {
            return flight;
        }
    }
    if (profile.crouchedJumpHeight && ducksAbove && ducksBelow &&
        height <= profile.crouchedJumpHeight->value()) {
        if (std::optional<Flight> flight =
                jumpsBetween(AreaKind::CrouchedJump, ducked, profile.crouchedJumpHeight->value())) {
            return flight;
        }
    }
    if (height > profile.dropHeight.value()) {
        return std::nullopt;
    }
    // Off the edge of a ledge it walks; only across a gap does it jump.
    const float hop = spansGap ? profile.jumpHeight.value() : profile.stepHeight.value();
    for (const auto& [fits, body] :
         {std::pair{standsAbove && standsBelow, standing}, std::pair{ducksAbove && ducksBelow, ducked}}) {
        if (fits && bodyFlies(input, near, body, high, low, hop)) {
            return Flight{.kind = AreaKind::Drop, .down = wayOf(true, body, high, standsAbove, hop)};
        }
    }
    return std::nullopt;
}

/// A link of a body leaving the floor of a polygon: where it lands, and how it crosses.
struct FoundLink {
    Vec3 end{};
    Flight flight{};
};

/// The links of a body leaving the floor at `start`, across the edge its way `outward` faces, as far as it
/// flies at the profile's speed: to the nearest floor of each height it comes down on (kindOfFlight), below
/// it within its drop height, once its box is past the edge, and level with it past a gap; none past a wall,
/// above a step of it. A floor higher than a step has links of its own, to this one. How high the ledge is is
/// the geometry's: a polygon's corners stand up to a cell above the floor they cover.
[[nodiscard]] auto linksFrom(Vec3 start, Vec3 outward, const TileInput& input,
                             std::span<const NearTriangle> near) -> std::vector<FoundLink> {
    const AgentProfile& profile = input.profile;
    const float step = profile.stepHeight.value();
    const float cell = input.settings.cellSize.value();
    // The floor it leaves within a step of its polygon's corner, as Detour finds the polygon a link starts
    // on (walkableClimb): a polygon's outline strays from the floor's edge by a little, so that the floor may
    // end just short of an edge of it.
    std::optional<float> ledge;
    for (float back = 0.0F; !ledge && back <= input.settings.edgeError.value() + cell; back += cell) {
        ledge = floorAt(start - (outward * back), near, start.z - step, start.z + step);
    }
    if (!ledge) {
        return {};
    }
    const Vec3 leaves{.x = start.x, .y = start.y, .z = *ledge};
    const Vec3 aboveStep{.z = step + input.settings.cellHeight.value()};
    const float farthest = farthestLeapOf(profile);
    std::vector<float> levels;
    std::vector<FoundLink> found;
    std::optional<float> edgeAt;
    Vec3 previous = leaves;
    for (float distance = cell; distance <= farthest; distance += cell) {
        const Vec3 point = leaves + (outward * distance);
        if (std::ranges::any_of(near, [&](const NearTriangle& triangle) {
                return meets(previous + aboveStep, point + aboveStep, triangle);
            })) {
            break;
        }
        previous = point;
        std::vector<float> floors = floorsAt(point, near, *ledge - profile.dropHeight.value(), *ledge + step);
        std::ranges::sort(floors, std::greater{});
        const auto isLevel = [&](float floor) {
            return std::abs(floor - *ledge) <= step;
        };
        if (!edgeAt && std::ranges::none_of(floors, isLevel)) {
            edgeAt = distance;
        }
        // A body's box holds it up on this floor until its middle is its radius past the edge: it comes down
        // on no lower floor before that.
        const bool hasLeftLedge = edgeAt && distance >= *edgeAt + profile.radius.value();
        for (const float floor : floors) {
            if ((isLevel(floor) ? !edgeAt.has_value() : !hasLeftLedge) ||
                std::ranges::any_of(levels, [=](float other) { return std::abs(other - floor) <= step; })) {
                continue;
            }
            // Right at the edge of that floor, as it leaves this one: a link goes both ways, and a body
            // taking it the other way has all the floor behind that edge for its run at the gap.
            const Vec3 lands = point + Vec3{.z = floor - point.z};
            if (const std::optional<Flight> flight = kindOfFlight(input, near, leaves, lands)) {
                levels.push_back(floor);
                found.push_back({.end = lands, .flight = *flight});
            }
        }
        // The floor goes on past where a polygon's outline strays from its edge: this is no edge of the
        // floor, the side of a tile say, and no link leaves from here, as a body that walks on gets to the
        // real one.
        if (!edgeAt && distance > input.settings.edgeError.value() + cell) {
            return {};
        }
    }
    return found;
}

/// A floor a ladder leads to: where a body lands on it, and how far from the ladder's middle across its edge
/// is.
struct LadderLanding {
    Vec3 at{};
    float distance = 0.0F;
};

/// How far past a ladder's side the floor it leads to is looked for, in the profile's radii: over a wall as
/// thick as a body's two widths.
constexpr float kLadderReachRadii = 4.0F;

/// The floors `ladder` leads to, the lowest first, within the profile's step of each other one of them,
/// the nearest: past each side of it, as far as kLadderReachRadii, those a body's top reaches its bottom
/// from, and no higher than a step above its top, with no wall in the way above a step of them, nor what
/// stops a body (BodyHull) from where it touches the ladder. A body lands its radius and a cell past where
/// the floor starts, the mesh starting its radius from its edge.
[[nodiscard]] auto landingsOf(const Aabb& ladder, const TileInput& input, std::span<const NearTriangle> near)
    -> std::vector<LadderLanding> {
    const AgentProfile& profile = input.profile;
    const float step = profile.stepHeight.value();
    const float cell = input.settings.cellSize.value();
    const float radius = profile.radius.value();
    const float lowest = ladder.min.z - profile.standingHeight.value();
    const float highest = ladder.max.z + step;
    const Vec3 middle = (ladder.min + ladder.max) * 0.5F;
    const Vec3 half = (ladder.max - ladder.min) * 0.5F;
    const float reach = radius * kLadderReachRadii;
    const Vec3 aboveStep{.z = step + input.settings.cellHeight.value()};
    std::vector<LadderLanding> landings;
    for (const Vec3 way : {Vec3{.x = 1.0F}, Vec3{.x = -1.0F}, Vec3{.y = 1.0F}, Vec3{.y = -1.0F}}) {
        const float side = (std::abs(way.x) * half.x) + (std::abs(way.y) * half.y);
        std::vector<float> met;
        const auto cells = static_cast<std::uint32_t>(reach / cell);
        for (std::uint32_t reached = 1; reached <= cells; ++reached) {
            const float distance = side + (cell * static_cast<float>(reached));
            const Vec3 point = middle + (way * distance);
            for (const float floor : floorsAt(point, near, lowest, highest)) {
                if (std::ranges::any_of(met, [=](float other) { return std::abs(other - floor) <= step; })) {
                    continue;
                }
                met.push_back(floor);
                Vec3 landing = middle + (way * (distance + radius + cell));
                landing.z = floor;
                const Vec3 from{.x = middle.x, .y = middle.y, .z = floor};
                if (!floorsAt(landing, near, floor - step, floor + step).empty() &&
                    std::ranges::none_of(near,
                                         [&](const NearTriangle& triangle) {
                                             return meets(from + aboveStep, landing + aboveStep, triangle);
                                         }) &&
                    // Up to its radius aside: a ladder in a corner has its floors along the wall it is on,
                    // and a link's end comes that far to the mesh, which keeps its radius from that wall.
                    bodyPassesAlong(input, from + (way * (side + radius)), landing, radius)) {
                    landings.push_back({.at = landing, .distance = distance});
                }
            }
        }
    }
    std::ranges::sort(landings, {}, [](const LadderLanding& landing) { return landing.at.z; });
    std::vector<LadderLanding> levels;
    for (const LadderLanding& landing : landings) {
        if (levels.empty() || landing.at.z - levels.back().at.z > step) {
            levels.push_back(landing);
        } else if (landing.distance < levels.back().distance) {
            levels.back() = landing;
        }
    }
    return levels;
}

/// Adds to `links` the links of the ladders of `input` about the tile `bounds` holds, from each floor one
/// leads to to the next up: Detour keeps those that start in the tile.
void addLadderLinks(const TileInput& input, const TileBounds& bounds, TileLinks& links) {
    const float radius = input.profile.radius.value();
    const float reach = input.settings.cellSize.value() + (radius * (kLadderReachRadii + 1.0F));
    for (const Aabb& ladder : input.ladders) {
        // Across, Detour's z is the game's y turned about.
        const TileBounds about{.low = {ladder.min.x - reach, 0.0F, -ladder.max.y - reach},
                               .high = {ladder.max.x + reach, 0.0F, -ladder.min.y + reach}};
        if (about.high[0] < bounds.low[0] || about.low[0] > bounds.high[0] || about.high[2] < bounds.low[2] ||
            about.low[2] > bounds.high[2]) {
            continue;
        }
        const std::vector<NearTriangle> near = trianglesAbout(input, about, 0.0F);
        const std::vector<LadderLanding> levels = landingsOf(ladder, input, near);
        const Vec3 middle = (ladder.min + ladder.max) * 0.5F;
        for (std::size_t level = 1; level < levels.size(); ++level) {
            links.addThrough(levels[level - 1].at, levels[level].at, radius, AreaKind::Ladder, middle);
        }
    }
}

/// Adds to `links` the links of the doors of `input` about the tile `bounds` holds, between the floors on the
/// two sides of each across the way it is thin, at the height of its bottom, where a body's middle clears it:
/// Detour keeps those that start in the tile.
void addDoorLinks(const TileInput& input, const TileBounds& bounds, TileLinks& links) {
    const float radius = input.profile.radius.value();
    const float cell = input.settings.cellSize.value();
    const float step = input.profile.stepHeight.value();
    for (const Aabb& door : input.doors) {
        const Vec3 middle = (door.min + door.max) * 0.5F;
        const Vec3 half = (door.max - door.min) * 0.5F;
        const bool isThinAlongX = half.x <= half.y;
        const float past = (isThinAlongX ? half.x : half.y) + radius + cell;
        const Vec3 across = isThinAlongX ? Vec3{.x = past} : Vec3{.y = past};
        // Across, Detour's z is the game's y turned about.
        const float reach = past + radius + cell;
        const TileBounds about{.low = {door.min.x - reach, 0.0F, -door.max.y - reach},
                               .high = {door.max.x + reach, 0.0F, -door.min.y + reach}};
        if (about.high[0] < bounds.low[0] || about.low[0] > bounds.high[0] || about.high[2] < bounds.low[2] ||
            about.low[2] > bounds.high[2]) {
            continue;
        }
        const std::vector<NearTriangle> near = trianglesAbout(input, about, 0.0F);
        const auto floorPast = [&](Vec3 way) -> std::optional<Vec3> {
            const Vec3 point = Vec3{.x = middle.x, .y = middle.y} + way;
            const std::vector<float> floors = floorsAt(point, near, door.min.z - step, door.min.z + step);
            if (floors.empty()) {
                return std::nullopt;
            }
            return point + Vec3{.z = std::ranges::max(floors)};
        };
        const std::optional<Vec3> one = floorPast(across);
        const std::optional<Vec3> other = floorPast(across * -1.0F);
        if (one && other) {
            links.addThrough(*one, *other, radius, AreaKind::Door, middle);
        }
    }
}

/// What Recast marks an edge along the side of a tile with (rcBuildPolyMesh's portal edges).
constexpr std::uint16_t kTileSideEdge = 0x8000;

/// The links of the polygons of `polygons`, tile `tileX`, `tileY` of `input`: along each edge of a polygon
/// no other polygon shares, a wall's or a ledge's, one a body's two widths or less apart, its middle first
/// (linksFrom). An edge along the side of the tile is a ledge too where no floor goes on past it, so its
/// links start just inside the tile, the only one Detour keeps them in.
[[nodiscard]] auto linksOf(const TileInput& input, const rcPolyMesh& polygons, std::int32_t tileX,
                           std::int32_t tileY) -> TileLinks {
    const AgentProfile& profile = input.profile;
    const float spacing = profile.radius.value() * kLinkSpacingRadii;
    const float reach = farthestLeapOf(profile) + profile.radius.value() + input.settings.cellSize.value();
    const std::vector<NearTriangle> near = trianglesAbout(input, boundsOf(input, tileX, tileY), reach);
    const auto vertexOf = [&polygons](std::uint16_t vertex) {
        const std::array<float, 3> point{
            polygons.bmin[0] + (static_cast<float>(polygons.verts[(vertex * 3) + 0]) * polygons.cs),
            polygons.bmin[1] + (static_cast<float>(polygons.verts[(vertex * 3) + 1]) * polygons.ch),
            polygons.bmin[2] + (static_cast<float>(polygons.verts[(vertex * 3) + 2]) * polygons.cs)};
        return positionOf(point);
    };
    const auto sides = narrow<std::size_t>(polygons.nvp);
    TileLinks links{extentOf(polygons)};
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
            const std::uint16_t neighbor = record[sides + edge];
            const bool isTileSide = neighbor != RC_MESH_NULL_IDX && (neighbor & kTileSideEdge) != 0;
            if (neighbor != RC_MESH_NULL_IDX && !isTileSide) {
                continue; // shared
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
                const Vec3 start = (from + ((to - from) * share)) -
                                   (isTileSide ? outward * detail::kLinkInsetFromTileSide : Vec3{});
                for (const FoundLink& link : linksFrom(start, outward, input, near)) {
                    links.addFlight(start, link.end, profile.radius.value(), link.flight);
                }
            }
        }
    }
    addLadderLinks(input, boundsOf(input, tileX, tileY), links);
    addDoorLinks(input, boundsOf(input, tileX, tileY), links);
    return links;
}

/// Detour's data of the polygons of `polygons` and `detail`, and of `links`, tile `tileX`, `tileY`. Errors:
/// TileRefused.
[[nodiscard]] auto tileDataOf(const TileInput& input, rcPolyMesh& polygons, const rcPolyMeshDetail& detail,
                              std::span<const TileLink> links, std::int32_t tileX, std::int32_t tileY)
    -> Result<BuiltTile, MeshBuildError> {
    const auto polygonCount = narrow<std::size_t>(polygons.npolys);
    std::ranges::fill(std::span{polygons.flags, polygonCount}, kWalkablePolygon);
    // Detour takes each of a link's numbers side by side with the others' (its two ends one after the other).
    std::vector<float> points;
    std::vector<float> radii;
    std::vector<std::uint16_t> flags;
    std::vector<std::uint8_t> areas;
    std::vector<std::uint8_t> ways;
    std::vector<std::uint32_t> ids;
    for (const TileLink& link : links) {
        std::ranges::copy(detourOf(link.start), std::back_inserter(points));
        std::ranges::copy(detourOf(link.end), std::back_inserter(points));
        radii.push_back(link.radius);
        flags.push_back(kWalkablePolygon);
        areas.push_back(areaIdOf(link.kind));
        ways.push_back(link.goesBothWays ? std::uint8_t{DT_OFFMESH_CON_BIDIR} : std::uint8_t{0});
        ids.push_back(link.id);
    }
    dtNavMeshCreateParams params{};
    params.offMeshConVerts = points.data();
    params.offMeshConRad = radii.data();
    params.offMeshConFlags = flags.data();
    params.offMeshConAreas = areas.data();
    params.offMeshConDir = ways.data();
    params.offMeshConUserID = ids.data();
    params.offMeshConCount = narrow<int>(radii.size());
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

/// A tile's polygons and their details as Recast built them, and the links found about it (linksOf): what the
/// first pass of a build keeps of it for the second, which hands each tile the links that start in it from
/// the tiles about it before Detour takes any (finishTile).
struct TileDraft {
    PolygonMesh polygons{};
    DetailMesh detail{};
    TileLinks links;
};

/// What the first pass of a build gives a tile: its draft, none when it holds no floor, or why Recast refused
/// it.
using DraftResult = Result<std::optional<TileDraft>, MeshBuildError>;

/// Tile `tileX`, `tileY` of `input`, as Recast builds it: its voxels, the floor the profile's radius
/// keeps away from walls and ledges, sorted by the bodies that fit on it (sortFloorByBodies), that floor
/// along them within its wall clearance (markEdges), its regions, their outlines, the polygons of those and
/// their details, and the links about it. None when it holds no floor. Errors: TileRefused.
[[nodiscard]] auto draftTile(const TileInput& input, std::int32_t tileX, std::int32_t tileY) -> DraftResult {
    RecastLog log;
    auto rasterized = rasterize(log, input, tileX, tileY);
    if (!rasterized) {
        return std::unexpected{std::move(rasterized.error())};
    }
    if (!*rasterized) {
        return std::optional<TileDraft>{};
    }
    Heightfield heightfield = std::move(**rasterized);
    const VoxelProfile& voxels = input.voxels;
    CompactHeightfield compact{rcAllocCompactHeightfield()};
    if (!compact ||
        !rcBuildCompactHeightfield(&log, voxels.crouchedVoxels, voxels.climbVoxels, *heightfield, *compact)) {
        return refused(log, tileX, tileY, "its compact heightfield");
    }
    keepFromWalls(*compact, *heightfield, voxels);
    heightfield.reset(); // what is left is in the compact one
    markCrouching(*compact, voxels.standingVoxels);
    sortFloorByBodies(*compact, input);
    if (auto marked = markEdges(log, *compact, voxels.clearanceCells, tileX, tileY); !marked) {
        return std::unexpected{std::move(marked.error())};
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
        return std::optional<TileDraft>{};
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
        return std::optional<TileDraft>{};
    }
    TileLinks links = linksOf(input, *polygons, tileX, tileY);
    return std::optional<TileDraft>{
        TileDraft{.polygons = std::move(polygons), .detail = std::move(detail), .links = std::move(links)}};
}

/// How many tiles about a tile's, across or down, the links that start in it may be found from: as many as a
/// flight from a ledge reaches over (linksOf).
[[nodiscard]] auto linkReachInTiles(const TileInput& input) noexcept -> std::int32_t {
    const float reach =
        farthestLeapOf(input.profile) + input.profile.radius.value() + input.settings.cellSize.value();
    const float tileSide = static_cast<float>(input.settings.tileCells) * input.settings.cellSize.value();
    return static_cast<std::int32_t>(std::ceil(reach / tileSide));
}

/// The tile whose draft is at `index` of `drafts`, row by row, `tilesAcross` to a row and `tilesDown` rows,
/// as Detour takes it (tileDataOf): with its own links, then those found from the tiles about it, row by row,
/// that start in it. Its polygons and their details go once Detour has them; its links stay for the tiles
/// about it. None when it holds no floor. Errors: its draft's, TileRefused.
[[nodiscard]] auto finishTile(const TileInput& input, std::span<DraftResult> drafts, std::int32_t tilesAcross,
                              std::int32_t tilesDown, std::size_t index)
    -> Result<std::optional<BuiltTile>, MeshBuildError> {
    DraftResult& drafted = drafts[index];
    if (!drafted) {
        return std::unexpected{drafted.error()};
    }
    if (!*drafted) {
        return std::optional<BuiltTile>{};
    }
    TileDraft& draft = **drafted;
    const auto tile = narrow<std::int32_t>(index);
    const std::int32_t tileX = tile % tilesAcross;
    const std::int32_t tileY = tile / tilesAcross;
    const TileExtent extent = extentOf(*draft.polygons);
    std::vector<TileLink> links{std::from_range, draft.links.own()};
    const std::int32_t reach = linkReachInTiles(input);
    for (std::int32_t y = std::max(tileY - reach, 0); y <= std::min(tileY + reach, tilesDown - 1); ++y) {
        for (std::int32_t x = std::max(tileX - reach, 0); x <= std::min(tileX + reach, tilesAcross - 1);
             ++x) {
            const DraftResult& about = drafts[narrow<std::size_t>((y * tilesAcross) + x)];
            if ((x == tileX && y == tileY) || !about || !*about) {
                continue;
            }
            std::ranges::copy_if((**about).links.routed(), std::back_inserter(links),
                                 [&extent](const TileLink& link) { return extent.holds(link.start); });
        }
    }
    auto data = tileDataOf(input, *draft.polygons, *draft.detail, links, tileX, tileY);
    draft.polygons.reset();
    draft.detail.reset();
    return data.transform([](BuiltTile built) { return std::optional{std::move(built)}; });
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
    BuiltTiles grid{};                 ///< Its tiles left out: they are in results.
    std::vector<DraftResult> drafts{}; ///< Row by row, the first pass's.
    std::vector<Result<std::optional<BuiltTile>, MeshBuildError>> results{}; ///< Row by row, the second's.
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
        TileWork{.input = std::move(*input), .grid = std::move(*grid), .drafts = {}, .results = {}});
    work->drafts.resize(tileCount);
    work->results.resize(tileCount);
    // Each tile in a job of its own in each pass, its result at its own index: the same mesh with any number
    // of workers. The second pass starts once the first is done, as a tile's links may start in the tiles
    // about it. The work outlives the jobs: its holder waits for them before it lets it go (MeshBuild).
    TileWork* const shared = work.get();
    const std::array drafted{jobs.parallelFor(
        {.label = "navigation.tile", .priority = priority}, std::views::iota(std::size_t{0}, tileCount),
        jobs::Grain::automatic(), [shared](std::size_t index) {
            const auto tile = narrow<std::int32_t>(index);
            const std::int32_t tilesAcross = shared->grid.tilesAcross;
            shared->drafts[index] = draftTile(shared->input, tile % tilesAcross, tile / tilesAcross);
        })};
    const jobs::JobHandle done = jobs.parallelFor(
        {.label = "navigation.tileLinks", .priority = priority, .dependsOn = drafted},
        std::views::iota(std::size_t{0}, tileCount), jobs::Grain::automatic(), [shared](std::size_t index) {
            shared->results[index] = finishTile(shared->input, shared->drafts, shared->grid.tilesAcross,
                                                shared->grid.tilesDown, index);
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
constexpr std::uint32_t kTilesVersion = 12;

/// The bytes a key hashes for each vertex (its three coordinates) and each triangle (its three corners
/// and its kind), to reserve them.
constexpr std::size_t kVertexKeyBytes = 3 * sizeof(float);
constexpr std::size_t kTriangleKeyBytes = (3 * sizeof(std::uint32_t)) + sizeof(AreaKind);

/// Adds to `bytes` what a side of a node of a body hull is: which of the two, and its value.
void appendChild(std::vector<std::byte>& bytes, const BodyHullChild& child) {
    appendLittleEndian(bytes, narrow<std::uint8_t>(child.index()));
    if (const auto* index = std::get_if<std::uint32_t>(&child)) {
        appendLittleEndian(bytes, *index);
    } else {
        appendLittleEndian(bytes, std::to_underlying(std::get<BodyHullLeaf>(child)));
    }
}

/// Adds to `bytes` the corners of `box`.
void appendBox(std::vector<std::byte>& bytes, const Aabb& box) {
    for (const float value : {box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z}) {
        appendLittleEndian(bytes, value);
    }
}

/// Adds to `bytes` all of `hull`.
void appendHull(std::vector<std::byte>& bytes, const BodyHull& hull) {
    appendChild(bytes, hull.root);
    appendLittleEndian(bytes, std::uint64_t{hull.nodes.size()});
    for (const BodyHullNode& node : hull.nodes) {
        for (const float value :
             {node.plane.normal.x, node.plane.normal.y, node.plane.normal.z, node.plane.distance}) {
            appendLittleEndian(bytes, value);
        }
        appendChild(bytes, node.children[0]);
        appendChild(bytes, node.children[1]);
    }
}

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
          profile.crouchedJumpHeight.value_or(HammerUnits{}).value(), profile.dropHeight.value(),
          profile.leapSpeed.value(), profile.gravity.value(), profile.minGroundNormalZ}) {
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
    appendLittleEndian(bytes, std::uint64_t{geometry.ladders.size()});
    for (const Aabb& ladder : geometry.ladders) {
        appendBox(bytes, ladder);
    }
    appendHull(bytes, geometry.bodyHull);
    appendHull(bytes, geometry.crouchedBodyHull);
    appendLittleEndian(bytes, std::uint64_t{geometry.doors.size()});
    for (const Aabb& door : geometry.doors) {
        appendBox(bytes, door);
    }
    for (const auto* const placed : {&geometry.brushBodyHulls, &geometry.crouchedBrushBodyHulls}) {
        appendLittleEndian(bytes, std::uint64_t{placed->size()});
        for (const PlacedBodyHull& brush : *placed) {
            for (const float value : {brush.origin.x, brush.origin.y, brush.origin.z}) {
                appendLittleEndian(bytes, value);
            }
            // Where a body's middle may meet it at all bounds where its hull stops one (isSolidAt).
            appendBox(bytes, brush.bounds);
            appendHull(bytes, brush.hull);
        }
    }
    return MeshKey{hash64(bytes)};
}

} // namespace nx::navigation
