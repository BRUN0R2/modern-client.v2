// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Decals on the faces of a map's models (docs/05-rendering.md, section 8): images of decals.wad
/// stamped on the faces a point is near, the world's or a brush entity's in its model's space, as the
/// original's engine stamps a bullet hole (Xash3D's R_DecalShoot and its clipping, GPL, consulted:
/// docs/20 section 3). A decal is cut to each face it touches, in the face's own texture directions, a
/// texel a unit; the faces keep their decals, the newest drawn over the older, in one pool for every
/// model that reuses its oldest when full, but for the permanent ones a map stamps.
///
/// Today: decals on the world and on brush entities, temporary and permanent; the decals of masked
/// (`{`) faces come with what cuts them (TODO.md).
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

/// A decal on one face: its image, the face and its model, and the part of the face the decal covers, a
/// convex fan in the model's space.
struct PlacedDecal {
    std::uint32_t texture = 0;
    std::uint32_t face = 0;
    assets::ModelIndex model{};
    std::vector<DecalVertex> polygon{};
};

/// How long a decal stays: until the pool needs its slot for another, or for good.
enum class DecalLife : std::uint8_t {
    Temporary,
    Permanent, ///< A map's (FDECAL_PERMANENT): no other decal takes its slot while another is free.
};

/// What a shot stamps: image `texture`, of `size`, on the faces of `model` near `center`, a point in
/// the model's own space, `scale` times smaller than its image, for `life`.
struct DecalShot {
    std::uint32_t texture = 0;
    DecalSize size{};
    assets::ModelIndex model = assets::kWorldModel;
    Vec3 center{};
    float scale = 1.0F; ///< Held to 0.01 to 16.
    DecalLife life = DecalLife::Temporary;
};

/// The decals of a map: a pool of decals on the faces of its models, each a slot, reused in turn when
/// full.
class DecalPool {
public:
    /// The most decals a pool holds (the original's MAX_RENDER_DECALS).
    static constexpr std::size_t kMostDecals = 4096;

    /// A pool for the models of `map`, which must outlive it, `faces` its faces' by their index, of at
    /// most `limit` decals on faces (setLimit).
    DecalPool(const assets::BspView& map, std::vector<DecalFace> faces, std::size_t limit);

    /// Takes the next decals in the first `limit` slots, kMostDecals at most; 0 stamps none. As the
    /// original's R_DecalAlloc reads r_decals at each decal: those past a lower limit stay until the
    /// overlap rule replaces them or a higher one brings their slots back into the ring.
    void setLimit(std::size_t limit);
    /// The slots it takes decals in now.
    [[nodiscard]] inline auto limit() const noexcept -> std::size_t { return m_limit; }

    /// Stamps `shot`, its model one of the map's, as the original's R_DecalShoot: the faces on the
    /// planes of the nodes of the model's tree it reaches within half the image's larger side that pass
    /// within 4 units of the center, each cut to the decal's square in the face's texture directions.
    /// On a face already under 6 temporary decals each over 60% covered by it, it replaces the one it
    /// covers least; otherwise it takes the pool's next slot not taken for good, whose decal goes, the
    /// next whatever it is once every slot is. A slot taken for good stays so, even when its decal
    /// misses the face, as the original's keeps its flags. Returns the faces it was stamped on.
    auto shoot(const DecalShot& shot) -> std::uint32_t;

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
        DecalLife life = DecalLife::Temporary;
    };
    /// A shot as the walk of its model's tree carries it: its scale held, and how far the walk looks
    /// (half the image's larger side, in texels).
    struct Walk {
        DecalShot shot{};
        float reach = 0.0F;
    };

    void walk(assets::NodeIndex index, const Walk& shot, std::uint32_t& stamped);
    /// Stamps `shot` on face `face` when it touches it; returns whether it did.
    auto stamp(std::uint32_t face, const Walk& shot) -> bool;
    /// The slot a decal on `face` takes: the temporary one it covers least of 6 or more temporary ones
    /// it covers mostly, or the pool's next not taken for good.
    [[nodiscard]] auto slotFor(std::uint32_t face, const DecalShot& shot, Vec3 sAxis, Vec3 tAxis)
        -> std::size_t;
    /// Empties slot `slot`, its decal leaving its face.
    void empty(std::size_t slot);
    /// The pool's next slot within its limit not taken for good, the last tried once each was
    /// (R_DecalAlloc); the one after it next, the first past the limit.
    [[nodiscard]] auto nextSlot() -> std::size_t;

    const assets::BspView* m_map;
    std::vector<DecalFace> m_faces;
    std::vector<Slot> m_slots;                            ///< As many as the highest limit asked for.
    std::vector<std::vector<std::size_t>> m_decalsOfFace; ///< Slots, in the order stamped on it.
    std::size_t m_limit = 0;
    std::size_t m_next = 0; ///< The slot the pool tries next.
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
