// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// Detour stays in this unit (docs/02-coding-standards.md, section 2): the meshes, queries and
// corridors of the interface hold it behind their own types.
#include <DetourCommon.h>
#include <DetourLocalBoundary.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourNode.h>
#include <DetourObstacleAvoidance.h>
#include <DetourPathCorridor.h>
#include <DetourStatus.h>

module nx.navigation;

import std;
import nx.core;
import nx.math;
import nx.jobs;

namespace nx::navigation {

namespace {

constexpr nx::log::Category kLog{"navigation"};

/// The most nodes Detour's search counts: its node indices are 16 bits, the last of them none
/// (DT_NULL_IDX).
constexpr std::size_t kMostSearchNodes = 65535;

/// How many more polygons than its way a corridor has room for, as its ends move between one search and the
/// next: as many as DetourCrowd's agents hold in all (dtCrowd's m_maxPathResult).
constexpr std::size_t kCorridorRoom = 256;

/// A point of Detour's space has three coordinates.
constexpr std::size_t kCoordinates = 3;

[[nodiscard]] auto isFinite(Vec3 point) noexcept -> bool {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] auto validCosts(const AreaCosts& costs) noexcept -> bool {
    return std::ranges::all_of(AreaCosts::keys(), [&costs](AreaKind area) {
        return std::isfinite(costs[area]) && costs[area] >= 1.0F;
    });
}

[[nodiscard]] auto validAvoided(std::span<const AvoidedPlace> avoided) noexcept -> bool {
    return std::ranges::all_of(avoided, [](const AvoidedPlace& place) {
        return isFinite(place.position) && std::isfinite(place.radius.value()) &&
               place.radius >= HammerUnits{} && std::isfinite(place.cost) && place.cost >= 1.0F;
    });
}

/// Whether a way may be searched for `request`. Errors: InvalidQuery (a cost or an avoided place out of
/// bounds).
[[nodiscard]] auto checkRequest(const PathRequest& request) -> Result<void, NavigationError> {
    if (request.costs && !validCosts(*request.costs)) {
        return fail(NavigationError::InvalidQuery, "area costs must be finite and at least one");
    }
    if (!validAvoided(request.avoided)) {
        return fail(
            NavigationError::InvalidQuery,
            "an avoided place must be finite, of a radius of none or more and a cost of at least one");
    }
    return {};
}

struct NavMeshFree {
    void operator()(dtNavMesh* mesh) const noexcept { dtFreeNavMesh(mesh); }
};

struct NavMeshQueryFree {
    void operator()(dtNavMeshQuery* query) const noexcept { dtFreeNavMeshQuery(query); }
};

struct ObstacleAvoidanceFree {
    void operator()(dtObstacleAvoidanceQuery* avoidance) const noexcept {
        dtFreeObstacleAvoidanceQuery(avoidance);
    }
};

/// The wall segments one avoidance weighs at most: a local boundary's own room (dtLocalBoundary).
constexpr int kMostSteeringWalls = 8;
/// How far an agent moves, in shares of how far it looks about it, before it looks for the walls again:
/// DetourCrowd's.
constexpr float kWallsFoundAgainShare = 0.25F;
/// How near a wall a point counts as on it: the avoidance's own (dtObstacleAvoidanceQuery::prepare).
constexpr float kTouching = 0.01F;
/// Half: of a box's height, the half extent Detour looks within.
constexpr float kHalf = 0.5F;
/// How the avoidance weighs a velocity: dtCrowd::init's defaults (its adaptive sampling's divisions, rings
/// and depth; its bias toward the desired velocity; the weights of the desired and current velocities, of
/// a side and of the time of impact; and how far ahead it looks, in seconds).
constexpr dtObstacleAvoidanceParams kAvoidance{.velBias = 0.4F,
                                               .weightDesVel = 2.0F,
                                               .weightCurVel = 0.75F,
                                               .weightSide = 0.75F,
                                               .weightToi = 2.5F,
                                               .horizTime = 2.5F,
                                               .gridSize = 33,
                                               .adaptiveDivs = 7,
                                               .adaptiveRings = 2,
                                               .adaptiveDepth = 5};

/// `vector` of the game's space across the ground, in Detour's: its height left out.
[[nodiscard]] auto acrossOf(Vec3 vector) noexcept -> std::array<float, 3> {
    return detail::detourOf(Vec3{.x = vector.x, .y = vector.y});
}

/// Ends the process when Detour could not allocate `what`: out of memory, as a failed allocation of the
/// standard library ends it in a build without exceptions.
void endUnlessAllocated(bool isAllocated, std::string_view what) noexcept {
    if (!isAllocated) [[unlikely]] {
        nx::log::error(kLog, "out of memory for {}", what);
        std::abort();
    }
}

/// Detour's filter of `costs`, through the polygons agents may cross.
[[nodiscard]] auto filterOf(const AreaCosts& costs) -> dtQueryFilter {
    dtQueryFilter filter;
    filter.setIncludeFlags(detail::kWalkablePolygon);
    for (const AreaKind kind : AreaCosts::keys()) {
        filter.setAreaCost(detail::areaIdOf(kind), costs[kind]);
    }
    return filter;
}

/// A polygon an avoided place reaches, and how many times its own cost it costs (PathRequest::avoided).
struct AvoidedPolygon {
    dtPolyRef polygon = 0;
    float cost = 1.0F;
};

/// Detour's filter of a way: `base`'s, each polygon of `avoided` (sorted by polygon, each once, outliving
/// the filter) costing its share times its own, and no link it shuns. Detour is built with its filter virtual
/// for it
/// (DT_VIRTUAL_QUERYFILTER, cmake/ports/recastnavigation).
class WayFilter final : public dtQueryFilter {
public:
    WayFilter(const dtQueryFilter& base, std::span<const AvoidedPolygon> avoided,
              const PathRequest& request) noexcept
        : dtQueryFilter{base}, avoidedPolygons{avoided}, shunnedLinks{request.shunned} {}

    [[nodiscard]] auto passFilter(dtPolyRef polygonRef, const dtMeshTile* tile, const dtPoly* polygon) const
        -> bool override {
        if (!dtQueryFilter::passFilter(polygonRef, tile, polygon)) {
            return false;
        }
        if (polygon->getType() != DT_POLYTYPE_OFFMESH_CONNECTION) {
            return true;
        }
        return !isShunned(*tile, *polygon);
    }

    [[nodiscard]] auto getCost(const float* from, const float* to, dtPolyRef previous,
                               const dtMeshTile* previousTile, const dtPoly* previousPolygon,
                               dtPolyRef current, const dtMeshTile* currentTile, const dtPoly* currentPolygon,
                               dtPolyRef next, const dtMeshTile* nextTile, const dtPoly* nextPolygon) const
        -> float override {
        const float cost = dtQueryFilter::getCost(from, to, previous, previousTile, previousPolygon, current,
                                                  currentTile, currentPolygon, next, nextTile, nextPolygon);
        const auto avoided = std::ranges::lower_bound(avoidedPolygons, current, {}, &AvoidedPolygon::polygon);
        return avoided != avoidedPolygons.end() && avoided->polygon == current ? cost * avoided->cost : cost;
    }

private:
    [[nodiscard]] auto isShunned(const dtMeshTile& tile, const dtPoly& polygon) const -> bool {
        if (shunnedLinks.empty()) {
            return false;
        }
        const std::span vertices{tile.verts, narrow<std::size_t>(tile.header->vertCount) * kCoordinates};
        const auto endOf = [&](std::size_t end) {
            return detail::positionOf(
                vertices.subspan(std::size_t{polygon.verts[end]} * kCoordinates).first<kCoordinates>());
        };
        const Vec3 one = endOf(0);
        const Vec3 other = endOf(1);
        constexpr float kSameEnd = 0.5F;
        const auto isAt = [](Vec3 first, Vec3 second) {
            return length(first - second) <= kSameEnd;
        };
        return std::ranges::any_of(shunnedLinks, [&](const MeshLink& link) {
            return (isAt(link.start, one) && isAt(link.end, other)) ||
                   (isAt(link.start, other) && isAt(link.end, one));
        });
    }

