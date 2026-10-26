// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// Detour stays in this unit (docs/02-coding-standards.md, section 2): the meshes, queries and
// corridors of the interface hold it behind their own types.
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourNode.h>
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

/// A point of Detour's space has three coordinates.
constexpr std::size_t kCoordinates = 3;

struct NavMeshFree {
    void operator()(dtNavMesh* mesh) const noexcept { dtFreeNavMesh(mesh); }
};

struct NavMeshQueryFree {
    void operator()(dtNavMeshQuery* query) const noexcept { dtFreeNavMeshQuery(query); }
};

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

/// The bits that number `count` things, at least one.
[[nodiscard]] auto bitsToNumber(std::size_t count) noexcept -> std::uint32_t {
    return narrow<std::uint32_t>(std::bit_width(std::bit_ceil(std::max(count, std::size_t{1}))) - 1);
}

/// Point `index` of `points`, Detour's coordinates one after another.
[[nodiscard]] auto pointAt(std::span<const float> points, std::size_t index) -> std::span<const float, 3> {
    return points.subspan(index * kCoordinates).first<kCoordinates>();
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

} // namespace

struct NavigationMesh::Detour {
    /// The tiles' data, which Detour keeps pointers into: declared before its mesh, which goes first.
    std::vector<std::vector<std::byte>> tiles;
    std::unique_ptr<dtNavMesh, NavMeshFree> mesh;
    std::size_t polygonCount = 0;
};

NavigationMesh::NavigationMesh(std::unique_ptr<Detour> detour, AgentProfile profile) noexcept
    : m_detour{std::move(detour)}, m_profile{std::move(profile)} {}

NavigationMesh::NavigationMesh(NavigationMesh&& other) noexcept = default;
auto NavigationMesh::operator=(NavigationMesh&& other) noexcept -> NavigationMesh& = default;
NavigationMesh::~NavigationMesh() = default;

