// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the faces of a BSP map lie (docs/06-assets-and-resources.md), as its compilers measured them
/// and the engine walks them: a face's corners and texture coordinates, where its texels lie on it
/// (CalcFaceExtents), and the first face a segment meets in a model's tree (R_RecursiveLightPoint and
/// SurfaceAtPoint, ReHLDS's, GPL, consulted: docs/20 section 3), which the light of the world and the
/// texture a player stands on are found by.
export module nx.assets:bspSurfaces;

import std;
import nx.core;
import nx.math;
import :bsp;

export namespace nx::assets {

/// Map units per lightmap sample: GoldSrc's luxel.
inline constexpr double kLuxelUnits = 16.0;

/// Where a face's luxels lie on it: in texels of its texture, the first luxel's coordinates (the next
/// ones 16 texels apart), and how many luxels it has across and down.
struct SurfaceExtent {
    double minS = 0.0;
    double minT = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Corner `edge` of `face` of `map`, in the order the compiler wound its edges.
[[nodiscard]] auto cornerOf(const BspView& map, const BspFace& face, std::uint32_t edge) -> Vec3;

/// The texture coordinate of `point` along `axis` (a texture's s or t), in double precision as the
/// compilers compute it.
[[nodiscard]] auto textureCoordinateOf(Vec3 point, Vec4 axis) noexcept -> double;

/// The texture coordinate of `point` along `axis`, as the engine computes it, in single precision.
[[nodiscard]] auto engineTextureCoordinateOf(Vec3 point, Vec4 axis) noexcept -> float;

/// The extent of `face` of `map`, as the compilers measure it from its corners (CalcFaceExtents);
/// none when it is more luxels than a count holds.
[[nodiscard]] auto surfaceExtentOf(const BspView& map, const BspFace& face) -> std::optional<SurfaceExtent>;

/// The extent of each face of `map` (surfaceExtentOf), by its index.
[[nodiscard]] auto surfaceExtentsOf(const BspView& map) -> std::vector<std::optional<SurfaceExtent>>;

/// Whether `point` is on the face of `extent` whose texture's axes are `s` and `t`, as the engine
/// tells it (SurfaceAtPoint): its coordinates, in single precision and cut to whole texels, from the
/// extent's first luxel to its last.
[[nodiscard]] auto isWithinExtent(Vec3 point, Vec4 s, Vec4 t, const SurfaceExtent& extent) noexcept -> bool;

/// Walks the segment from `start` to `end` down the tree of `map` from `root`, the nearer side of each
/// node first, as the original's R_RecursiveLightPoint and SurfaceAtPoint walk it, and gives `meets`
/// each face of every node it crosses, with the point where it crosses the node's plane, until
/// `meets` finds something: what the walk returns. The way ahead is kept on a stack of its own, not in
/// recursion, however deep a file's tree.
template <class Found, class Meets>
[[nodiscard]] auto firstAlong(const BspView& map, NodeChild root, Vec3 start, Vec3 end, const Meets& meets)
    -> std::optional<Found> {
    /// A side of a node to walk from `start` to `end`.
    struct Side {
        NodeChild child;
        Vec3 start;
        Vec3 end;
    };
    /// A node's faces, met where the segment crosses its plane.
    struct Crossing {
        NodeIndex node;
        Vec3 point;
    };
    std::vector<std::variant<Side, Crossing>> ahead{Side{.child = root, .start = start, .end = end}};
    while (!ahead.empty()) {
        const std::variant<Side, Crossing> step = ahead.back();
        ahead.pop_back();
        if (const auto* crossing = std::get_if<Crossing>(&step)) {
            const BspNode& node = map.nodes()[crossing->node.value];
            for (std::uint32_t offset = 0; offset < node.faceCount; ++offset) {
                if (std::optional<Found> found =
                        meets(FaceIndex{node.firstFace.value + offset}, crossing->point)) {
                    return found;
                }
            }
            continue;
        }
        const Side& side = std::get<Side>(step);
        const auto* index = std::get_if<NodeIndex>(&side.child);
        if (index == nullptr) {
            continue; // a leaf: nothing is met in it
        }
        const BspNode& node = map.nodes()[index->value];
        const Plane& plane = map.planes()[node.plane.value].plane;
        const float front = plane.signedDistance(side.start);
        const float back = plane.signedDistance(side.end);
        const std::size_t nearSide = front < 0.0F ? 1 : 0;
        if ((back < 0.0F) == (nearSide == 1)) {
            ahead.push_back(Side{.child = node.children[nearSide], .start = side.start, .end = side.end});
            continue;
        }
        const Vec3 point = lerp(side.start, side.end, front / (front - back));
        // Taken in reverse: the near side, then the node's faces, then the far side.
        ahead.push_back(Side{.child = node.children[nearSide ^ 1], .start = point, .end = side.end});
        ahead.push_back(Crossing{.node = *index, .point = point});
        ahead.push_back(Side{.child = node.children[nearSide], .start = side.start, .end = point});
    }
    return std::nullopt;
}

/// The first face of `model` of `map` the segment from `start` to `end`, in the model's own space,
/// meets at a point on it (SurfaceAtPoint), each face's extent among `extents` (surfaceExtentsOf);
/// none when it meets none, or for a model the map lacks.
[[nodiscard]] auto surfaceAt(const BspView& map, std::span<const std::optional<SurfaceExtent>> extents,
                             ModelIndex model, Vec3 start, Vec3 end) -> std::optional<FaceIndex>;

} // namespace nx::assets
