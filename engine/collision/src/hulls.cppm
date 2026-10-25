// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The collision hulls of a map's brush models (docs/07-simulation-and-physics.md, section 4.1):
/// for the world and each brush entity, a tree of planes for each hull, as the compiler built it;
/// traces through them stop where the original's stop. The original traces by two sets of rules:
/// the server's, for shots and entities (SV_RecursiveHullCheck), and player movement's, on the
/// server and the client alike (PM_RecursiveHullCheck); both are here, as the GPL engines that
/// reconstruct the original describe them, written anew (docs/20-legal-and-licensing.md, section 3).
export module nx.collision:hulls;

import std;
import nx.core;
import nx.math;
import nx.assets;

export namespace nx::collision {

/// The hulls of a brush model: a point, and the boxes of a standing player, of large entities
/// and of a ducked player, each a tree the compiler grew by that box (docs/07, section 3.3).
enum class Hull : std::uint8_t {
    Point,
    Standing,
    Large,
    Ducked,
    Count,
};

/// What a point of a hull is in: the contents of the map's leaves, as the original numbers them.
using Contents = assets::Contents;

/// A brush model of the map, and the world, model 0, which every map has.
using ModelIndex = assets::ModelIndex;
using assets::kWorldModel;

/// A plane of the map.
using PlaneIndex = assets::PlaneIndex;

/// A node of a hull's tree, by its index.
struct HullNodeIndex {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator==(const HullNodeIndex&) const noexcept -> bool = default;
};

/// A side of a hull node: a node after it, or what the space there holds.
using HullChild = std::variant<HullNodeIndex, Contents>;

/// A node: its plane splits space; children[0] is in front of it, the plane itself included.
struct HullNode {
    PlaneIndex plane{};
    std::array<HullChild, 2> children{};
};

/// The root of each hull of a brush model.
using HullRoots = EnumArray<Hull, HullChild>;

/// A brush model: the root of each of its hulls, and the box of its faces, in its own space.
struct BrushModel {
    HullRoots hulls{};
    Aabb bounds{};
};

/// The trees of a map's hulls. Hull 0 grows from the visible tree's nodes, a leaf as its
/// contents; hulls 1 to 3 from the clip nodes, which they may share.
struct BrushHullsDesc {
    std::vector<Plane> planes{};
    std::vector<HullNode> pointNodes{}; ///< Hull 0's.
    std::vector<HullNode> clipNodes{};  ///< Hulls 1 to 3's.
    std::vector<BrushModel> models{};   ///< By their ModelIndex: the world first.
};

/// No tree is deeper: the compilers' deepest (hldemo3 of Half-Life) is 146 nodes deep, and the
/// trace recurses once a node.
inline constexpr std::size_t kMaxHullDepth = 1024;

/// What can be wrong with hull trees.
enum class CollisionError : std::uint8_t {
    BadIndex, ///< A node names a plane or a node that does not exist.
    Cycle,    ///< A node is under itself.
    TooDeep,  ///< A tree deeper than kMaxHullDepth.
};

/// The box the compilers grow `hull` by, about a point (the clip_mins and clip_maxs the original's
/// Mod_LoadClipnodes gives each hull): none for Point.
[[nodiscard]] auto hullBoxOf(Hull hull) noexcept -> Aabb;

/// How the engine traces a box through brush models: the hull it goes through, and how far the hull's
/// middle is from the box's origin.
struct BoxHull {
    Hull hull = Hull::Point;
    /// The trace of a box at `origin` goes through the hull from origin - offset; where it stops, plus
    /// offset, is where the box's origin is.
    Vec3 offset{};
};

/// The hull of a box `box`, about its origin, as the original's SV_HullForBsp chooses it: a point for
/// one 8 units wide or less; for one 36 wide or less, the ducked hull when it is 36 tall or less, else
/// the standing one; else the large one. Its offset, the hull's box's low corner from the box's (a
/// point's none).
[[nodiscard]] auto boxHullOf(const Aabb& box) noexcept -> BoxHull;

/// Where a trace goes: from `start` to `end` through `hull` of brush model `model`, placed at
/// `offset` (its entity's origin; the world's is 0) and turned by `angles` about it (its entity's).
struct HullTraceQuery {
    ModelIndex model = kWorldModel;
    Hull hull = Hull::Point;
    Vec3 offset{};
    EulerAngles angles{};
    Vec3 start{};
    Vec3 end{};
};

/// What a trace met.
struct Trace {
    float fraction = 1.0F; ///< Of the way it went before it stopped; 1 when nothing stopped it.
    Vec3 end{};            ///< Where it stopped.
    /// Of what stopped it, facing the way it came, its normal in the world's space; none at a fraction
    /// of 1.
    Plane plane{};
    bool isAllSolid = false;  ///< It never left solid.
    bool startsSolid = false; ///< It started in solid.
    bool isInOpen = false;    ///< It went through empty space.
    bool isInWater = false;   ///< It went through water, slime, lava or the other liquid contents.
};

/// The hulls of a map's brush models, which traces read from any thread.
class BrushHulls {
public:
    /// Errors: every CollisionError.
    [[nodiscard]] static auto create(BrushHullsDesc desc) -> Result<BrushHulls, CollisionError>;