    std::span<const AvoidedPolygon> avoidedPolygons;
    std::span<const MeshLink> shunnedLinks;
};

/// The bits that number `count` things, at least one.
[[nodiscard]] auto bitsToNumber(std::size_t count) noexcept -> std::uint32_t {
    return narrow<std::uint32_t>(std::bit_width(std::bit_ceil(std::max(count, std::size_t{1}))) - 1);
}

/// Point `index` of `points`, Detour's coordinates one after another.
[[nodiscard]] auto pointAt(std::span<const float> points, std::size_t index) -> std::span<const float, 3> {
    return points.subspan(index * kCoordinates).first<kCoordinates>();
}

/// The edges Detour gives the joins of a link's polygon to the floor at its start and at its end
/// (dtNavMesh::baseOffMeshLinks, connectExtOffMeshLinks).
constexpr unsigned char kLinkStartEdge = 0;
constexpr unsigned char kLinkEndEdge = 1;

/// Whether Detour joined the link of `polygon`, of `tile`, to the floor at both its ends: one a body
/// crosses. One it found no floor for at an end is no way.
[[nodiscard]] auto isJoined(const dtMeshTile& tile, const dtPoly& polygon) -> bool {
    bool isStartJoined = false;
    bool isEndJoined = false;
    for (unsigned int link = polygon.firstLink; link != DT_NULL_LINK; link = tile.links[link].next) {
        isStartJoined = isStartJoined || tile.links[link].edge == kLinkStartEdge;
        isEndJoined = isEndJoined || tile.links[link].edge == kLinkEndEdge;
    }
    return isStartJoined && isEndJoined;
}

/// The link from `start` to `end` of the polygon `polygon` of `connection`: a ladder's knows the ladder.
[[nodiscard]] auto linkOf(Vec3 start, Vec3 end, const dtPoly& polygon, const dtOffMeshConnection& connection)
    -> MeshLink {
    const AreaKind kind = detail::kindOfArea(polygon.getArea());
    return MeshLink{.start = start,
                    .end = end,
                    .kind = kind,
                    .ladder =
                        kind == AreaKind::Ladder ? detail::ladderOfId(connection.userId, start.z) : Vec3{}};
}

/// The corners of `polygon` of `tile`, in Detour's space, one point after another.
[[nodiscard]] auto cornersOf(const dtMeshTile& tile, const dtPoly& polygon) -> std::vector<float> {
    const std::span vertices{tile.verts, narrow<std::size_t>(tile.header->vertCount) * kCoordinates};
    std::vector<float> corners;
    corners.reserve(std::size_t{polygon.vertCount} * kCoordinates);
    for (const std::uint16_t vertex : std::span{polygon.verts}.first(polygon.vertCount)) {
        std::ranges::copy(pointAt(vertices, vertex), std::back_inserter(corners));
    }
    return corners;
}

/// The area of the polygon of `corners` across, Detour's x and z.
[[nodiscard]] auto areaAcross(std::span<const float> corners) -> float {
    const std::size_t count = corners.size() / kCoordinates;
    float area = 0.0F;
    for (std::size_t corner = 2; corner < count; ++corner) {
        area +=
            dtTriArea2D(corners.data(), pointAt(corners, corner - 1).data(), pointAt(corners, corner).data());
    }
    return std::abs(area);
}

[[nodiscard]] auto outside(Vec3 position) -> std::unexpected<Failure<NavigationError>> {
    return fail(NavigationError::OutsideMesh, std::format("{}", position));
}

/// A point on a polygon of a mesh, in Detour's space.
struct MeshPoint {
    dtPolyRef polygon = 0;
    std::array<float, 3> position{};
};

/// What starts the bytes of every mesh NavigationMesh::save writes, and the version of what follows: raise
/// it whenever their layout changes.
constexpr std::string_view kMeshFileMagic = "NXNAVMSH";
constexpr std::uint32_t kMeshFileVersion = 1;

/// The header of those bytes, little-endian: the magic, the version, four bytes of nothing, the key and
/// the hash of the body that follows it.
constexpr std::size_t kMeshFileVersionAt = kMeshFileMagic.size();
constexpr std::size_t kMeshFileKeyAt = kMeshFileVersionAt + (2 * sizeof(std::uint32_t));
constexpr std::size_t kMeshFileHashAt = kMeshFileKeyAt + sizeof(std::uint64_t);
constexpr std::size_t kMeshFileBodyAt = kMeshFileHashAt + sizeof(std::uint64_t);

/// The body begins with the grid, its first corner and a tile's side in Detour's space, and the tiles'
/// count; then each tile, its place in the grid, its polygons' count and its bytes' count, then its bytes.
constexpr std::size_t kGridBytes = (4 * sizeof(float)) + sizeof(std::uint32_t);
constexpr std::size_t kTileRecordBytes = 4 * sizeof(std::uint32_t);

[[nodiscard]] auto damaged(std::string detail) -> std::unexpected<Failure<MeshFileError>> {
    return fail(MeshFileError::Damaged, std::move(detail));
}

/// Reads a body's fields one after another, each little-endian, as long as the body holds them.
class BodyReader {
public:
    explicit BodyReader(std::span<const std::byte> body) noexcept : rest{body} {}

    /// The next `Value`, or none when the body ends first.
    template <class Value>
    [[nodiscard]] auto next() noexcept -> std::optional<Value> {
        if (rest.size() < sizeof(Value)) {
            return std::nullopt;
        }
        const auto value = loadLittleEndian<Value>(rest);
        rest = rest.subspan(sizeof(Value));
        return value;
    }

    /// The next `count` bytes, or none when the body ends first.
    [[nodiscard]] auto bytes(std::size_t count) noexcept -> std::optional<std::span<const std::byte>> {
        if (rest.size() < count) {
            return std::nullopt;
        }
        const std::span<const std::byte> taken = rest.first(count);
        rest = rest.subspan(count);
        return taken;
    }

    [[nodiscard]] inline auto left() const noexcept -> std::size_t { return rest.size(); }

private:
    std::span<const std::byte> rest;
};

/// Which polygons of a mesh a way leads from to which others, known without searching: a search for a way
/// that is not there goes through every polygon its start reaches before it can tell, the most any search
/// costs, however often it is asked, as bodies are of a player on a roof no way leads to. Links go one way
/// where a body only drops: the polygons each joined to the others both ways are a component (Tarjan's), and
/// each component knows those its links lead to, its own among them. It reads Detour's links, which every
/// filter of a query leaves as they are: a query's costs make a way dearer, never closed.
class PolygonReach {
public:
    explicit PolygonReach(const dtNavMesh& mesh) {
        firstOfTile.reserve(narrow<std::size_t>(mesh.getMaxTiles()));
        std::vector<Polygon> polygons;
        for (int index = 0; index < mesh.getMaxTiles(); ++index) {
            const dtMeshTile* const tile = mesh.getTile(index);
            firstOfTile.push_back(narrow<std::uint32_t>(polygons.size()));
            if (tile == nullptr || tile->header == nullptr) {
                continue;
            }
            for (const dtPoly& polygon :
                 std::span{tile->polys, narrow<std::size_t>(tile->header->polyCount)}) {
                polygons.push_back({.tile = tile, .firstLink = polygon.firstLink});
            }
        }
        const std::size_t componentCount = findComponents(mesh, polygons);
        wordsOfComponent = (componentCount + kBitsOfWord - 1) / kBitsOfWord;
        reached.assign(componentCount * wordsOfComponent, 0);
        leadOn(mesh, polygons, componentCount);
    }

    /// Whether a way leads from the polygon `from` to the polygon `to`, both of `mesh`.
    [[nodiscard]] auto leads(const dtNavMesh& mesh, dtPolyRef from, dtPolyRef to) const noexcept -> bool {
        const std::size_t target = componentOfPolygon[numberOf(mesh, to)];
        const std::uint64_t word =
            reached[(std::size_t{componentOfPolygon[numberOf(mesh, from)]} * wordsOfComponent) +
                    (target / kBitsOfWord)];
        return ((word >> (target % kBitsOfWord)) & 1U) != 0;
    }

private:
    static constexpr std::size_t kBitsOfWord = 64;
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    /// A polygon as the search of its components walks it.
    struct Polygon {
        const dtMeshTile* tile = nullptr;
        unsigned int firstLink = DT_NULL_LINK;
    };

