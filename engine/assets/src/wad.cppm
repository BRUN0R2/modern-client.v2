// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// WAD3, the texture archives of GoldSrc (halflife.wad, cstrike.wad, decals.wad and the WADs a
/// map names; docs/06-assets-and-resources.md, section 3.1), and the pictures and console fonts of
/// gfx.wad, fonts.wad and cached.wad.
export module nx.assets:wad;

import std;
import nx.core;

export namespace nx::assets {

/// The levels of a mip texture: the full size, then halved three times.
inline constexpr std::size_t kMipLevels = 4;

/// A texture palette: 256 RGB colors.
inline constexpr std::size_t kPaletteColors = 256;
inline constexpr std::size_t kPaletteBytes = kPaletteColors * 3;

/// The name field of WAD3 entries and mip textures: up to 15 characters and a terminator.
inline constexpr std::size_t kTextureNameBytes = 16;

/// GoldSrc texture sizes are multiples of this, so that every mip level has whole rows.
inline constexpr std::uint32_t kTextureSizeMultiple = 16;

/// What can be wrong with a WAD3 file or one of its textures; the detail has the offending values.
enum class WadError : std::uint8_t {
    TooSmall,             ///< Smaller than the WAD3 header.
    NotWad3,              ///< The file does not start with "WAD3".
    DirectoryOutOfBounds, ///< The directory does not fit in the file.
    LumpOutOfBounds,      ///< An entry's data does not fit in the file.
    CompressedLump,       ///< An entry is compressed, which no GoldSrc tool writes.
    NotATexture,          ///< The entry holds something other than a mip texture.
    BadTexture,           ///< A mip texture breaks its format.
    NotAPicture,          ///< The entry holds something other than a picture.
    BadPicture,           ///< A picture breaks its format.
    NotAFont,             ///< The entry holds something other than a console font.
    BadFont,              ///< A console font breaks its format.
};

/// What a WAD3 entry holds. An entry may hold a value without an enumerator: a type this reader
/// does not decode.
enum class WadLumpType : std::uint8_t {
    Picture = 0x42,    ///< A plain picture (qpic): its size, pixels and palette.
    MipTexture = 0x43, ///< A texture with its mip levels and palette.
    Font = 0x46,       ///< A console font.
};

/// One entry of a WAD3 directory.
struct WadEntry {
    std::string name; ///< At most kTextureNameBytes characters; lookups ignore ASCII case.
    WadLumpType type{};
    std::span<const std::byte> data; ///< A view of the file's bytes.
};

/// A WAD3 file: its directory, with views of the bytes it was parsed from, which must outlive it.
class WadView {
public:
    /// Errors: TooSmall, NotWad3, DirectoryOutOfBounds, LumpOutOfBounds, CompressedLump.
    [[nodiscard]] static auto parse(std::span<const std::byte> bytes) -> Result<WadView, WadError>;

    /// The entries in directory order.
    [[nodiscard]] inline auto entries() const noexcept -> std::span<const WadEntry> { return m_entries; }

    /// The first entry named `name`, ignoring ASCII case, as GoldSrc looks textures up; null when
    /// there is none.
    [[nodiscard]] auto find(std::string_view name) const noexcept -> const WadEntry*;

private:
    inline explicit WadView(std::vector<WadEntry> entries) noexcept : m_entries{std::move(entries)} {}

    std::vector<WadEntry> m_entries;
};

/// A mip texture: palette indices at four levels of detail, and the palette. A map may name a
/// texture without its pixels, which are in one of the WADs it lists: its levels and palette are
/// then empty.
struct MipTexture {
    std::string name;
    std::uint32_t width = 0;  ///< A nonzero multiple of kTextureSizeMultiple.
    std::uint32_t height = 0; ///< A nonzero multiple of kTextureSizeMultiple.
    /// Views of the palette indices, row by row: level `i` is (width >> i) by (height >> i).
    std::array<std::span<const std::byte>, kMipLevels> levels{};
    std::span<const std::byte> palette{}; ///< A view of kPaletteColors RGB triples.

    /// Whether the pixels are here, rather than in a WAD.
    [[nodiscard]] inline auto hasPixels() const noexcept -> bool { return !palette.empty(); }
};

/// The mip texture of `entry`, with views of the same bytes. Errors: NotATexture, BadTexture.
[[nodiscard]] auto readTexture(const WadEntry& entry) -> Result<MipTexture, WadError>;

/// A picture (qpic): palette indices, row by row, and the palette. The console's background
/// (CONBACK) and the loading screen are pictures; index 255 is where a masked picture shows
/// through.
struct WadPicture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::byte> pixels{};  ///< A view of width times height palette indices.
    std::span<const std::byte> palette{}; ///< A view of kPaletteColors RGB triples.
};

/// The picture of `entry`, with views of the same bytes. Errors: NotAPicture, BadPicture.
[[nodiscard]] auto readPicture(const WadEntry& entry) -> Result<WadPicture, WadError>;

/// The characters of a console font: the 256 of a code page.
inline constexpr std::size_t kFontGlyphs = 256;

/// Where a character of a console font is drawn from in its picture: its left and top, and its
/// width; every glyph of a font is FontView::rowHeight tall.
struct FontGlyph {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
};

/// A console font (CONCHARS and CREDITSFONT of gfx.wad, FONT0 to FONT2 of fonts.wad): a picture of
/// its characters in rows, index 255 where they show through, and where each character is.
struct FontView {
    std::uint32_t rowHeight = 0; ///< The height of every glyph.
    WadPicture picture{};
    std::array<FontGlyph, kFontGlyphs> glyphs{};
};

/// The console font of `entry`, as GoldSrc reads it, with views of the same bytes: its picture is
/// 256 pixels wide (the width field is a character's, but in the fonts of Half-Life 1.1.0.0, whose
/// size says so, it is a sixteenth of the picture's); its palette counts 256 colors, or 768, as
/// fonts.wad writes it. Errors: NotAFont, BadFont (a glyph outside the picture among them).
[[nodiscard]] auto readFont(const WadEntry& entry) -> Result<FontView, WadError>;

} // namespace nx::assets

namespace nx::assets {

/// Whether a mip texture must carry its pixels (a WAD3 entry) or may leave them to a WAD (a map).
enum class TexturePixels : std::uint8_t {
    Required,
    MayBeInWad,
};

/// The mip texture in `lump`; with MayBeInWad, every level offset 0 means only its name and size
/// are here. For the readers of this module. Errors: BadTexture.
[[nodiscard]] auto parseMipTexture(std::span<const std::byte> lump, TexturePixels pixels)
    -> Result<MipTexture, WadError>;

} // namespace nx::assets
