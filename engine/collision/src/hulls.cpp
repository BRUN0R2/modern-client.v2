// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.collision;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace nx::collision {

namespace {

/// How far before a plane a trace stops: the original's DIST_EPSILON, 1/32 unit.
constexpr float kStopDistance = 0.03125F;
/// How far back along its way the server moves a stop it finds in solid, at a time; and player
/// movement, which adds it in double precision.
constexpr float kServerBackup = 0.1F;
constexpr double kMovementBackup = 0.05;

/// The hulls of a map's clip nodes, in the order its models list their roots.
constexpr std::array<Hull, assets::kClipHulls> kClipNodeHulls{Hull::Standing, Hull::Large, Hull::Ducked};

/// The rules a trace follows: the server's or player movement's.
enum class Rules : std::uint8_t {
    Server,
    Movement,
};

/// Checks the nodes of one tree: the planes and nodes they name exist, no node is under itself and
/// no path is deeper than kMaxHullDepth. Errors: BadIndex, Cycle, TooDeep.
[[nodiscard]] auto checkTree(std::span<const HullNode> nodes, std::size_t planes, std::string_view tree)
    -> Result<void, CollisionError> {
    std::vector<std::uint32_t> parents(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].plane.value >= planes) {
            return fail(CollisionError::BadIndex, std::format("{} node {}: plane {} of {}", tree, index,
                                                              nodes[index].plane.value, planes));
        }
        for (const HullChild& child : nodes[index].children) {
            if (const auto* node = std::get_if<HullNodeIndex>(&child)) {
                if (node->value >= nodes.size()) {
                    return fail(CollisionError::BadIndex, std::format("{} node {}: child {} of {}", tree,
                                                                      index, node->value, nodes.size()));
                }
                ++parents[node->value];
            }
        }
    }
    // Parents before their children (Kahn); then the depth of each node, from its children up.
    std::vector<std::uint32_t> order;
    order.reserve(nodes.size());
    for (std::uint32_t index = 0; index < nodes.size(); ++index) {
        if (parents[index] == 0) {
            order.push_back(index);
        }
    }
    for (std::size_t next = 0; next < order.size(); ++next) {
        for (const HullChild& child : nodes[order[next]].children) {
            if (const auto* node = std::get_if<HullNodeIndex>(&child);
                node != nullptr && --parents[node->value] == 0) {
                order.push_back(node->value);
            }
        }
    }
    if (order.size() != nodes.size()) {
        return fail(CollisionError::Cycle, std::format("{}: {} of {} nodes are in a cycle, or under one",
                                                       tree, nodes.size() - order.size(), nodes.size()));
    }
    std::vector<std::size_t> depths(nodes.size(), 1);
    for (const std::uint32_t index : order | std::views::reverse) {
        for (const HullChild& child : nodes[index].children) {
            if (const auto* node = std::get_if<HullNodeIndex>(&child)) {
                depths[index] = std::max(depths[index], depths[node->value] + 1);
            }
        }
        if (depths[index] > kMaxHullDepth) {
            return fail(CollisionError::TooDeep, std::format("{} node {}: {} nodes deep, more than {}", tree,
                                                             index, depths[index], kMaxHullDepth));
        }
    }
    return {};
}

/// What `point` is in, under `child` of a tree (SV_HullPointContents; PM_HullPointContents but for its
/// quirk).
[[nodiscard]] auto contentsUnder(std::span<const Plane> planes, std::span<const HullNode> nodes,
                                 HullChild child, Vec3 point) noexcept -> Contents {
    while (const auto* index = std::get_if<HullNodeIndex>(&child)) {
        const HullNode& node = nodes[index->value];
        child = node.children[planes[node.plane.value].signedDistance(point) < 0.0F ? 1 : 0];
    }
    return std::get<Contents>(child);
}

/// Whether `root` is the last node of `nodes`, which player movement takes for an empty hull.
[[nodiscard]] auto isTheLastNode(HullChild root, std::span<const HullNode> nodes) noexcept -> bool {
    const auto* node = std::get_if<HullNodeIndex>(&root);
    return node != nullptr && node->value + 1 == nodes.size();
}

/// Follows a trace through one hull's tree.
template <Rules kRules>
class Tracer {
public:
    Tracer(std::span<const Plane> planes, std::span<const HullNode> nodes, HullChild root,
           Trace& trace) noexcept
        : m_planes{planes}, m_nodes{nodes}, m_root{root}, m_trace{&trace} {}

