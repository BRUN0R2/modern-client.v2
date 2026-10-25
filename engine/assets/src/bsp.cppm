// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// BSP v30, the maps of GoldSrc (docs/06-assets-and-resources.md, section 3.1): every lump, every
/// index checked against what it indexes, the visible tree checked to be a tree and the collision
/// hulls to be free of cycles, so readers index and walk without checks.
export module nx.assets:bsp;

import std;
import nx.core;
import nx.math;
import :wad;

export namespace nx::assets {

/// The BSP version of GoldSrc maps.
inline constexpr std::int32_t kBspVersion = 30;

/// The light styles a face blends; kNoLightStyle marks an unused one.
inline constexpr std::size_t kFaceLightStyles = 4;
inline constexpr std::uint8_t kNoLightStyle = 255;

/// The ambient sounds a leaf sets levels for: water, sky, slime and lava.
inline constexpr std::size_t kAmbientSounds = 4;

/// The collision hulls a model has besides its node tree: player standing, large (monsters),
/// player ducking.
inline constexpr std::size_t kClipHulls = 3;

/// An index into one array of a BspView, named by `Tag`; parse() checks every one it reads.
template <class Tag>
struct BspIndex {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator==(const BspIndex&) const noexcept -> bool = default;
};

struct PlaneTag;
struct VertexTag;
struct EdgeTag;
struct SurfaceEdgeTag;
struct TextureInfoTag;
struct TextureTag;
struct FaceTag;
struct MarkSurfaceTag;
struct NodeTag;
struct LeafTag;
struct ClipNodeTag;
struct ModelTag;

using PlaneIndex = BspIndex<PlaneTag>;
using VertexIndex = BspIndex<VertexTag>;
using EdgeIndex = BspIndex<EdgeTag>;
using SurfaceEdgeIndex = BspIndex<SurfaceEdgeTag>;
using TextureInfoIndex = BspIndex<TextureInfoTag>;
using TextureIndex = BspIndex<TextureTag>;
using FaceIndex = BspIndex<FaceTag>;
using MarkSurfaceIndex = BspIndex<MarkSurfaceTag>;
using NodeIndex = BspIndex<NodeTag>;
using LeafIndex = BspIndex<LeafTag>;
using ClipNodeIndex = BspIndex<ClipNodeTag>;
/// The world (model 0) or a brush entity's model ("*1", "*2"...), by its index in the models.
using ModelIndex = BspIndex<ModelTag>;

/// The world: model 0 of every map.
inline constexpr ModelIndex kWorldModel{};

/// What can be wrong with a BSP file; the detail names the lump, the record and the value.
enum class BspError : std::uint8_t {
    TooSmall,           ///< Smaller than the header.
    UnsupportedVersion, ///< Not version 30.
    LumpOutOfBounds,    ///< A lump does not fit in the file.
    MisalignedLump,     ///< A lump is not a whole number of its records.
    BadIndex,           ///< A record names a record that does not exist.
    BadValue,           ///< A value outside its domain: a float that is not finite, a normal that is
                        ///< not unit length, an unknown plane type or contents, a face of fewer than
                        ///< three edges, a node that is not after its parent or has two parents (the
                        ///< visible tree must neither cycle nor share a subtree), clip nodes in a cycle.
    BadTexture,         ///< An embedded texture breaks its format (cause: the WadError).
    BadVisibility,      ///< A leaf's potentially visible set runs past the visibility lump.
    BadEntities,        ///< The entity text breaks its syntax; the detail has the line.
};

/// What a point of a map is in, numbered as the original numbers it: what a leaf or a side of a clip
/// node holds, Empty to Translucent; and Ladder, which only an entity holds (a func_ladder's "skin").
enum class Contents : std::int8_t {
    Empty = -1,
    Solid = -2,
    Water = -3,
    Slime = -4,
    Lava = -5,
    Sky = -6,
    Origin = -7,
    Clip = -8,
    Current0 = -9,
    Current90 = -10,
    Current180 = -11,
    Current270 = -12,
    CurrentUp = -13,
    CurrentDown = -14,
    Translucent = -15,
    Ladder = -16,
};

/// The contents the original numbers `value`, as an entity's "skin" holds them; nullopt for a number
/// that names none.
[[nodiscard]] auto contentsNumbered(std::int32_t value) noexcept -> std::optional<Contents>;

/// The axis a plane faces: one of the three exactly, or the nearest of them.
enum class PlaneAxis : std::uint8_t {
    X,
    Y,
    Z,
    NearestX,
    NearestY,
    NearestZ,
    Count,
};

/// A plane of the map (the planes lump): faces and nodes lie on it.
struct BspPlane {
    Plane plane{};
    PlaneAxis axis = PlaneAxis::X;
};

/// An edge between two vertices.
struct BspEdge {
    std::array<VertexIndex, 2> vertices{};
};

/// An edge of a face's outline, in the order the face winds: the edge, from its second vertex to
/// its first when reversed.
struct SurfaceEdge {
    EdgeIndex edge{};
    bool isReversed = false;
};

/// How a texture lies on the faces that use it: texture coordinates are s = dot(point, s.xyz) +
/// s.w and t alike.
struct TextureInfo {
    Vec4 s{};
    Vec4 t{};
    TextureIndex texture{};
    bool isSpecial = false; ///< Sky, water and other animated surfaces: no lightmap, no subdivision.
};

/// A face of the map (the faces lump): its plane and side, its edges, its texture's placement and
/// its lightmaps.
struct BspFace {
    PlaneIndex plane{};
    bool isBackSide = false; ///< The face looks against the plane's normal.
    SurfaceEdgeIndex firstEdge{};
    std::uint32_t edgeCount = 0; ///< At least three, from firstEdge on.
    TextureInfoIndex textureInfo{};
    std::array<std::uint8_t, kFaceLightStyles> lightStyles{};
    std::optional<std::uint32_t> lightmapOffset{}; ///< In BspView::lighting(); none when unlit.
};

/// A side of a node: a node after it, or a leaf.
using NodeChild = std::variant<NodeIndex, LeafIndex>;

/// A side of a clip node: a clip node, or what the space there holds. Compilers write a subtree
/// that several nodes have once (VHLT shares them), so a clip node may have several parents and
/// come before them; the hulls have no cycle.
using ClipNodeChild = std::variant<ClipNodeIndex, Contents>;

/// A node of the visible tree: its plane splits its bounds; children[0] is in front.
struct BspNode {
    PlaneIndex plane{};
    std::array<NodeChild, 2> children{};
    Aabb bounds{};
    FaceIndex firstFace{};
    std::uint32_t faceCount = 0;
};

/// A leaf of the visible tree (the leaves lump): what it holds, what it sees, its bounds, its faces
/// and the levels of its ambient sounds.
struct BspLeaf {
    Contents contents = Contents::Empty;
    std::optional<std::uint32_t> visibilityOffset{}; ///< In BspView::visibility(); none sees everything.
    Aabb bounds{};
    MarkSurfaceIndex firstMarkSurface{};
    std::uint32_t markSurfaceCount = 0;
    std::array<std::uint8_t, kAmbientSounds> ambientLevels{};
};

/// A node of a collision hull: its plane splits space; children[0] is in front.
struct ClipNode {
    PlaneIndex plane{};
    std::array<ClipNodeChild, 2> children{};
};

/// The world (model 0) or a brush entity's model ("*1", "*2"...).
struct BspModel {
    Aabb bounds{};
    Vec3 origin{};
    NodeIndex headNode{};                          ///< The root of its visible tree.
    std::array<ClipNodeChild, kClipHulls> hulls{}; ///< The roots of its collision hulls.
    std::uint32_t visibleLeafCount = 0;            ///< Leaves 1 to visibleLeafCount have visibility.
    FaceIndex firstFace{};
    std::uint32_t faceCount = 0;
};

/// Leaves of the world: the ones a leaf may see, its potentially visible set.
class LeafSet {
public:
    LeafSet() = default;
    /// The leaves of `bits`, bit n of byte n / 8 being leaf n + 1, as the compiler packs them, of
    /// the world's `leafCount` leaves after leaf 0.
    inline LeafSet(std::vector<std::uint8_t> bits, std::uint32_t leafCount) noexcept
        : m_bits{std::move(bits)}, m_leafCount{leafCount} {}

