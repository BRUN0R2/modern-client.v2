// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Decals on the world (docs/05-rendering.md, section 8.3): images of decals.wad stamped on the faces a
/// point is near, as the original's engine stamps a bullet hole (Xash3D's R_DecalShoot and its
/// clipping, GPL, consulted: docs/20 section 3). A decal is cut to each face it touches, in the
/// face's own texture directions, a texel a unit; the faces keep their decals, the newest drawn over
/// the older, in a pool that reuses its oldest when full.
///
/// Today: the world's decals; those of brush entities, permanent ones (infodecal) and the decals of
/// masked (`{`) faces come with what stamps them (TODO.md).
export module nx.render:decals;

import std;
import nx.core;
import nx.math;
import nx.assets;
import :gamma;

export namespace nx::render {

/// The image a decal of decals.wad shows, as the original makes it of its mip texture: when its
/// palette's last color is not pure blue (every decal of the game's), each texel that color, as
/// opaque as its index (index 255 is clear); otherwise each texel its palette's color, opaque, index
/// 255 clear. The palette goes through `textureGamma` first, as the original's does.
[[nodiscard]] auto decalImageOf(const assets::MipTexture& texture, const ChannelTable& textureGamma)
    -> assets::RgbaImage;

/// The size of a decal's image, in texels: at scale 1, its size on a face in units.
struct DecalSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Where a face's luxels lie on it, in texels of its texture: the first one's coordinates (its
/// texturemins) and how far the last one is from it (its extents).
struct DecalFaceExtent {
    double minS = 0.0;
    double minT = 0.0;
    double spanS = 0.0;
    double spanT = 0.0;
};

/// What decals need of a face of the map.
struct DecalFace {
    /// Whether decals stick to it: neither the sky, nor water, nor a conveyor, nor masked, and drawn.
    bool takesDecals = false;
    /// None for a face too large to have its luxels counted: every decal near it is cut to it.
    std::optional<DecalFaceExtent> extent{};
    /// Where its lightmaps are in the lightmap atlas, x then y, in luxels; none: it is drawn unlit.
    std::optional<std::array<std::uint32_t, 2>> lightmapCorner{};
    std::array<std::uint8_t, assets::kFaceLightStyles> styles{assets::kNoLightStyle, assets::kNoLightStyle,
                                                              assets::kNoLightStyle, assets::kNoLightStyle};
};

/// A corner of a decal on a face: where it is, where on the decal's image (from 0 to 1 across it),
/// and how the face is lit there (the world's vertex: in luxels of the atlas, with the face's styles).
struct DecalVertex {
    Vec3 position{};
    float u = 0.0F;
    float v = 0.0F;
    float lightmapX = -1.0F; ///< Negative on a face drawn unlit.
    float lightmapY = -1.0F;
    std::array<std::uint8_t, assets::kFaceLightStyles> styles{assets::kNoLightStyle, assets::kNoLightStyle,
                                                              assets::kNoLightStyle, assets::kNoLightStyle};
};

/// A decal on one face: its image, the face, and the part of it the decal covers, a convex fan.
struct PlacedDecal {
    std::uint32_t texture = 0;
    std::uint32_t face = 0;
    std::vector<DecalVertex> polygon{};
};

/// The decals of a map's world: a pool of decals on faces, each a slot, reused in turn when full.
class DecalPool {
public:
    /// The most decals a pool holds (the original's MAX_RENDER_DECALS).
    static constexpr std::size_t kMostDecals = 4096;

    /// A pool for the world of `map`, which must outlive it, `faces` its faces' by their index, of at
    /// most `limit` decals on faces (kMostDecals at most; 0 holds none).
    DecalPool(const assets::BspView& map, std::vector<DecalFace> faces, std::size_t limit);

    /// Stamps image `texture`, of `size`, on the faces of the world near `center`, `scale` times
    /// smaller (from 0.01 to 16), as the original's R_DecalShoot: the faces on the planes of the nodes
    /// it reaches within half the image's larger side that pass within 4 units of it, each cut to the
    /// decal's square in the face's texture directions. On a face already under 6 decals each over
    /// 60% covered by it, it replaces the one it covers least; otherwise it takes the pool's next slot,
    /// whose decal goes. Returns the faces it was stamped on.
    auto shoot(std::uint32_t texture, DecalSize size, Vec3 center, float scale = 1.0F) -> std::uint32_t;

    /// Every decal, oldest first: the order they are drawn in, the newest over the older.
    [[nodiscard]] auto inOrder() const -> std::vector<const PlacedDecal*>;

    /// How many decals face `face` has.
    [[nodiscard]] auto decalsOn(std::uint32_t face) const noexcept -> std::size_t;

    /// Changes each time a decal is stamped or goes: what draws them is redrawn then.
    [[nodiscard]] inline auto revision() const noexcept -> std::uint64_t { return m_revision; }

private:
    /// A slot of the pool: its decal, none while empty, and what it was stamped with.
    struct Slot {
        std::optional<PlacedDecal> decal{};
        Vec3 center{};
        float scale = 1.0F;
        DecalSize size{};
        std::uint64_t sequence = 0; ///< Its stamping's, in the order of all of them.
    };
    /// What a shot stamps.
    struct Shot {
        std::uint32_t texture = 0;
        DecalSize size{};
        Vec3 center{};
        float scale = 1.0F;
        float reach = 0.0F; ///< Half the image's larger side, in texels: how far the walk looks.
    };

    void walk(assets::NodeIndex index, const Shot& shot, std::uint32_t& stamped);
    /// Stamps `shot` on face `face` when it touches it; returns whether it did.
    auto stamp(std::uint32_t face, const Shot& shot) -> bool;
    /// The slot a decal on `face` takes: the one it covers least of 6 or more it covers mostly, or
    /// the pool's next.
    [[nodiscard]] auto slotFor(std::uint32_t face, const Shot& shot, Vec3 sAxis, Vec3 tAxis) -> std::size_t;
    /// Empties slot `slot`, its decal leaving its face.
    void empty(std::size_t slot);

    const assets::BspView* m_map;
    std::vector<DecalFace> m_faces;
    std::vector<Slot> m_slots;
    std::vector<std::vector<std::size_t>> m_decalsOfFace; ///< Slots, in the order stamped on it.
    std::size_t m_next = 0;                               ///< The slot the pool takes next.
    std::uint64_t m_sequence = 0;
    std::uint64_t m_revision = 0;
};

} // namespace nx::render

namespace nx::render {

/// The levels of `image` for a texture, each half the last, down to a texel, each texel the average
/// of the four it covers: the original's decals are mipmapped from their first level alone.
struct DecalLevels {
    std::vector<std::byte> bytes;
    std::vector<std::size_t> offsets;
};

[[nodiscard]] auto decalLevelsOf(const assets::RgbaImage& image) -> DecalLevels;

/// Whether the faces of a texture named `name` take decals: neither the sky, nor water, nor a
/// conveyor (`scroll`), nor masked (`{`), nor invisible.
[[nodiscard]] auto takesDecals(std::string_view name) noexcept -> bool;

} // namespace nx::render
