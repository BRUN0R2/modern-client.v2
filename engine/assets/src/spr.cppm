// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// SPR v2, the sprites of GoldSrc: the HUD, effects, smoke and the crosshair
/// (docs/06-assets-and-resources.md, section 3.1). Paletted frames, alone or in timed groups. And
/// Counter-Strike Online's version 3, its effects' (muzzle flashes...): frames of BC3 blocks.
export module nx.assets:spr;

import std;
import nx.core;
import :wad;

export namespace nx::assets {

/// The sprite version of GoldSrc.
inline constexpr std::int32_t kSpriteVersion = 2;
/// The sprite version of Counter-Strike Online, as its files are observed: GoldSrc's header without a
/// palette, each frame a single image, a DDS file of BC3 (DXT5) blocks of one level, with no origin,
/// centered on the sprite's.
inline constexpr std::int32_t kCompressedSpriteVersion = 3;

/// What can be wrong with a sprite file; the detail has the frame and the value.
enum class SprError : std::uint8_t {
    TooSmall,           ///< Smaller than the header, and the palette of version 2.
    NotSprite,          ///< The file does not start with "IDSP".
    UnsupportedVersion, ///< Neither version 2 nor 3.
    BadValue,           ///< A header value outside its domain, a palette that is not 256 colors, a group
                        ///< whose intervals are not positive and rising; a version 3 frame that is not
                        ///< a DDS of BC3 blocks of one level.
    FrameOutOfBounds,   ///< A frame does not fit in the file.
};

/// How a sprite faces the viewer.
enum class SpriteOrientation : std::uint8_t {
    ParallelUpright, ///< Faces the view plane, rotating only about the vertical.
    FacingUpright,   ///< Faces the viewer's position, rotating only about the vertical.
    Parallel,        ///< Faces the view plane.
    Oriented,        ///< Keeps the entity's angles.
    ParallelOriented,
    Count,
};

/// How a sprite's pixels blend.
enum class SpriteRendering : std::uint8_t {
    Normal,
    Additive,
    IndexAlpha, ///< The palette index is the alpha; the last color is the color.
    AlphaTest,  ///< The last palette index is transparent.
    Count,
};

/// Whether every instance animates in step, or each from a random start.
enum class SpriteSync : std::uint8_t {
    Synchronized,
    Random,
    Count,
};

/// How the pixels of a sprite's image are stored.
enum class SpritePixels : std::uint8_t {
    Indexed, ///< A byte a pixel, an index into the sprite's palette, row by row.
    Bc3,     ///< BC3 blocks of 4x4 pixels, row by row (nx.assets:blockCompression's decodeBc3).
    Count,
};

/// An image: its origin (the point drawn at the sprite's position) and its pixels, as a view of the
/// file's bytes.
struct SpriteImage {
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    SpritePixels format = SpritePixels::Indexed;
    std::span<const std::byte> pixels{};
};

/// A frame: one image, or a group of images that loops by itself, each shown until its interval
/// (time since the group started) has passed.
struct SpriteFrame {
    std::vector<SpriteImage> images{};
    std::vector<Seconds> intervals{}; ///< Rising and positive, one per image of a group; empty for one image.
};

/// An SPR v2 or v3 file: its header, its palette and frames as views of the bytes it was parsed from,
/// which must outlive it.
struct SpriteView {
    SpriteOrientation orientation = SpriteOrientation::ParallelUpright;
    SpriteRendering rendering = SpriteRendering::Normal;
    float boundingRadius = 0.0F;
    std::uint32_t maxWidth = 0;
    std::uint32_t maxHeight = 0;
    float beamLength = 0.0F;
    SpriteSync sync = SpriteSync::Synchronized;
    std::span<const std::byte> palette{}; ///< kPaletteColors RGB triples; none in version 3.
    std::vector<SpriteFrame> frames{};

    /// Errors: every SprError.
    [[nodiscard]] static auto parse(std::span<const std::byte> bytes) -> Result<SpriteView, SprError>;
};

} // namespace nx::assets