auto NavigationMesh::build(jobs::JobSystem& jobs, const Geometry& geometry, const AgentProfile& profile,
                           const BuildSettings& settings) -> Result<NavigationMesh, MeshBuildError> {
    if (auto checked = checkSettings(settings).and_then([&] { return checkProfile(profile, settings); });
        !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    auto built = detail::buildTiles(jobs, geometry, profile, settings);
    if (!built) {
        return std::unexpected{std::move(built.error())};
    }
    const std::uint32_t tileBits = bitsToNumber(built->tiles.size());
    const std::uint32_t polygonBits = detail::kTileAndPolygonBits - tileBits;
    for (const detail::BuiltTile& tile : built->tiles) {
        if (tile.polygonCount > (std::uint32_t{1} << polygonBits)) {
            return fail(MeshBuildError::TooLarge,
                        std::format("tile {}, {} has {} polygons, more than the {} of each of {} tiles",
                                    tile.x, tile.y, tile.polygonCount, std::uint32_t{1} << polygonBits,
                                    built->tiles.size()));
        }
    }
    auto detour = std::make_unique<Detour>();
    detour->mesh.reset(dtAllocNavMesh());
    dtNavMeshParams params{};
    std::ranges::copy(built->origin, std::begin(params.orig));
    params.tileWidth = built->tileSide;
    params.tileHeight = built->tileSide;
    params.maxTiles = 1 << tileBits;
    params.maxPolys = 1 << polygonBits;
    endUnlessAllocated(detour->mesh != nullptr && dtStatusSucceed(detour->mesh->init(&params)), "a mesh");
    detour->tiles.reserve(built->tiles.size());
    for (detail::BuiltTile& tile : built->tiles) {
        std::vector<std::byte>& data = detour->tiles.emplace_back(std::move(tile.data));
        // The tile stays theirs (no DT_TILE_FREE_DATA): Detour writes its links into it.
        const dtStatus added = detour->mesh->addTile(reinterpret_cast<unsigned char*>(data.data()),
                                                     narrow<int>(data.size()), 0, 0, nullptr);
        invariant(dtStatusSucceed(added),
                  "NavigationMesh::build: Detour takes each tile Recast built for it");
        if (dtStatusSucceed(added)) {
            detour->polygonCount += tile.polygonCount;
        }
    }
    return NavigationMesh{std::move(detour), profile};
}

auto NavigationMesh::tileCount() const noexcept -> std::size_t {
    return m_detour->tiles.size();
}

auto NavigationMesh::polygonCount() const noexcept -> std::size_t {
    return m_detour->polygonCount;
}

auto NavigationMesh::outline() const -> MeshOutline {
    MeshOutline outline;
    outline.polygons.reserve(m_detour->polygonCount);
    const dtNavMesh& mesh = *m_detour->mesh;
    for (int index = 0; index < mesh.getMaxTiles(); ++index) {
        const dtMeshTile* const tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        for (const dtPoly& polygon : std::span{tile->polys, narrow<std::size_t>(tile->header->polyCount)}) {
            const std::vector<float> corners = cornersOf(*tile, polygon);
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

struct NavigationQuery::Detour {
    std::unique_ptr<dtNavMeshQuery, NavMeshQueryFree> query;
    dtQueryFilter filter; ///< Of the profile's costs.
    /// How far from a point the polygon under it is looked for: the profile's standing box.
    std::array<float, 3> reach{};
    std::vector<dtPolyRef> polygons; ///< Room for a way's polygons, or those a circle reaches.
    std::vector<float> points;       ///< Room for a way's corners.
    std::string profileName;

    /// The point of the mesh nearest `position`. Errors: OutsideMesh.
    [[nodiscard]] auto nearest(Vec3 position) const -> Result<MeshPoint, NavigationError> {
        const std::array<float, 3> center = detail::detourOf(position);
        MeshPoint found;
        const dtStatus status = query->findNearestPoly(center.data(), reach.data(), &filter, &found.polygon,
                                                       found.position.data());
        if (dtStatusFailed(status) || found.polygon == 0) {
            return outside(position);
        }
        return found;
    }

    /// The polygons of the cheapest way from `from` to `to` by `wayFilter`. Errors: Unreachable.
    [[nodiscard]] auto polygonPath(const MeshPoint& from, const MeshPoint& to, const dtQueryFilter& wayFilter)
        -> Result<std::span<const dtPolyRef>, NavigationError> {
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
};

NavigationQuery::NavigationQuery(const NavigationMesh& mesh)
    : m_detour{std::make_unique<Detour>()}, m_mesh{&mesh} {
    // A polygon entered across the edge of its tile is searched once for each side it is entered from.
    const std::size_t nodes = std::clamp(mesh.polygonCount() * narrow<std::size_t>(DT_MAX_STATES_PER_NODE),
                                         std::size_t{1}, kMostSearchNodes);
    m_detour->query.reset(dtAllocNavMeshQuery());
    endUnlessAllocated(m_detour->query != nullptr && dtStatusSucceed(m_detour->query->init(
                                                         mesh.m_detour->mesh.get(), narrow<int>(nodes))),
                       "a search");
    const AgentProfile& profile = mesh.profile();
    m_detour->filter = filterOf(areaCostsOf(profile));
    m_detour->reach = {profile.radius.value(), profile.standingHeight.value(), profile.radius.value()};
    m_detour->polygons.resize(nodes);
    m_detour->profileName = profile.name;
}

NavigationQuery::NavigationQuery(NavigationQuery&& other) noexcept = default;
auto NavigationQuery::operator=(NavigationQuery&& other) noexcept -> NavigationQuery& = default;
NavigationQuery::~NavigationQuery() = default;

auto NavigationQuery::findPath(const PathRequest& request) -> Result<Path, NavigationError> {
    const auto from = m_detour->nearest(request.from);
    if (!from) {
        return std::unexpected{from.error()};
    }
    const auto to = m_detour->nearest(request.to);
    if (!to) {
        return std::unexpected{to.error()};
    }
    const dtQueryFilter filter = request.costs ? filterOf(*request.costs) : m_detour->filter;
    const auto polygons = m_detour->polygonPath(*from, *to, filter);
    if (!polygons) {
        return std::unexpected{polygons.error()};
    }
    // The funnel adds a corner at most at each polygon it crosses, besides the start and the end.
    const std::size_t mostPoints = polygons->size() + 2;
    m_detour->points.resize(mostPoints * kCoordinates);
    int count = 0;
    const dtStatus status = m_detour->query->findStraightPath(
        from->position.data(), to->position.data(), polygons->data(), narrow<int>(polygons->size()),
        m_detour->points.data(), nullptr, nullptr, &count, narrow<int>(mostPoints));
    invariant(dtStatusSucceed(status), "NavigationQuery::findPath: Detour straightens the way it found");
    Path path;
    path.points.reserve(narrow<std::size_t>(count));
    for (std::size_t point = 0; point < narrow<std::size_t>(count); ++point) {
        path.points.push_back(detail::positionOf(pointAt(m_detour->points, point)));
    }
    return path;
}

auto NavigationQuery::nearestPoint(Vec3 position) -> Result<Vec3, NavigationError> {
    return m_detour->nearest(position).transform(
        [](const MeshPoint& found) { return detail::positionOf(found.position); });
}

auto NavigationQuery::raycast(Vec3 from, Vec3 to) -> Result<StraightWalk, NavigationError> {
    const auto start = m_detour->nearest(from);
    if (!start) {
        return std::unexpected{start.error()};
    }
    const std::array<float, 3> end = detail::detourOf(to);
    float hit = 0.0F;
    std::array<float, 3> wallNormal{};
    int count = 0;
    const dtStatus status = m_detour->query->raycast(
        start->polygon, start->position.data(), end.data(), &m_detour->filter, &hit, wallNormal.data(),
        m_detour->polygons.data(), &count, narrow<int>(m_detour->polygons.size()));
    invariant(dtStatusSucceed(status), "NavigationQuery::raycast: Detour walks from a polygon of its mesh");
    // Detour gives the most a float holds when nothing stops the walk.
    const float fraction = std::min(hit, 1.0F);
    std::array<float, 3> stop{};
    dtVlerp(stop.data(), start->position.data(), end.data(), fraction);
    if (count > 0) {
        stop[1] = m_detour->heightOn(m_detour->polygons[narrow<std::size_t>(count) - 1], stop);
    }
    return StraightWalk{.fraction = fraction, .end = detail::positionOf(stop)};
}

auto NavigationQuery::randomPoint(Vec3 center, HammerUnits radius, Random& random)
    -> Result<Vec3, NavigationError> {
    const auto start = m_detour->nearest(center);
    if (!start) {
        return std::unexpected{start.error()};
    }
    int count = 0;
    const dtStatus status = m_detour->query->findPolysAroundCircle(
        start->polygon, start->position.data(), radius.value(), &m_detour->filter, m_detour->polygons.data(),
        nullptr, nullptr, &count, narrow<int>(m_detour->polygons.size()));
    invariant(dtStatusSucceed(status) && count > 0,
              "NavigationQuery::randomPoint: a circle reaches the polygon it starts on");
    if (count <= 0) {
        return detail::positionOf(start->position);
    }
    // Each polygon as likely as its area: the first whose areas, added in order, pass the one drawn.
    const dtNavMesh& mesh = *m_detour->query->getAttachedNavMesh();
    const auto polygonCorners = [&mesh](dtPolyRef reference) {
        const dtMeshTile* tile = nullptr;
        const dtPoly* polygon = nullptr;
        mesh.getTileAndPolyByRefUnsafe(reference, &tile, &polygon);
        return cornersOf(*tile, *polygon);
    };
    const std::span<const dtPolyRef> reached =
        std::span{m_detour->polygons}.first(narrow<std::size_t>(count));
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
            m_detour->query->closestPointOnPoly(reached[chosen], start->position.data(), nearest.data(),
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
    point[1] = m_detour->heightOn(reached[chosen], point);
    return detail::positionOf(point);
}

auto NavigationQuery::isReachable(Vec3 from, Vec3 to) -> Result<bool, NavigationError> {
    const auto start = m_detour->nearest(from);
    if (!start) {
        return std::unexpected{start.error()};
    }
    const auto end = m_detour->nearest(to);
    if (!end) {
        return std::unexpected{end.error()};
    }
    return m_detour->polygonPath(*start, *end, m_detour->filter).has_value();
}

struct Corridor::Detour {
    dtPathCorridor corridor;
};

Corridor::Corridor(std::unique_ptr<Detour> detour, const NavigationMesh& mesh) noexcept
    : m_detour{std::move(detour)}, m_mesh{&mesh} {}

Corridor::Corridor(Corridor&& other) noexcept = default;
auto Corridor::operator=(Corridor&& other) noexcept -> Corridor& = default;
Corridor::~Corridor() = default;

auto Corridor::plan(NavigationQuery& query, const PathRequest& request) -> Result<Corridor, NavigationError> {
    NavigationQuery::Detour& search = *query.m_detour;
    const auto from = search.nearest(request.from);
    if (!from) {
        return std::unexpected{from.error()};
    }
    const auto to = search.nearest(request.to);
    if (!to) {
        return std::unexpected{to.error()};
    }
    const dtQueryFilter filter = request.costs ? filterOf(*request.costs) : search.filter;
    const auto polygons = search.polygonPath(*from, *to, filter);
    if (!polygons) {
        return std::unexpected{polygons.error()};
    }
    auto detour = std::make_unique<Detour>();
    endUnlessAllocated(detour->corridor.init(narrow<int>(search.polygons.size())), "a corridor");
    detour->corridor.reset(from->polygon, from->position.data());
    detour->corridor.setCorridor(to->position.data(), polygons->data(), narrow<int>(polygons->size()));
    return Corridor{std::move(detour), query.mesh()};
}

auto Corridor::position() const noexcept -> Vec3 {
    return detail::positionOf(std::span<const float, 3>{m_detour->corridor.getPos(), kCoordinates});
}

auto Corridor::target() const noexcept -> Vec3 {
    return detail::positionOf(std::span<const float, 3>{m_detour->corridor.getTarget(), kCoordinates});
}

auto Corridor::polygonCount() const noexcept -> std::size_t {
    return narrow<std::size_t>(m_detour->corridor.getPathCount());
}

auto Corridor::moveAgent(NavigationQuery& query, Vec3 position) -> Vec3 {
    expects(&query.mesh() == m_mesh, "Corridor::moveAgent: a query of the corridor's mesh");
    if (&query.mesh() == m_mesh) {
        const std::array<float, 3> wanted = detail::detourOf(position);
        // False only when its first polygon left the mesh, which nothing changes yet.
        std::ignore = m_detour->corridor.movePosition(wanted.data(), query.m_detour->query.get(),
                                                      &query.m_detour->filter);
    }
    return this->position();
}

auto Corridor::moveTarget(NavigationQuery& query, Vec3 target) -> Vec3 {
    expects(&query.mesh() == m_mesh, "Corridor::moveTarget: a query of the corridor's mesh");
    if (&query.mesh() == m_mesh) {
        const std::array<float, 3> wanted = detail::detourOf(target);
        // False only when its last polygon left the mesh, which nothing changes yet.
        std::ignore = m_detour->corridor.moveTargetPosition(wanted.data(), query.m_detour->query.get(),
                                                            &query.m_detour->filter);
    }
    return this->target();
}

auto Corridor::corners(NavigationQuery& query, std::size_t count) -> std::vector<Vec3> {
    expects(&query.mesh() == m_mesh, "Corridor::corners: a query of the corridor's mesh");
    if (&query.mesh() != m_mesh || count == 0) {
        return {};
    }
    // Detour finds the way's points from the agent's own, which it then drops: one more than the corners.
    const std::size_t room = count + 1;
    std::vector<float> points(room * kCoordinates);
    std::vector<unsigned char> flags(room);
    std::vector<dtPolyRef> polygons(room);
    const int found =
        m_detour->corridor.findCorners(points.data(), flags.data(), polygons.data(), narrow<int>(room),
                                       query.m_detour->query.get(), &query.m_detour->filter);
    std::vector<Vec3> corners;
    corners.reserve(count);
    for (std::size_t corner = 0; corner < std::min(narrow<std::size_t>(found), count); ++corner) {
        corners.push_back(detail::positionOf(pointAt(points, corner)));
    }
    return corners;
}

NavigationMeshes::NavigationMeshes(std::vector<AgentProfile> profiles, BuildSettings settings) noexcept
    : m_profiles{std::move(profiles)}, m_settings{settings}, m_meshes(m_profiles.size()) {}

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
    const auto found = std::ranges::find(m_profiles, name, &AgentProfile::name);
    if (found == m_profiles.end()) {
        return fail(NavigationError::UnknownProfile, std::format("{:?}", name));
    }
    return ProfileIndex{narrow<std::uint32_t>(found - m_profiles.begin())};
}

auto NavigationMeshes::build(jobs::JobSystem& jobs, const Geometry& geometry, ProfileIndex profile)
    -> Result<void, MeshBuildError> {
    expects(profile.value < m_profiles.size(), "NavigationMeshes::build: one of its profiles");
    if (profile.value >= m_profiles.size()) {
        return {};
    }
    auto mesh = NavigationMesh::build(jobs, geometry, m_profiles[profile.value], m_settings);
    if (!mesh) {
        return std::unexpected{std::move(mesh.error())};
    }
    m_meshes[profile.value].emplace(std::move(*mesh));
    return {};
}

auto NavigationMeshes::meshOf(ProfileIndex profile) const
    -> Result<std::reference_wrapper<const NavigationMesh>, NavigationError> {
    expects(profile.value < m_profiles.size(), "NavigationMeshes::meshOf: one of its profiles");
    if (profile.value >= m_profiles.size() || !m_meshes[profile.value]) {
        return fail(NavigationError::NoMesh, std::format("profile {:?}", profile.value < m_profiles.size()
                                                                             ? m_profiles[profile.value].name
                                                                             : std::string{}));
    }
    return std::cref(*m_meshes[profile.value]);
}

void NavigationMeshes::clear() noexcept {
    for (std::optional<NavigationMesh>& mesh : m_meshes) {
        mesh.reset();
    }
}

} // namespace nx::navigation