    /// The hulls of `map`, as its compiler built them. Errors: TooDeep.
    [[nodiscard]] static auto create(const assets::BspView& map) -> Result<BrushHulls, CollisionError>;

    [[nodiscard]] inline auto modelCount() const noexcept -> std::size_t { return m_models.size(); }
    /// The box of the faces of `model`, in its own space.
    [[nodiscard]] auto boundsOf(ModelIndex model) const noexcept -> Aabb;

    /// What `point`, in the model's space, is in, in `hull` of `model` (SV_HullPointContents).
    [[nodiscard]] auto contentsAt(ModelIndex model, Hull hull, Vec3 point) const noexcept -> Contents;
    /// What `point` is in, in `hull` of `model` placed at `offset` and turned by `angles` about it.
    [[nodiscard]] auto contentsAt(ModelIndex model, Hull hull, Vec3 point, Vec3 offset,
                                  EulerAngles angles) const noexcept -> Contents;

    /// The same, as player movement reads it (PM_HullPointContents): a hull whose root is the last
    /// node of its tree, or none, is empty, a quirk of the original kept for parity.
    [[nodiscard]] auto movementContentsAt(ModelIndex model, Hull hull, Vec3 point) const noexcept -> Contents;
    /// The same of `model` placed at `offset` and turned by `angles` about it (PM_TestPlayerPosition).
    [[nodiscard]] auto movementContentsAt(ModelIndex model, Hull hull, Vec3 point, Vec3 offset,
                                          EulerAngles angles) const noexcept -> Contents;

    /// The way of `query`, by the server's rules: stopped 1/32 unit before the plane it would cross
    /// into solid, backed up a tenth of the way at a time while that is still solid; translucent
    /// contents are neither open nor water; the end recomputed from the fraction, its sum in double
    /// precision as the original's.
    [[nodiscard]] auto trace(const HullTraceQuery& query) const noexcept -> Trace;

    /// The way of `query`, by player movement's rules: as the server's, but backed up a twentieth of
    /// the way at a time, every content not empty nor solid counted as water, the movement quirk of
    /// movementContentsAt, a trace that starts in solid stopped there (fraction 0) and all-solid
    /// counted as starting solid.
    [[nodiscard]] auto traceMovement(const HullTraceQuery& query) const noexcept -> Trace;

private:
    BrushHulls() = default;

    std::vector<Plane> m_planes;
    std::vector<HullNode> m_pointNodes;
    std::vector<HullNode> m_clipNodes;
    std::vector<BrushModel> m_models;
};

/// `box` grown by the box of `hull` (hullBoxOf), as the engine grows an entity's box by what is traced
/// against it (SV_HullForEntity, PM_HullForBox): where the hull's center meets it.
[[nodiscard]] auto grownByHull(const Aabb& box, Hull hull) noexcept -> Aabb;

/// The way from `start` to `end` through `box`, solid within it and empty around it, made a hull as the
/// engine makes one of a box (SV_InitBoxHull, SV_HullForBox): a node on each of its six faces, crossed
/// as a map's hull's nodes are, by the server's rules (BrushHulls::trace).
[[nodiscard]] auto traceBox(const Aabb& box, Vec3 start, Vec3 end) noexcept -> Trace;

/// The same by player movement's rules (BrushHulls::traceMovement, PM_HullForBox).
[[nodiscard]] auto traceBoxMovement(const Aabb& box, Vec3 start, Vec3 end) noexcept -> Trace;

/// What `point` is in within `box` made the same hull: the solid from its least along each axis to
/// short of its most, the empty elsewhere (PM_HullPointContents of a box's hull).
[[nodiscard]] auto boxContentsAt(const Aabb& box, Vec3 point) noexcept -> Contents;

/// A box turned and placed as a bone places its hitbox: `bounds` along the axes of `placement`, the
/// columns of its rotation, about its translation.
struct PlacedBox {
    Mat3x4 placement{};
    Aabb bounds{};
};

/// What a trace through hitboxes met: the trace, and the hitbox that gave it, by its index.
struct HitboxTrace {
    Trace trace{};
    std::size_t hitbox = 0;
};

/// The server's way from `start` to `end` through `hitboxes`, one at least, each made a hull of a
/// node on each face as the engine makes a studio model's (R_StudioHull), crossed by the server's
/// rules (BrushHulls::trace): the first hitbox's trace, then each later one's that keepFirstStop
/// keeps (SV_SingleClipMoveToEntity).
[[nodiscard]] auto traceHitboxes(std::span<const PlacedBox> hitboxes, Vec3 start, Vec3 end) noexcept
    -> HitboxTrace;

/// Keeps in `kept` the trace of what stops a move first, as the engine keeps it among what it clips
/// the move against (SV_ClipToLinks): `trace` takes its place when it is all in the solid, starts in
/// it or stops sooner, and then starts in the solid when either did. Returns whether it took it.
auto keepFirstStop(Trace& kept, const Trace& trace) noexcept -> bool;

} // namespace nx::collision