    /// Follows the way from `start` to `end`, at the fractions `startFraction` and `endFraction` of the
    /// whole trace, under `child`; false once it stopped. Recurses once a node of the tree (no deeper
    /// than kMaxHullDepth).
    auto follow(HullChild child, float startFraction, float endFraction, Vec3 start, Vec3 end) noexcept
        -> bool {
        if (const auto* contents = std::get_if<Contents>(&child)) {
            enter(*contents);
            return true;
        }
        const HullNode& node = m_nodes[std::get<HullNodeIndex>(child).value];
        const Plane& plane = m_planes[node.plane.value];
        const float startDistance = plane.signedDistance(start);
        const float endDistance = plane.signedDistance(end);
        if (startDistance >= 0.0F && endDistance >= 0.0F) {
            return follow(node.children[0], startFraction, endFraction, start, end);
        }
        if (startDistance < 0.0F && endDistance < 0.0F) {
            return follow(node.children[1], startFraction, endFraction, start, end);
        }
        // The crossing, kStopDistance before the plane on the side the way starts.
        const float stop =
            startDistance < 0.0F ? startDistance + kStopDistance : startDistance - kStopDistance;
        float local = stop / (startDistance - endDistance);
        if constexpr (kRules == Rules::Server) {
            local = local < 0.0F ? 0.0F : local > 1.0F ? 1.0F : local;
            if (std::isnan(local)) {
                return false; // the server's trace stops where it is
            }
        } else {
            local = local >= 0.0F ? std::min(local, 1.0F) : 0.0F; // movement's makes not-a-number 0
        }
        float crossingFraction = startFraction + ((endFraction - startFraction) * local);
        Vec3 crossing = pointAlong(start, end, local);
        const std::size_t side = startDistance < 0.0F ? 1 : 0;

        if (!follow(node.children[side], startFraction, crossingFraction, start, crossing)) {
            return false;
        }
        if (contentsUnder(m_planes, m_nodes, node.children[side ^ 1], crossing) != Contents::Solid) {
            return follow(node.children[side ^ 1], crossingFraction, endFraction, crossing, end);
        }
        if (m_trace->isAllSolid) {
            return false; // it never left solid
        }

        // The other side is solid: the trace stops here, at the plane facing the way it came.
        if constexpr (kRules == Rules::Server) {
            m_trace->plane = side == 0 ? plane : Plane{.normal = -plane.normal, .distance = -plane.distance};
        } else {
            // Parity: 0 - normal, so that a zero component stays +0, as movement's negation leaves it.
            m_trace->plane =
                side == 0 ? plane : Plane{.normal = Vec3{} - plane.normal, .distance = -plane.distance};
        }
        // A stop still in solid, which rounding makes, moves back along the way, to its start at most.
        while (contentsUnder(m_planes, m_nodes, m_root, crossing) == Contents::Solid) {
            if constexpr (kRules == Rules::Server) {
                local -= kServerBackup;
            } else {
                local = static_cast<float>(double{local} - kMovementBackup);
            }
            if (local < 0.0F) {
                break; // the last stop is kept
            }
            crossingFraction = startFraction + ((endFraction - startFraction) * local);
            crossing = pointAlong(start, end, local);
        }
        m_trace->fraction = crossingFraction;
        m_trace->end = crossing;
        return false;
    }

private:
    /// A trace through a leaf of `contents`.
    void enter(Contents contents) noexcept {
        if (contents == Contents::Solid) {
            m_trace->startsSolid = true;
            return;
        }
        m_trace->isAllSolid = false;
        if (contents == Contents::Empty) {
            m_trace->isInOpen = true;
        } else if (kRules == Rules::Movement || contents != Contents::Translucent) {
            m_trace->isInWater = true;
        }
    }

    /// The point `fraction` of the way from `start` to `end`: the server's sum is in double precision,
    /// rounded once, as the original's; movement's in single.
    [[nodiscard]] static auto pointAlong(Vec3 start, Vec3 end, float fraction) noexcept -> Vec3 {
        if constexpr (kRules == Rules::Server) {
            const auto along = [fraction](float from, float to) {
                return static_cast<float>((double{fraction} * double{to - from}) + double{from});
            };
            return Vec3{.x = along(start.x, end.x), .y = along(start.y, end.y), .z = along(start.z, end.z)};
        } else {
            return ((end - start) * fraction) + start;
        }
    }

    std::span<const Plane> m_planes;
    std::span<const HullNode> m_nodes;
    HullChild m_root;
    Trace* m_trace;
};

[[nodiscard]] auto childOf(assets::NodeChild child, std::span<const assets::BspLeaf> leaves) -> HullChild {
    if (const auto* node = std::get_if<assets::NodeIndex>(&child)) {
        return HullNodeIndex{node->value};
    }
    return leaves[std::get<assets::LeafIndex>(child).value].contents;
}

[[nodiscard]] auto childOf(assets::ClipNodeChild child) -> HullChild {
    if (const auto* node = std::get_if<assets::ClipNodeIndex>(&child)) {
        return HullNodeIndex{node->value};
    }
    return std::get<Contents>(child);
}

} // namespace