    /// The polygon `reference`, of `mesh`, as numbered through its tiles.
    [[nodiscard]] auto numberOf(const dtNavMesh& mesh, dtPolyRef reference) const noexcept -> std::size_t {
        unsigned int salt = 0;
        unsigned int tile = 0;
        unsigned int polygon = 0;
        mesh.decodePolyId(reference, salt, tile, polygon);
        return std::size_t{firstOfTile[tile]} + polygon;
    }

    /// Writes each polygon's component to componentOfPolygon, those its links lead to numbered before it:
    /// how many they are.
    [[nodiscard]] auto findComponents(const dtNavMesh& mesh, std::span<const Polygon> polygons)
        -> std::size_t {
        // Tarjan's, its recursion held in a list: a mesh's ways are as deep as it has polygons.
        struct Visit {
            std::uint32_t polygon = 0;
            unsigned int nextLink = DT_NULL_LINK;
        };
        componentOfPolygon.assign(polygons.size(), kNone);
        std::vector<std::uint32_t> foundAt(polygons.size(), kNone);
        std::vector<std::uint32_t> lowest(polygons.size(), kNone);
        std::vector<std::uint32_t> open;
        std::vector<Visit> visits;
        std::uint32_t found = 0;
        std::uint32_t components = 0;
        const auto enter = [&](std::uint32_t polygon) {
            foundAt[polygon] = found;
            lowest[polygon] = found;
            ++found;
            open.push_back(polygon);
            visits.push_back({.polygon = polygon, .nextLink = polygons[polygon].firstLink});
        };
        for (std::uint32_t root = 0; root < polygons.size(); ++root) {
            if (foundAt[root] != kNone) {
                continue;
            }
            enter(root);
            while (!visits.empty()) {
                Visit& visit = visits.back();
                const std::uint32_t polygon = visit.polygon;
                if (visit.nextLink != DT_NULL_LINK) {
                    const dtLink& link = polygons[polygon].tile->links[visit.nextLink];
                    visit.nextLink = link.next;
                    const auto neighbor = narrow<std::uint32_t>(numberOf(mesh, link.ref));
                    if (foundAt[neighbor] == kNone) {
                        enter(neighbor);
                    } else if (componentOfPolygon[neighbor] == kNone) {
                        lowest[polygon] = std::min(lowest[polygon], foundAt[neighbor]);
                    }
                    continue;
                }
                visits.pop_back();
                if (!visits.empty()) {
                    lowest[visits.back().polygon] = std::min(lowest[visits.back().polygon], lowest[polygon]);
                }
                if (lowest[polygon] == foundAt[polygon]) {
                    std::uint32_t member = kNone;
                    do {
                        member = open.back();
                        open.pop_back();
                        componentOfPolygon[member] = components;
                    } while (member != polygon);
                    ++components;
                }
            }
        }
        return components;
    }

    /// Writes what each component leads to: itself, and what those its links lead to lead to, numbered before
    /// it.
    void leadOn(const dtNavMesh& mesh, std::span<const Polygon> polygons, std::size_t componentCount) {
        std::vector<std::vector<std::uint32_t>> nextOfComponent(componentCount);
        for (std::size_t polygon = 0; polygon < polygons.size(); ++polygon) {
            const std::uint32_t component = componentOfPolygon[polygon];
            for (unsigned int index = polygons[polygon].firstLink; index != DT_NULL_LINK;
                 index = polygons[polygon].tile->links[index].next) {
                const std::uint32_t next =
                    componentOfPolygon[numberOf(mesh, polygons[polygon].tile->links[index].ref)];
                if (next != component) {
                    nextOfComponent[component].push_back(next);
                }
            }
        }
        const std::span<std::uint64_t> words{reached};
        for (std::size_t component = 0; component < componentCount; ++component) {
            const std::span<std::uint64_t> own =
                words.subspan(component * wordsOfComponent, wordsOfComponent);
            own[component / kBitsOfWord] |= std::uint64_t{1} << (component % kBitsOfWord);
            for (const std::uint32_t next : nextOfComponent[component]) {
                const std::span<const std::uint64_t> onward =
                    words.subspan(std::size_t{next} * wordsOfComponent, wordsOfComponent);
                for (std::size_t word = 0; word < wordsOfComponent; ++word) {
                    own[word] |= onward[word];
                }
            }
        }
    }