    /// Never the solid leaf 0, nor a leaf of a brush entity's model.
    [[nodiscard]] auto contains(LeafIndex leaf) const noexcept -> bool;

private:
    std::vector<std::uint8_t> m_bits;
    std::uint32_t m_leafCount = 0;
};

/// The integer at the start of an entity's value, after spaces, as the original's atoi reads it;
/// nullopt when the value does not start with one.
[[nodiscard]] auto integerOfValue(std::string_view value) noexcept -> std::optional<int>;

/// The number at the start of an entity's value, after spaces, as the original's atof reads it;
/// nullopt when the value does not start with one.
[[nodiscard]] auto numberOfValue(std::string_view value) noexcept -> std::optional<float>;

/// The three numbers of an entity's vector value ("-512 1024 36"), each after spaces; nullopt for
/// other text.
[[nodiscard]] auto vectorOfValue(std::string_view value) noexcept -> std::optional<Vec3>;

/// An entity of the map: its keys and values, in file order.
struct BspEntity {
    std::vector<std::pair<std::string, std::string>> keyValues{};

    /// The value of `key`; the last when it repeats, as the original assigns them in order.
    [[nodiscard]] auto find(std::string_view key) const noexcept -> std::optional<std::string_view>;
    /// The value of `key` read by integerOfValue; nullopt when there is none too.
    [[nodiscard]] auto findInteger(std::string_view key) const noexcept -> std::optional<int>;
    /// The value of `key` read by numberOfValue; nullopt when there is none too.
    [[nodiscard]] auto findNumber(std::string_view key) const noexcept -> std::optional<float>;
    /// The value of `key` read by vectorOfValue; nullopt when there is none too.
    [[nodiscard]] auto findVector(std::string_view key) const noexcept -> std::optional<Vec3>;
    /// The brush model the entity is ("model" "*3"); nullopt when it has no model or another kind (a
    /// studio model or a sprite). The map may lack it: parse() does not read the entities' values.
    [[nodiscard]] auto findBrushModel() const noexcept -> std::optional<ModelIndex>;
};

/// A BSP v30 file: decoded records, and views of the bytes it was parsed from (textures, lighting,
/// visibility), which must outlive it.
class BspView {
public:
    /// Errors: every BspError.
    [[nodiscard]] static auto parse(std::span<const std::byte> bytes) -> Result<BspView, BspError>;

