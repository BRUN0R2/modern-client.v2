// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// BSP v30, the maps of GoldSrc (docs/06-assets-and-resources.md, section 3.1). Today: the geometry
/// a renderer draws, every index checked against what it indexes, so its readers index without
/// checks. The tree (visibility, nodes, leaves, clip nodes), the models and the entities come next.
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

using PlaneIndex = BspIndex<PlaneTag>;
using VertexIndex = BspIndex<VertexTag>;
using EdgeIndex = BspIndex<EdgeTag>;
using SurfaceEdgeIndex = BspIndex<SurfaceEdgeTag>;
using TextureInfoIndex = BspIndex<TextureInfoTag>;
using TextureIndex = BspIndex<TextureTag>;

/// What can be wrong with a BSP file; the detail names the lump, the record and the value.
enum class BspError : std::uint8_t {
    TooSmall,           ///< Smaller than the header.
    UnsupportedVersion, ///< Not version 30.
    LumpOutOfBounds,    ///< A lump does not fit in the file.
    MisalignedLump,     ///< A lump is not a whole number of its records.
    BadIndex,           ///< A record names a record that does not exist.
    BadValue,           ///< A value outside its domain: a float that is not finite, a normal that is
                        ///< not unit length, an unknown plane type, a face of fewer than three edges.
    BadTexture,         ///< An embedded texture breaks its format (cause: the WadError).
};

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

struct BspFace {
    PlaneIndex plane{};
    bool isBackSide = false; ///< The face looks against the plane's normal.
    SurfaceEdgeIndex firstEdge{};
    std::uint32_t edgeCount = 0; ///< At least three, from firstEdge on.
    TextureInfoIndex textureInfo{};
    std::array<std::uint8_t, kFaceLightStyles> lightStyles{};
    std::optional<std::uint32_t> lightmapOffset{}; ///< In BspView::lighting(); none when unlit.
};

/// A BSP v30 file: decoded records, and views of the bytes it was parsed from (textures, lighting),
/// which must outlive it.
class BspView {
public:
    /// Errors: TooSmall, UnsupportedVersion, LumpOutOfBounds, MisalignedLump, BadIndex, BadValue,
    /// BadTexture.
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
};

} // namespace nx::assets