    std::vector<std::uint32_t> firstOfTile; ///< Of each tile of the mesh, how many polygons come before.
    std::vector<std::uint32_t> componentOfPolygon; ///< By polygon, numbered through the tiles.
    std::vector<std::uint64_t> reached;            ///< A row of bits for each component, by component.
    std::size_t wordsOfComponent = 0;
};

/// Whether `data`, the bytes of the tile at `x`, `y` of `polygonCount` polygons, are a tile of the
/// Detour this unit is built with, of that place and count: what Detour takes without asking.
[[nodiscard]] auto isTileOf(std::span<const std::byte> data, std::int32_t x, std::int32_t y,
                            std::uint32_t polygonCount) noexcept -> bool {
    if (data.size() < sizeof(dtMeshHeader)) {
        return false;
    }
    dtMeshHeader header{};
    std::memcpy(&header, data.data(), sizeof(header));
    return header.magic == DT_NAVMESH_MAGIC && header.version == DT_NAVMESH_VERSION && header.x == x &&
           header.y == y && std::cmp_equal(header.polyCount, polygonCount);
}

} // namespace

struct NavigationMesh::Detour {
    /// The tiles' data, which Detour keeps pointers into: declared before its mesh, which goes first.
    std::vector<std::vector<std::byte>> tiles;
    std::unique_ptr<dtNavMesh, NavMeshFree> mesh;
    std::size_t polygonCount = 0;
    std::optional<PolygonReach> reach{}; ///< Once it has its tiles.
};

NavigationMesh::NavigationMesh(std::unique_ptr<Detour> detour, AgentProfile profile) noexcept
    : detourState{std::move(detour)}, agentProfile{std::move(profile)} {}

NavigationMesh::NavigationMesh(NavigationMesh&& other) noexcept = default;
auto NavigationMesh::operator=(NavigationMesh&& other) noexcept -> NavigationMesh& = default;
NavigationMesh::~NavigationMesh() = default;

struct MeshBuild::State {
    jobs::JobSystem* jobs = nullptr;
    std::shared_ptr<detail::TileWork> tiles{}; ///< Until the mesh is made of them.
    AgentProfile profile{};
    std::optional<Result<NavigationMesh, MeshBuildError>> mesh{}; ///< Once its job has run.
    jobs::JobHandle meshMade{};                                   ///< Of the last job.
};

MeshBuild::MeshBuild(std::unique_ptr<State> state) noexcept : buildState{std::move(state)} {}

MeshBuild::MeshBuild(MeshBuild&& other) noexcept = default;

MeshBuild::~MeshBuild() {
    if (buildState) {
        buildState->jobs->wait(buildState->meshMade);
    }
}

auto MeshBuild::finish() && -> Result<NavigationMesh, MeshBuildError> {
    expects(buildState != nullptr, "MeshBuild::finish: a build not finished yet");
    const std::unique_ptr<State> state = std::move(buildState);
    state->jobs->wait(state->meshMade);
    invariant(state->mesh.has_value(), "MeshBuild::finish: the mesh's job has run");
    return std::move(*state->mesh);
}

auto NavigationMesh::startBuild(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                                const BuildSettings& settings) -> Result<MeshBuild, MeshBuildError> {
    if (auto checked = checkSettings(settings).and_then([&] { return checkProfile(profile, settings); });
        !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    auto tiles = detail::startTiles(jobs, geometry, profile, settings, jobs::Priority::Background);
    if (!tiles) {
        return std::unexpected{std::move(tiles.error())};
    }
    auto state = std::make_unique<MeshBuild::State>(
        MeshBuild::State{.jobs = &jobs, .tiles = std::move(tiles->work), .profile = profile});
    // The state outlives the job: MeshBuild waits for it before it lets the state go.
    MeshBuild::State* const shared = state.get();
    const std::array tilesBuilt{tiles->done};
    state->meshMade = jobs.submit(
        {.label = "navigation.mesh", .priority = jobs::Priority::Background, .dependsOn = tilesBuilt},
        [shared] {
            shared->mesh = detail::takeTiles(*shared->tiles).and_then([shared](detail::BuiltTiles built) {
                return assemble(std::move(built), shared->profile);
            });
            shared->tiles.reset();
        });
    return MeshBuild{std::move(state)};
}

auto NavigationMesh::build(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                           const BuildSettings& settings) -> Result<NavigationMesh, MeshBuildError> {
    return startBuild(jobs, geometry, profile, settings).and_then([](MeshBuild started) {
        return std::move(started).finish();
    });
}

auto NavigationMesh::assemble(detail::BuiltTiles built, const AgentProfile& profile)
    -> Result<NavigationMesh, MeshBuildError> {
    const std::uint32_t tileBits = bitsToNumber(built.tiles.size());
    const std::uint32_t polygonBits = detail::kTileAndPolygonBits - tileBits;
    for (const detail::BuiltTile& tile : built.tiles) {
        if (tile.polygonCount > (std::uint32_t{1} << polygonBits)) {
            return fail(MeshBuildError::TooLarge,
                        std::format("tile {}, {} has {} polygons, more than the {} of each of {} tiles",
                                    tile.x, tile.y, tile.polygonCount, std::uint32_t{1} << polygonBits,
                                    built.tiles.size()));
        }
    }
    auto detour = std::make_unique<Detour>();
    detour->mesh.reset(dtAllocNavMesh());
    dtNavMeshParams params{};
    std::ranges::copy(built.origin, std::begin(params.orig));
    params.tileWidth = built.tileSide;
    params.tileHeight = built.tileSide;
    params.maxTiles = 1 << tileBits;
    params.maxPolys = 1 << polygonBits;
    endUnlessAllocated(detour->mesh != nullptr && dtStatusSucceed(detour->mesh->init(&params)), "a mesh");
    detour->tiles.reserve(built.tiles.size());
    for (detail::BuiltTile& tile : built.tiles) {
        std::vector<std::byte>& data = detour->tiles.emplace_back(std::move(tile.data));
        // The tile stays theirs (no DT_TILE_FREE_DATA): Detour writes its links into it.
        const dtStatus added = detour->mesh->addTile(reinterpret_cast<unsigned char*>(data.data()),
                                                     narrow<int>(data.size()), 0, 0, nullptr);
        invariant(dtStatusSucceed(added),
                  "NavigationMesh::assemble: Detour takes each tile Recast built for it");
        if (dtStatusSucceed(added)) {
            detour->polygonCount += tile.polygonCount;
        }
    }
    detour->reach.emplace(*detour->mesh);
    return NavigationMesh{std::move(detour), profile};
}

auto NavigationMesh::tileCount() const noexcept -> std::size_t {
    return detourState->tiles.size();
}

auto NavigationMesh::polygonCount() const noexcept -> std::size_t {
    return detourState->polygonCount;
}

auto NavigationMesh::outline() const -> MeshOutline {
    MeshOutline outline;
    outline.polygons.reserve(detourState->polygonCount);
    const dtNavMesh& mesh = *detourState->mesh;
    for (int index = 0; index < mesh.getMaxTiles(); ++index) {
        const dtMeshTile* const tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        const std::span polygons{tile->polys, narrow<std::size_t>(tile->header->polyCount)};
        for (std::size_t number = 0; number < polygons.size(); ++number) {
            const dtPoly& polygon = polygons[number];
            const std::vector<float> corners = cornersOf(*tile, polygon);
            if (polygon.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                // Its ends where Detour put them, on the polygons it joins.
                if (isJoined(*tile, polygon)) {
                    const dtOffMeshConnection& connection =
                        tile->offMeshCons[number - narrow<std::size_t>(tile->header->offMeshBase)];
                    outline.links.push_back(linkOf(detail::positionOf(pointAt(corners, 0)),
                                                   detail::positionOf(pointAt(corners, 1)), polygon,
                                                   connection));
                }
                continue;
            }
            outline.polygons.push_back(
                MeshPolygon{.firstCorner = narrow<std::uint32_t>(outline.corners.size()),
                            .cornerCount = polygon.vertCount,
                            .kind = detail::kindOfArea(polygon.getArea())});
            for (std::size_t corner = 0; corner < polygon.vertCount; ++corner) {
                outline.corners.push_back(detail::positionOf(pointAt(corners, corner)));
            }
        }
    }
    return outline;
}

auto NavigationMesh::save(MeshKey key) const -> std::vector<std::byte> {
    const dtNavMesh& mesh = *detourState->mesh;
    const dtNavMeshParams& params = *mesh.getParams();
    std::vector<std::byte> body;
    for (const float coordinate : params.orig) {
        appendLittleEndian(body, coordinate);
    }
    appendLittleEndian(body, params.tileWidth);
    appendLittleEndian(body, narrow<std::uint32_t>(detourState->tiles.size()));
    // In the order Detour holds them, which is the order they were added in: row by row.
    for (int index = 0; index < mesh.getMaxTiles(); ++index) {
        const dtMeshTile* const tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        appendLittleEndian(body, std::int32_t{tile->header->x});
        appendLittleEndian(body, std::int32_t{tile->header->y});
        appendLittleEndian(body, narrow<std::uint32_t>(tile->header->polyCount));
        appendLittleEndian(body, narrow<std::uint32_t>(tile->dataSize));
        const std::span data{tile->data, narrow<std::size_t>(tile->dataSize)};
        std::ranges::copy(std::as_bytes(data), std::back_inserter(body));
    }
    std::vector<std::byte> bytes;
    bytes.reserve(kMeshFileBodyAt + body.size());
    std::ranges::transform(kMeshFileMagic, std::back_inserter(bytes),
                           [](char letter) { return static_cast<std::byte>(letter); });
    appendLittleEndian(bytes, kMeshFileVersion);
    appendLittleEndian(bytes, std::uint32_t{0});
    appendLittleEndian(bytes, key.value);
    appendLittleEndian(bytes, hash64(body));
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

auto NavigationMesh::load(std::span<const std::byte> bytes, MeshKey key, const AgentProfile& profile)
    -> Result<NavigationMesh, MeshFileError> {
    const bool isMesh =
        bytes.size() >= kMeshFileBodyAt &&
        std::ranges::equal(bytes.first(kMeshFileMagic.size()), kMeshFileMagic, {}, {},
                           [](char letter) { return static_cast<std::byte>(letter); }) &&
        loadLittleEndian<std::uint32_t>(bytes.subspan(kMeshFileVersionAt)) == kMeshFileVersion;
    if (!isMesh) {
        return fail(MeshFileError::NotAMesh, std::format("{} bytes", bytes.size()));
    }
    if (const auto kept = loadLittleEndian<std::uint64_t>(bytes.subspan(kMeshFileKeyAt)); kept != key.value) {
        return fail(MeshFileError::OtherMesh,
                    std::format("the mesh of {:016x}, not of {:016x}", kept, key.value));
    }
    const std::span<const std::byte> body = bytes.subspan(kMeshFileBodyAt);
    if (hash64(body) != loadLittleEndian<std::uint64_t>(bytes.subspan(kMeshFileHashAt))) {
        return damaged("its body is not the one it was written with");
    }
    if (body.size() < kGridBytes) {
        return damaged(std::format("{} bytes of body, fewer than its grid", body.size()));
    }
    BodyReader reader{body};
    detail::BuiltTiles built;
    // The body holds the grid: none of its fields is missing.
    for (float& coordinate : built.origin) {
        coordinate = reader.next<float>().value_or(0.0F);
    }
    built.tileSide = reader.next<float>().value_or(0.0F);
    const std::uint32_t tileCount = reader.next<std::uint32_t>().value_or(0);
    if (tileCount > reader.left() / kTileRecordBytes) {
        return damaged(std::format("{} tiles in {} bytes", tileCount, reader.left()));
    }
    built.tiles.reserve(tileCount);
    std::set<std::pair<std::int32_t, std::int32_t>> places;
    for (std::uint32_t index = 0; index < tileCount; ++index) {
        const auto x = reader.next<std::int32_t>();
        const auto y = reader.next<std::int32_t>();
        const auto polygonCount = reader.next<std::uint32_t>();
        const auto byteCount = reader.next<std::uint32_t>();
        const auto data = byteCount ? reader.bytes(*byteCount) : std::nullopt;
        if (!data || !isTileOf(*data, *x, *y, *polygonCount) || !places.emplace(*x, *y).second) {
            return damaged(std::format("its tile {} of {}", index + 1, tileCount));
        }
        built.tiles.push_back(detail::BuiltTile{
            .x = *x, .y = *y, .polygonCount = *polygonCount, .data = {data->begin(), data->end()}});
    }
    if (reader.left() != 0) {
        return damaged(std::format("{} bytes after its last tile", reader.left()));
    }
    auto mesh = assemble(std::move(built), profile);
    if (!mesh) {
        return damaged(std::move(mesh.error().detail));
    }
    return std::move(*mesh);
}

struct NavigationQuery::Detour {
    std::unique_ptr<dtNavMeshQuery, NavMeshQueryFree> query;
    dtQueryFilter filter; ///< Of the profile's costs.
    /// Where about a point the polygon under it is looked for: a box's middle above the point, and the
    /// box's half extents.
    struct Reach {
        float above = 0.0F;
        std::array<float, 3> extents{};
    };
    /// The profile's standing box (NavigationQuery).
    Reach reach{};
    /// The profile's edge reach, looked within next, none being within reach (NavigationQuery).
    Reach edgeReach{};
    const PolygonReach* polygonReach = nullptr; ///< Of its mesh.
    std::vector<dtPolyRef> polygons;            ///< Room for a way's polygons, or those a circle reaches.
    std::vector<float> points;                  ///< Room for a way's corners.
    std::vector<AvoidedPolygon> avoided;        ///< Room for the polygons a way's avoided places reach.
    std::string profileName;
    /// The avoidance Corridor::steer samples velocities with, made as it is first asked for.
    std::unique_ptr<dtObstacleAvoidanceQuery, ObstacleAvoidanceFree> avoidance;

    /// The point of the mesh nearest `position`: within reach, else within the edge reach. Errors:
    /// OutsideMesh.
    [[nodiscard]] auto nearest(Vec3 position) const -> Result<MeshPoint, NavigationError> {
        if (!isFinite(position)) {
            return fail(NavigationError::InvalidQuery, "navigation coordinates must be finite");
        }
        for (const Reach* const extents : {&reach, &edgeReach}) {
            const std::array<float, 3> center = detail::detourOf(position + Vec3{.z = extents->above});
            MeshPoint found;
            const dtStatus status = query->findNearestPoly(center.data(), extents->extents.data(), &filter,
                                                           &found.polygon, found.position.data());
            if (dtStatusSucceed(status) && found.polygon != 0) {
                return found;
            }
        }
        return outside(position);
    }

    /// The polygons the places of `places` reach, sorted, each once with the most any of them makes it cost:
    /// those within a place's radius of the polygon nearest it. Valid until the next call.
    [[nodiscard]] auto avoidedPolygonsOf(std::span<const AvoidedPlace> places)
        -> std::span<const AvoidedPolygon> {
        avoided.clear();
        for (const AvoidedPlace& place : places) {
            const auto center = nearest(place.position);
            if (!center) {
                continue; // off the mesh, it avoids nothing
            }
            int count = 0;
            const dtStatus status = query->findPolysAroundCircle(
                center->polygon, center->position.data(), place.radius.value(), &filter, polygons.data(),
                nullptr, nullptr, &count, narrow<int>(polygons.size()));
            invariant(dtStatusSucceed(status), "NavigationQuery: a circle reaches the polygon it starts on");
            for (const dtPolyRef polygon : std::span{polygons}.first(narrow<std::size_t>(count))) {
                avoided.push_back({.polygon = polygon, .cost = place.cost});
            }
        }
        std::ranges::sort(avoided, [](const AvoidedPolygon& first, const AvoidedPolygon& second) {
            return first.polygon != second.polygon ? first.polygon < second.polygon
                                                   : first.cost > second.cost;
        });
        const auto repeated = std::ranges::unique(avoided, {}, &AvoidedPolygon::polygon);
        avoided.erase(repeated.begin(), repeated.end());
        return avoided;
    }

    /// The polygons of the cheapest way from `from` to `to` by `wayFilter`. Errors: Unreachable.
    [[nodiscard]] auto polygonPath(const MeshPoint& from, const MeshPoint& to, const dtQueryFilter& wayFilter)
        -> Result<std::span<const dtPolyRef>, NavigationError> {
        if (!polygonReach->leads(*query->getAttachedNavMesh(), from.polygon, to.polygon)) {
            return fail(NavigationError::Unreachable,
                        std::format("from {} to {} on {:?}'s mesh", detail::positionOf(from.position),
                                    detail::positionOf(to.position), profileName));
        }
        int count = 0;
        const dtStatus status =
            query->findPath(from.polygon, to.polygon, from.position.data(), to.position.data(), &wayFilter,
                            polygons.data(), &count, narrow<int>(polygons.size()));
        // A search that cannot reach the end gives the way to the polygon nearest it.
        if (dtStatusFailed(status) || count == 0 || polygons[narrow<std::size_t>(count) - 1] != to.polygon) {
            return fail(
                NavigationError::Unreachable,
                std::format("from {} to {} on {:?}'s mesh{}", detail::positionOf(from.position),
                            detail::positionOf(to.position), profileName,
                            dtStatusDetail(status, DT_OUT_OF_NODES)
                                ? std::format(", past the {} polygons a search reaches", polygons.size())
                                : std::string{}));
        }
        return std::span<const dtPolyRef>{polygons}.first(narrow<std::size_t>(count));
    }

    /// The height of the floor of `polygon` under `point`, or the point's own when it is not over it.
    [[nodiscard]] auto heightOn(dtPolyRef polygon, const std::array<float, 3>& point) const -> float {
        float height = point[1];
        if (dtStatusFailed(query->getPolyHeight(polygon, point.data(), &height))) {
            return point[1];
        }
        return height;
    }

private:
    friend class Corridor;
    std::vector<unsigned char> cornerFlags;
    std::vector<dtPolyRef> cornerPolygons;
};

NavigationQuery::NavigationQuery(const NavigationMesh& mesh)
    : detourState{std::make_unique<Detour>()}, navigationMesh{&mesh} {
    // A polygon entered across the edge of its tile is searched once for each side it is entered from.
    const std::size_t nodes = std::clamp(mesh.polygonCount() * narrow<std::size_t>(DT_MAX_STATES_PER_NODE),
                                         std::size_t{1}, kMostSearchNodes);
    detourState->query.reset(dtAllocNavMeshQuery());
    endUnlessAllocated(
        detourState->query != nullptr &&
            dtStatusSucceed(detourState->query->init(mesh.detourState->mesh.get(), narrow<int>(nodes))),
        "a search");
    const AgentProfile& profile = mesh.profile();
    detourState->filter = filterOf(areaCostsOf(profile));
    detourState->polygonReach = &*mesh.detourState->reach;
    detourState->reach = {
        .extents = {profile.radius.value(), profile.standingHeight.value(), profile.radius.value()}};
    // The radius the mesh keeps from the floor's end, and the body's width: the half of it past the floor's
    // end, and the rest for a cell a ledge may take off (rcFilterLedgeSpans) and the outline's
    // simplification. Up its standing height, down its drop height: the box's middle halfway between.
    const float width = profile.radius.value() + profile.radius.value();
    const float edgeAcross = profile.radius.value() + width;
    const float up = profile.standingHeight.value();
    const float down = std::max(profile.dropHeight.value(), up);
    detourState->edgeReach = {.above = (up - down) * kHalf,
                              .extents = {edgeAcross, (up + down) * kHalf, edgeAcross}};
    detourState->polygons.resize(nodes);
    detourState->profileName = profile.name;
}

NavigationQuery::NavigationQuery(NavigationQuery&& other) noexcept = default;
auto NavigationQuery::operator=(NavigationQuery&& other) noexcept -> NavigationQuery& = default;
NavigationQuery::~NavigationQuery() = default;

auto NavigationQuery::findPath(const PathRequest& request) -> Result<Path, NavigationError> {
    if (auto checked = checkRequest(request); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    const auto from = detourState->nearest(request.from);
    if (!from) {
        return std::unexpected{from.error()};
    }
    const auto to = detourState->nearest(request.to);
    if (!to) {
        return std::unexpected{to.error()};
    }
    const WayFilter filter{request.costs ? filterOf(*request.costs) : detourState->filter,
                           detourState->avoidedPolygonsOf(request.avoided), request};
    const auto polygons = detourState->polygonPath(*from, *to, filter);
    if (!polygons) {
        return std::unexpected{polygons.error()};
    }
    // The funnel adds a corner at most at each polygon it crosses, besides the start and the end.
    const std::size_t mostPoints = polygons->size() + 2;
    detourState->points.resize(mostPoints * kCoordinates);
    int count = 0;
    const dtStatus status = detourState->query->findStraightPath(
        from->position.data(), to->position.data(), polygons->data(), narrow<int>(polygons->size()),
        detourState->points.data(), nullptr, nullptr, &count, narrow<int>(mostPoints));
    invariant(dtStatusSucceed(status), "NavigationQuery::findPath: Detour straightens the way it found");
    Path path;
    path.points.reserve(narrow<std::size_t>(count));
    for (std::size_t point = 0; point < narrow<std::size_t>(count); ++point) {
        path.points.push_back(detail::positionOf(pointAt(detourState->points, point)));
    }
    return path;
}

auto NavigationQuery::nearestPoint(Vec3 position) -> Result<Vec3, NavigationError> {
    return detourState->nearest(position).transform(
        [](const MeshPoint& found) { return detail::positionOf(found.position); });
}

auto NavigationQuery::raycast(Vec3 from, Vec3 to) -> Result<StraightWalk, NavigationError> {
    if (!isFinite(to)) {
        return fail(NavigationError::InvalidQuery, "navigation coordinates must be finite");
    }
    const auto start = detourState->nearest(from);
    if (!start) {
        return std::unexpected{start.error()};
    }
    const std::array<float, 3> end = detail::detourOf(to);
    float hit = 0.0F;
    std::array<float, 3> wallNormal{};
    int count = 0;
    const dtStatus status = detourState->query->raycast(
        start->polygon, start->position.data(), end.data(), &detourState->filter, &hit, wallNormal.data(),
        detourState->polygons.data(), &count, narrow<int>(detourState->polygons.size()));
    invariant(dtStatusSucceed(status), "NavigationQuery::raycast: Detour walks from a polygon of its mesh");
    // Detour gives the most a float holds when nothing stops the walk.
    const float fraction = std::min(hit, 1.0F);
    std::array<float, 3> stop{};
    dtVlerp(stop.data(), start->position.data(), end.data(), fraction);
    if (count > 0) {
        stop[1] = detourState->heightOn(detourState->polygons[narrow<std::size_t>(count) - 1], stop);
    }
    return StraightWalk{.fraction = fraction, .end = detail::positionOf(stop)};
}

auto NavigationQuery::randomPoint(Vec3 center, HammerUnits radius, Random& random)
    -> Result<Vec3, NavigationError> {
    if (!std::isfinite(radius.value()) || radius < HammerUnits{}) {
        return fail(NavigationError::InvalidQuery, "a sampling radius must be finite and nonnegative");
    }
    const auto start = detourState->nearest(center);
    if (!start) {
        return std::unexpected{start.error()};
    }
    int count = 0;
    const dtStatus status = detourState->query->findPolysAroundCircle(
        start->polygon, start->position.data(), radius.value(), &detourState->filter,
        detourState->polygons.data(), nullptr, nullptr, &count, narrow<int>(detourState->polygons.size()));
    invariant(dtStatusSucceed(status) && count > 0,
              "NavigationQuery::randomPoint: a circle reaches the polygon it starts on");
    if (count <= 0) {
        return detail::positionOf(start->position);
    }
    // Each polygon as likely as its area: the first whose areas, added in order, pass the one drawn.
    const dtNavMesh& mesh = *detourState->query->getAttachedNavMesh();
    const auto polygonCorners = [&mesh](dtPolyRef reference) {
        const dtMeshTile* tile = nullptr;
        const dtPoly* polygon = nullptr;
        mesh.getTileAndPolyByRefUnsafe(reference, &tile, &polygon);
        return cornersOf(*tile, *polygon);
    };
    const std::span<const dtPolyRef> reached =
        std::span{detourState->polygons}.first(narrow<std::size_t>(count));
    std::vector<float> areas;
    areas.reserve(reached.size());
    for (const dtPolyRef reference : reached) {
        areas.push_back(areaAcross(polygonCorners(reference)));
    }
    double total = 0.0;
    for (const float area : areas) {
        total += double{area};
    }
    const double drawn = random.nextUnit() * total;
    std::size_t chosen = 0;
    double passed = double{areas.front()};
    while (passed <= drawn && chosen + 1 < areas.size()) {
        ++chosen;
        passed += double{areas[chosen]};
    }
    const std::vector<float> corners = polygonCorners(reached[chosen]);
    std::vector<float> triangleAreas(corners.size() / kCoordinates);
    std::array<float, 3> point{};
    const auto s = static_cast<float>(random.nextUnit());
    const auto t = static_cast<float>(random.nextUnit());
    dtRandomPointInConvexPoly(corners.data(), narrow<int>(triangleAreas.size()), triangleAreas.data(), s, t,
                              point.data());
    // Beyond the radius, it is drawn back along the polygon, towards its point nearest the center, which
    // the circle reaches: where that line crosses the circle.
    const float radiusSquared = radius.value() * radius.value();
    if (dtVdist2DSqr(point.data(), start->position.data()) > radiusSquared) {
        std::array<float, 3> nearest{};
        std::ignore =
            detourState->query->closestPointOnPoly(reached[chosen], start->position.data(), nearest.data(),
                                                   nullptr); // a polygon of the mesh has a nearest point
        const float alongX = point[0] - nearest[0];
        const float alongZ = point[2] - nearest[2];
        const float fromX = nearest[0] - start->position[0];
        const float fromZ = nearest[2] - start->position[2];
        const float a = (alongX * alongX) + (alongZ * alongZ);
        const float b = 2.0F * ((fromX * alongX) + (fromZ * alongZ));
        const float c = (fromX * fromX) + (fromZ * fromZ) - radiusSquared;
        const float share = (-b + std::sqrt(std::max((b * b) - (4.0F * a * c), 0.0F))) / (2.0F * a);
        dtVlerp(point.data(), nearest.data(), point.data(), std::clamp(share, 0.0F, 1.0F));
    }
    point[1] = detourState->heightOn(reached[chosen], point);
    return detail::positionOf(point);
}

auto NavigationQuery::isReachable(Vec3 from, Vec3 to) -> Result<bool, NavigationError> {
    const auto start = detourState->nearest(from);
    if (!start) {
        return std::unexpected{start.error()};
    }
    const auto end = detourState->nearest(to);
    if (!end) {
        return std::unexpected{end.error()};
    }
    return detourState->polygonPath(*start, *end, detourState->filter).has_value();
}

struct Corridor::Detour {
    dtPathCorridor corridor;
    dtLocalBoundary walls; ///< About the agent, which steer looks for again as it moves.
};

Corridor::Corridor(std::unique_ptr<Detour> detour, const NavigationMesh& mesh) noexcept
    : detourState{std::move(detour)}, navigationMesh{&mesh} {}

Corridor::Corridor(Corridor&& other) noexcept = default;
auto Corridor::operator=(Corridor&& other) noexcept -> Corridor& = default;
Corridor::~Corridor() = default;

auto Corridor::plan(NavigationQuery& query, const PathRequest& request) -> Result<Corridor, NavigationError> {
    if (auto checked = checkRequest(request); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    NavigationQuery::Detour& search = *query.detourState;
    const auto from = search.nearest(request.from);
    if (!from) {
        return std::unexpected{from.error()};
    }
    const auto to = search.nearest(request.to);
    if (!to) {
        return std::unexpected{to.error()};
    }
    const WayFilter filter{request.costs ? filterOf(*request.costs) : search.filter,
                           search.avoidedPolygonsOf(request.avoided), request};
    const auto polygons = search.polygonPath(*from, *to, filter);
    if (!polygons) {
        return std::unexpected{polygons.error()};
    }
    auto detour = std::make_unique<Detour>();
    endUnlessAllocated(detour->corridor.init(narrow<int>(polygons->size() + kCorridorRoom)), "a corridor");
    detour->corridor.reset(from->polygon, from->position.data());
    detour->corridor.setCorridor(to->position.data(), polygons->data(), narrow<int>(polygons->size()));
    return Corridor{std::move(detour), query.mesh()};
}

auto Corridor::position() const noexcept -> Vec3 {
    return detail::positionOf(std::span<const float, 3>{detourState->corridor.getPos(), kCoordinates});
}

auto Corridor::target() const noexcept -> Vec3 {
    return detail::positionOf(std::span<const float, 3>{detourState->corridor.getTarget(), kCoordinates});
}

auto Corridor::polygonCount() const noexcept -> std::size_t {
    return narrow<std::size_t>(detourState->corridor.getPathCount());
}

auto Corridor::moveAgent(NavigationQuery& query, Vec3 position) -> Vec3 {
    expects(&query.mesh() == navigationMesh, "Corridor::moveAgent: a query of the corridor's mesh");
    if (&query.mesh() == navigationMesh) {
        const std::array<float, 3> wanted = detail::detourOf(position);
        // False only when its first polygon left the mesh, which nothing changes yet.
        std::ignore = detourState->corridor.movePosition(wanted.data(), query.detourState->query.get(),
                                                         &query.detourState->filter);
    }
    return this->position();
}

auto Corridor::moveTarget(NavigationQuery& query, Vec3 target) -> Vec3 {
    expects(&query.mesh() == navigationMesh, "Corridor::moveTarget: a query of the corridor's mesh");
    if (&query.mesh() == navigationMesh) {
        const std::array<float, 3> wanted = detail::detourOf(target);
        const bool moved = detourState->corridor.moveTargetPosition(
            wanted.data(), query.detourState->query.get(), &query.detourState->filter);
        invariant(moved, "Corridor::moveTarget: its last polygon belongs to the immutable mesh");
        const std::span<const float, kCoordinates> clamped{detourState->corridor.getTarget(), kCoordinates};
        std::array<float, kCoordinates> grounded{};
        // Surface movement can end exactly on an edge, where getPolyHeight requires a strictly
        // interior point. Projection covers the boundary too and supplies its detailed floor height.
        const dtStatus projected = query.detourState->query->closestPointOnPoly(
            detourState->corridor.getLastPoly(), clamped.data(), grounded.data(), nullptr);
        invariant(dtStatusSucceed(projected), "Corridor::moveTarget: its final polygon belongs to the mesh");
        // Its setter also takes the path: reuse query scratch rather than aliasing memcpy's buffers.
        if (!std::ranges::equal(grounded, clamped)) {
            const std::span path{detourState->corridor.getPath(),
                                 narrow<std::size_t>(detourState->corridor.getPathCount())};
            invariant(path.size() <= query.detourState->polygons.size(),
                      "Corridor::moveTarget: query scratch holds the corridor's path");
            std::ranges::copy(path, query.detourState->polygons.begin());
            detourState->corridor.setCorridor(grounded.data(), query.detourState->polygons.data(),
                                              narrow<int>(path.size()));
        }
    }
    return this->target();
}

auto Corridor::corners(NavigationQuery& query, std::size_t count) -> std::vector<Vec3> {
    expects(&query.mesh() == navigationMesh, "Corridor::corners: a query of the corridor's mesh");
    if (&query.mesh() != navigationMesh || count == 0) {
        return {};
    }
    // The funnel adds at most one corner per polygon, besides the start and end. Detour drops the
    // agent's own point, so one more slot than requested suffices without allocating for a larger count.
    const std::size_t maxCorners = narrow<std::size_t>(detourState->corridor.getPathCount()) + 1;
    const std::size_t room = std::min(count, maxCorners) + 1;
    auto& scratch = *query.detourState;
    scratch.points.resize(room * kCoordinates);
    scratch.cornerFlags.resize(room);
    scratch.cornerPolygons.resize(room);
    const int found = detourState->corridor.findCorners(scratch.points.data(), scratch.cornerFlags.data(),
                                                        scratch.cornerPolygons.data(), narrow<int>(room),
                                                        scratch.query.get(), &scratch.filter);
    std::vector<Vec3> corners;
    const std::size_t returned = std::min(narrow<std::size_t>(found), count);
    corners.reserve(returned);
    for (std::size_t corner = 0; corner < returned; ++corner) {
        corners.push_back(detail::positionOf(pointAt(scratch.points, corner)));
    }
    return corners;
}

namespace {

/// A link a corridor crosses next (Corridor::nextLink), and its polygon.
struct NextLink {
    dtPolyRef polygon = 0;
    MeshLink link{};
};

/// The link `corridor` crosses next on `mesh`, its next corner the link's start, found by `query` and
/// `filter`: the way it crosses it, from the polygon before it in the corridor; none.
[[nodiscard]] auto nextLinkOf(dtPathCorridor& corridor, const dtNavMesh& mesh, dtNavMeshQuery& query,
                              const dtQueryFilter& filter) -> std::optional<NextLink> {
    // Room for the agent's own point, which Detour finds first and leaves out, and the next corner: a
    // link's start, the last it finds.
    constexpr int kNextCorner = 2;
    std::array<float, std::size_t{kNextCorner} * kCoordinates> points{};
    std::array<unsigned char, kNextCorner> flags{};
    std::array<dtPolyRef, kNextCorner> polygons{};
    const int found =
        corridor.findCorners(points.data(), flags.data(), polygons.data(), kNextCorner, &query, &filter);
    if (found < 1 || (flags[0] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) == 0) {
        return std::nullopt;
    }
    const std::span<const dtPolyRef> path{corridor.getPath(), narrow<std::size_t>(corridor.getPathCount())};
    const auto link = std::ranges::find(path, polygons[0]);
    if (link == path.begin() || link == path.end()) {
        return std::nullopt;
    }
    std::array<float, kCoordinates> start{};
    std::array<float, kCoordinates> end{};
    const dtMeshTile* tile = nullptr;
    const dtPoly* polygon = nullptr;
    if (dtStatusFailed(mesh.getOffMeshConnectionPolyEndPoints(*std::ranges::prev(link), *link, start.data(),
                                                              end.data())) ||
        dtStatusFailed(mesh.getTileAndPolyByRef(*link, &tile, &polygon))) {
        return std::nullopt;
    }
    const dtOffMeshConnection* const connection = mesh.getOffMeshConnectionByRef(*link);
    if (connection == nullptr) {
        return std::nullopt;
    }
    return NextLink{.polygon = *link,
                    .link =
                        linkOf(detail::positionOf(start), detail::positionOf(end), *polygon, *connection)};
}

} // namespace

auto Corridor::nextLink(NavigationQuery& query) -> std::optional<MeshLink> {
    expects(&query.mesh() == navigationMesh, "Corridor::nextLink: a query of the corridor's mesh");
    if (&query.mesh() != navigationMesh) {
        return std::nullopt;
    }
    return nextLinkOf(detourState->corridor, *query.detourState->query->getAttachedNavMesh(),
                      *query.detourState->query, query.detourState->filter)
        .transform([](const NextLink& next) { return next.link; });
}

auto Corridor::crouchesWithin(NavigationQuery& query, HammerUnits distance) const -> bool {
    expects(&query.mesh() == navigationMesh, "Corridor::crouchesWithin: a query of the corridor's mesh");
    const dtNavMesh& mesh = *query.detourState->query->getAttachedNavMesh();
    const float* const position = detourState->corridor.getPos();
    const float reachSquared = distance.value() * distance.value();
    const std::span<const dtPolyRef> path{detourState->corridor.getPath(),
                                          narrow<std::size_t>(detourState->corridor.getPathCount())};
    for (std::size_t step = 0; step < path.size(); ++step) {
        const dtMeshTile* tile = nullptr;
        const dtPoly* polygon = nullptr;
        mesh.getTileAndPolyByRefUnsafe(path[step], &tile, &polygon);
        if (polygon->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
            return false;
        }
        if (step > 0) {
            float nearestSquared = std::numeric_limits<float>::max();
            const std::span<const unsigned short> corners{polygon->verts, polygon->vertCount};
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                const float* const first = &tile->verts[std::size_t{corners[corner]} * kCoordinates];
                const float* const second =
                    &tile->verts[std::size_t{corners[(corner + 1) % corners.size()]} * kCoordinates];
                float along = 0.0F;
                nearestSquared =
                    std::min(nearestSquared, dtDistancePtSegSqr2D(position, first, second, along));
            }
            if (nearestSquared > reachSquared) {
                return false;
            }
        }
        if (detail::kindOfArea(polygon->getArea()) == AreaKind::Crouch) {
            return true;
        }
    }
    return false;
}

auto Corridor::crossLink(NavigationQuery& query) -> std::optional<MeshLink> {
    expects(&query.mesh() == navigationMesh, "Corridor::crossLink: a query of the corridor's mesh");
    if (&query.mesh() != navigationMesh) {
        return std::nullopt;
    }
    const std::optional<NextLink> next =
        nextLinkOf(detourState->corridor, *query.detourState->query->getAttachedNavMesh(),
                   *query.detourState->query, query.detourState->filter);
    if (!next) {
        return std::nullopt;
    }
    std::array<dtPolyRef, 2> crossed{}; // the polygons it leaves and lands on
    std::array<float, kCoordinates> start{};
    std::array<float, kCoordinates> end{};
    if (!detourState->corridor.moveOverOffmeshConnection(next->polygon, crossed.data(), start.data(),
                                                         end.data(), query.detourState->query.get())) {
        return std::nullopt;
    }
    return next->link;
}

void Corridor::appendWay(NavigationQuery& query, std::vector<Vec3>& points) const {
    expects(&query.mesh() == navigationMesh, "Corridor::appendWay: a query of the corridor's mesh");
    if (&query.mesh() != navigationMesh) {
        return;
    }
    const dtPathCorridor& corridor = detourState->corridor;
    // The funnel adds a corner at most at each polygon it crosses, besides the agent's and the target's.
    const std::size_t mostPoints = narrow<std::size_t>(corridor.getPathCount()) + 2;
    std::vector<float>& found = query.detourState->points;
    found.resize(mostPoints * kCoordinates);
    int count = 0;
    const dtStatus status = query.detourState->query->findStraightPath(
        corridor.getPos(), corridor.getTarget(), corridor.getPath(), corridor.getPathCount(), found.data(),
        nullptr, nullptr, &count, narrow<int>(mostPoints));
    invariant(dtStatusSucceed(status), "Corridor::appendWay: Detour straightens the corridor's polygons");
    for (std::size_t point = 0; point < narrow<std::size_t>(count); ++point) {
        points.push_back(detail::positionOf(pointAt(found, point)));
    }
}

auto Corridor::steer(NavigationQuery& query, const SteeringRequest& request) -> Vec3 {
    expects(&query.mesh() == navigationMesh, "Corridor::steer: a query of the corridor's mesh");
    if (&query.mesh() != navigationMesh) {
        return request.desired;
    }
    NavigationQuery::Detour& search = *query.detourState;
    if (!search.avoidance) {
        search.avoidance.reset(dtAllocObstacleAvoidanceQuery());
        endUnlessAllocated(search.avoidance != nullptr &&
                               search.avoidance->init(narrow<int>(kMostSteeringBodies), kMostSteeringWalls),
                           "an obstacle avoidance");
    }
    const float radius = navigationMesh->profile().radius.value();
    const float range = radius * kSteeringRangeRadii;
    const dtPathCorridor& corridor = detourState->corridor;
    const float* const position = corridor.getPos();
    dtLocalBoundary& walls = detourState->walls;
    if (dtVdist2DSqr(position, walls.getCenter()) > dtSqr(range * kWallsFoundAgainShare) ||
        !walls.isValid(search.query.get(), &search.filter)) {
        walls.update(corridor.getFirstPoly(), position, range, search.query.get(), &search.filter);
    }
    dtObstacleAvoidanceQuery& avoidance = *search.avoidance;
    avoidance.reset();
    for (const SteeringBody& body :
         request.bodies.first(std::min(request.bodies.size(), kMostSteeringBodies))) {
        const std::array<float, 3> feet = detail::detourOf(body.feet);
        // Over a body already, which so does not stop it, it goes its way: the avoidance takes any move
        // relative to one it overlaps for a collision and only going in step with it for none, so that two
        // pushed one way went on that way together, however far from where they were going, as long as they
        // overlapped.
        if (dtVdist2DSqr(position, feet.data()) < dtSqr(radius + body.radius.value())) {
            continue;
        }
        const std::array<float, 3> velocity = acrossOf(body.velocity);
        avoidance.addCircle(feet.data(), body.radius.value(), velocity.data(), velocity.data());
    }
    // The ledge the link it crosses next leaves from is its way on, no wall to keep off: the link's start is
    // on it, or just inside it where it is the side of a tile.
    constexpr float kOnLedge = detail::kLinkInsetFromTileSide + kTouching;
    const std::optional<std::array<float, 3>> linkStart =
        nextLinkOf(detourState->corridor, *search.query->getAttachedNavMesh(), *search.query, search.filter)
            .transform([](const NextLink& next) { return detail::detourOf(next.link.start); });
    for (int wall = 0; wall < walls.getSegmentCount(); ++wall) {
        const float* const segment = walls.getSegment(wall);
        // A wall it has behind it, it is leaving: DetourCrowd leaves it out too.
        if (dtTriArea2D(position, segment, segment + kCoordinates) < 0.0F) {
            continue;
        }
        float along = 0.0F;
        if (linkStart && dtDistancePtSegSqr2D(linkStart->data(), segment, segment + kCoordinates, along) <
                             kOnLedge * kOnLedge) {
            continue;
        }
        avoidance.addSegment(segment, segment + kCoordinates);
    }
    const std::array<float, 3> velocity = acrossOf(request.velocity);
    const std::array<float, 3> desired = acrossOf(request.desired);
    std::array<float, 3> chosen{};
    avoidance.sampleVelocityAdaptive(position, radius, request.maxSpeed.value(), velocity.data(),
                                     desired.data(), chosen.data(), &kAvoidance);
    return detail::positionOf(chosen);
}

NavigationMeshes::NavigationMeshes(std::vector<AgentProfile> profiles, BuildSettings settings) noexcept
    : agentProfiles{std::move(profiles)}, buildSettings{settings}, profileMeshes(agentProfiles.size()) {}

auto NavigationMeshes::create(std::vector<AgentProfile> profiles, BuildSettings settings)
    -> Result<NavigationMeshes, MeshBuildError> {
    if (auto checked = checkSettings(settings); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    std::set<std::string_view> names;
    for (const AgentProfile& profile : profiles) {
        if (auto checked = checkProfile(profile, settings); !checked) {
            return std::unexpected{std::move(checked.error())};
        }
        if (!names.insert(profile.name).second) {
            return fail(MeshBuildError::InvalidProfile, std::format("two profiles named {:?}", profile.name));
        }
    }
    return NavigationMeshes{std::move(profiles), settings};
}

auto NavigationMeshes::profileNamed(std::string_view name) const -> Result<ProfileIndex, NavigationError> {
    const auto found = std::ranges::find(agentProfiles, name, &AgentProfile::name);
    if (found == agentProfiles.end()) {
        return fail(NavigationError::UnknownProfile, std::format("{:?}", name));
    }
    return ProfileIndex{narrow<std::uint32_t>(found - agentProfiles.begin())};
}

auto NavigationMeshes::build(jobs::JobSystem& jobs, const Geometry& geometry, ProfileIndex profile)
    -> Result<void, MeshBuildError> {
    expects(profile.value < agentProfiles.size(), "NavigationMeshes::build: one of its profiles");
    if (profile.value >= agentProfiles.size()) {
        return {};
    }
    auto mesh = NavigationMesh::build(jobs, geometry, agentProfiles[profile.value], buildSettings);
    if (!mesh) {
        return std::unexpected{std::move(mesh.error())};
    }
    profileMeshes[profile.value].emplace(std::move(*mesh));
    return {};
}

auto NavigationMeshes::meshOf(ProfileIndex profile) const
    -> Result<std::reference_wrapper<const NavigationMesh>, NavigationError> {
    expects(profile.value < agentProfiles.size(), "NavigationMeshes::meshOf: one of its profiles");
    if (profile.value >= agentProfiles.size() || !profileMeshes[profile.value]) {
        return fail(NavigationError::NoMesh,
                    std::format("profile {:?}", profile.value < agentProfiles.size()
                                                    ? agentProfiles[profile.value].name
                                                    : std::string{}));
    }
    return std::cref(*profileMeshes[profile.value]);
}

void NavigationMeshes::clear() noexcept {
    for (std::optional<NavigationMesh>& mesh : profileMeshes) {
        mesh.reset();
    }
}

} // namespace nx::navigation