    [[nodiscard]] inline auto planes() const noexcept -> std::span<const BspPlane> { return m_planes; }
    [[nodiscard]] inline auto vertices() const noexcept -> std::span<const Vec3> { return m_vertices; }
    [[nodiscard]] inline auto edges() const noexcept -> std::span<const BspEdge> { return m_edges; }
    [[nodiscard]] inline auto surfaceEdges() const noexcept -> std::span<const SurfaceEdge> {
        return m_surfaceEdges;
    }
    [[nodiscard]] inline auto textureInfos() const noexcept -> std::span<const TextureInfo> {
        return m_textureInfos;
    }
    /// The textures by index; nullopt where the compiler left a slot empty.
    [[nodiscard]] inline auto textures() const noexcept -> std::span<const std::optional<MipTexture>> {
        return m_textures;
    }
    [[nodiscard]] inline auto faces() const noexcept -> std::span<const BspFace> { return m_faces; }
    /// RGB lightmaps, three bytes per sample.
    [[nodiscard]] inline auto lighting() const noexcept -> std::span<const std::byte> { return m_lighting; }
    /// The faces of each leaf, by FaceIndex.
    [[nodiscard]] inline auto markSurfaces() const noexcept -> std::span<const FaceIndex> {
        return m_markSurfaces;
    }
    /// The potentially visible sets of the leaves, run-length compressed as the compiler wrote them.
    [[nodiscard]] inline auto visibility() const noexcept -> std::span<const std::byte> {
        return m_visibility;
    }
    [[nodiscard]] inline auto nodes() const noexcept -> std::span<const BspNode> { return m_nodes; }
    /// Leaf 0 is the solid leaf every outside points to.
    [[nodiscard]] inline auto leaves() const noexcept -> std::span<const BspLeaf> { return m_leaves; }
    [[nodiscard]] inline auto clipNodes() const noexcept -> std::span<const ClipNode> { return m_clipNodes; }
    /// The world first, then the brush models.
    [[nodiscard]] inline auto models() const noexcept -> std::span<const BspModel> { return m_models; }
    /// The entities, the world's first.
    [[nodiscard]] inline auto entities() const noexcept -> std::span<const BspEntity> { return m_entities; }

    /// The leaf of the world that holds `point`: a point on a node's plane is behind it, as the
    /// original's Mod_PointInLeaf puts it. The solid leaf 0 holds what is inside a wall or outside the
    /// world.
    [[nodiscard]] auto leafAt(Vec3 point) const noexcept -> LeafIndex;

    /// The leaves of the world `leaf` may see. Every one from the solid leaf 0 and from a leaf the
    /// compiler gave no set, as the original sees from inside a wall or without visibility.
    [[nodiscard]] auto potentiallyVisible(LeafIndex leaf) const -> LeafSet;

private:
    BspView() = default;

    std::vector<BspPlane> m_planes;
    std::vector<Vec3> m_vertices;
    std::vector<BspEdge> m_edges;
    std::vector<SurfaceEdge> m_surfaceEdges;
    std::vector<TextureInfo> m_textureInfos;
    std::vector<std::optional<MipTexture>> m_textures;
    std::vector<BspFace> m_faces;
    std::span<const std::byte> m_lighting;
    std::vector<FaceIndex> m_markSurfaces;
    std::span<const std::byte> m_visibility;
    std::vector<BspNode> m_nodes;
    std::vector<BspLeaf> m_leaves;
    std::vector<ClipNode> m_clipNodes;
    std::vector<BspModel> m_models;
    std::vector<BspEntity> m_entities;
};

} // namespace nx::assets