namespace {

// The boxes compilers grow the hulls by (Mod_LoadClipnodes): a standing player's, a large entity's and
// a ducked player's, their half sizes.
constexpr float kPlayerHalfWidth = 16.0F;
constexpr float kStandingHalfHeight = 36.0F;
constexpr float kLargeHalfSize = 32.0F;
constexpr float kDuckedHalfHeight = 18.0F;
/// SV_HullForBsp: the widest box a point traces, the widest a player's hull does, and the tallest the
/// ducked one does.
constexpr float kPointWidth = 8.0F;
constexpr float kPlayerWidth = 36.0F;
constexpr float kDuckedHeight = 36.0F;

} // namespace

auto hullBoxOf(Hull hull) noexcept -> Aabb {
    switch (hull) {
    case Hull::Standing:
        return Aabb::around({}, {.x = kPlayerHalfWidth, .y = kPlayerHalfWidth, .z = kStandingHalfHeight});
    case Hull::Large:
        return Aabb::around({}, {.x = kLargeHalfSize, .y = kLargeHalfSize, .z = kLargeHalfSize});
    case Hull::Ducked:
        return Aabb::around({}, {.x = kPlayerHalfWidth, .y = kPlayerHalfWidth, .z = kDuckedHalfHeight});
    case Hull::Point:
    case Hull::Count:
        break;
    }
    return Aabb{};
}

auto boxHullOf(const Aabb& box) noexcept -> BoxHull {
    const Vec3 size = box.max - box.min;
    if (size.x <= kPointWidth) {
        return BoxHull{.hull = Hull::Point, .offset = hullBoxOf(Hull::Point).min};
    }
    const Hull hull = size.x > kPlayerWidth     ? Hull::Large
                      : size.z <= kDuckedHeight ? Hull::Ducked
                                                : Hull::Standing;
    return BoxHull{.hull = hull, .offset = hullBoxOf(hull).min - box.min};
}

auto BrushHulls::create(BrushHullsDesc desc) -> Result<BrushHulls, CollisionError> {
    for (const auto& [tree, nodes] :
         {std::pair{std::string_view{"hull 0"}, std::span{std::as_const(desc.pointNodes)}},
          std::pair{std::string_view{"clip"}, std::span{std::as_const(desc.clipNodes)}}}) {
        if (auto checked = checkTree(nodes, desc.planes.size(), tree); !checked) {
            return std::unexpected{std::move(checked.error())};
        }
    }
    for (std::size_t model = 0; model < desc.models.size(); ++model) {
        for (const Hull hull : HullRoots::keys()) {
            const std::size_t nodes = hull == Hull::Point ? desc.pointNodes.size() : desc.clipNodes.size();
            if (const auto* root = std::get_if<HullNodeIndex>(&desc.models[model].hulls[hull]);
                root != nullptr && root->value >= nodes) {
                return fail(CollisionError::BadIndex, std::format("model {}, hull {}: root {} of {} nodes",
                                                                  model, enumName(hull), root->value, nodes));
            }
        }
    }
    BrushHulls hulls;
    hulls.m_planes = std::move(desc.planes);
    hulls.m_pointNodes = std::move(desc.pointNodes);
    hulls.m_clipNodes = std::move(desc.clipNodes);
    hulls.m_models = std::move(desc.models);
    return hulls;
}

auto BrushHulls::create(const assets::BspView& map) -> Result<BrushHulls, CollisionError> {
    BrushHullsDesc desc;
    desc.planes =
        map.planes() | std::views::transform(&assets::BspPlane::plane) | std::ranges::to<std::vector>();
    for (const assets::BspNode& node : map.nodes()) {
        desc.pointNodes.push_back(HullNode{
            .plane = node.plane,
            .children = {childOf(node.children[0], map.leaves()), childOf(node.children[1], map.leaves())}});
    }
    for (const assets::ClipNode& node : map.clipNodes()) {
        desc.clipNodes.push_back(HullNode{
            .plane = node.plane, .children = {childOf(node.children[0]), childOf(node.children[1])}});
    }
    for (const assets::BspModel& model : map.models()) {
        BrushModel& brush = desc.models.emplace_back(BrushModel{.bounds = model.bounds});
        brush.hulls[Hull::Point] = HullNodeIndex{model.headNode.value};
        for (std::size_t clip = 0; clip < assets::kClipHulls; ++clip) {
            brush.hulls[kClipNodeHulls[clip]] = childOf(model.hulls[clip]);
        }
    }
    return create(std::move(desc));
}

auto BrushHulls::boundsOf(ModelIndex model) const noexcept -> Aabb {
    expects(model.value < m_models.size(), "BrushHulls::boundsOf: a model");
    return m_models[model.value].bounds;
}

namespace {

/// `point` in the space of a model at `offset` turned by `angles`; turned only when it is.
[[nodiscard]] auto inModelSpace(Vec3 point, Vec3 offset, EulerAngles angles) noexcept -> Vec3 {
    const Vec3 moved = point - offset;
    return angles == EulerAngles{} ? moved : intoAxes(angleVectors(angles), moved);
}

} // namespace

auto BrushHulls::contentsAt(ModelIndex model, Hull hull, Vec3 point, Vec3 offset,
                            EulerAngles angles) const noexcept -> Contents {
    return contentsAt(model, hull, inModelSpace(point, offset, angles));
}

auto BrushHulls::movementContentsAt(ModelIndex model, Hull hull, Vec3 point, Vec3 offset,
                                    EulerAngles angles) const noexcept -> Contents {
    return movementContentsAt(model, hull, inModelSpace(point, offset, angles));
}

auto BrushHulls::contentsAt(ModelIndex model, Hull hull, Vec3 point) const noexcept -> Contents {
    expects(model.value < m_models.size() && hull != Hull::Count,
            "BrushHulls::contentsAt: a model and a hull");
    return contentsUnder(m_planes, hull == Hull::Point ? m_pointNodes : m_clipNodes,
                         m_models[model.value].hulls[hull], point);
}

auto BrushHulls::movementContentsAt(ModelIndex model, Hull hull, Vec3 point) const noexcept -> Contents {
    expects(model.value < m_models.size() && hull != Hull::Count,
            "BrushHulls::movementContentsAt: a model and a hull");
    const std::span<const HullNode> nodes = hull == Hull::Point ? m_pointNodes : m_clipNodes;
    const HullChild root = m_models[model.value].hulls[hull];
    if (isTheLastNode(root, nodes)) {
        return Contents::Empty; // Parity: movement takes such a hull for an empty one
    }
    return contentsUnder(m_planes, nodes, root, point);
}

auto BrushHulls::trace(const HullTraceQuery& query) const noexcept -> Trace {
    expects(query.model.value < m_models.size() && query.hull != Hull::Count,
            "BrushHulls::trace: a model and a hull");
    Trace result{.fraction = 1.0F, .end = query.end, .isAllSolid = true};
    const HullChild root = m_models[query.model.value].hulls[query.hull];
    Tracer<Rules::Server> tracer{m_planes, query.hull == Hull::Point ? m_pointNodes : m_clipNodes, root,
                                 result};
    // Where it stopped, if it did, is in the result already.
    std::ignore = tracer.follow(root, 0.0F, 1.0F, inModelSpace(query.start, query.offset, query.angles),
                                inModelSpace(query.end, query.offset, query.angles));
    if (result.fraction != 1.0F) {
        if (query.angles != EulerAngles{}) {
            result.plane.normal = outOfAxes(angleVectors(query.angles), result.plane.normal);
        }
        // The end, from the fraction, in double precision as the original's.
        const auto along = [&result](float from, float to) {
            return static_cast<float>((double{result.fraction} * double{to - from}) + double{from});
        };
        result.end = Vec3{.x = along(query.start.x, query.end.x),
                          .y = along(query.start.y, query.end.y),
                          .z = along(query.start.z, query.end.z)};
    }
    return result;
}

auto BrushHulls::traceMovement(const HullTraceQuery& query) const noexcept -> Trace {
    expects(query.model.value < m_models.size() && query.hull != Hull::Count,
            "BrushHulls::traceMovement: a model and a hull");
    Trace result{.fraction = 1.0F, .end = query.end, .isAllSolid = true};
    const std::span<const HullNode> nodes = query.hull == Hull::Point ? m_pointNodes : m_clipNodes;
    HullChild root = m_models[query.model.value].hulls[query.hull];
    if (isTheLastNode(root, nodes)) {
        root = Contents::Empty; // Parity: movement takes such a hull for an empty one
    }
    Tracer<Rules::Movement> tracer{m_planes, nodes, root, result};
    // Where it stopped, if it did, is in the result already.
    std::ignore = tracer.follow(root, 0.0F, 1.0F, inModelSpace(query.start, query.offset, query.angles),
                                inModelSpace(query.end, query.offset, query.angles));
    if (result.isAllSolid) {
        result.startsSolid = true;
    }
    if (result.startsSolid) {
        result.fraction = 0.0F;
    }
    if (result.fraction != 1.0F) {
        if (query.angles != EulerAngles{}) {
            result.plane.normal = outOfAxes(angleVectors(query.angles), result.plane.normal);
        }
        result.end = ((query.end - query.start) * result.fraction) + query.start;
    }
    return result;
}

} // namespace nx::collision
