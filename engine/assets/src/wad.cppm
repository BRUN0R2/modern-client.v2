// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// WAD3, the texture archives of GoldSrc (halflife.wad, cstrike.wad, decals.wad and the WADs a
/// map names; docs/06-assets-and-resources.md, section 3.1).
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

/// A mip texture: palette indices at four levels of detail, and the palette.
struct MipTexture {
    std::string name;
    std::uint32_t width = 0;  ///< A nonzero multiple of kTextureSizeMultiple.
    std::uint32_t height = 0; ///< A nonzero multiple of kTextureSizeMultiple.
    /// Views of the palette indices, row by row: level `i` is (width >> i) by (height >> i).
    std::array<std::span<const std::byte>, kMipLevels> levels{};
    std::span<const std::byte> palette; ///< A view of kPaletteColors RGB triples.
};

/// The mip texture of `entry`, with views of the same bytes. Errors: NotATexture, BadTexture.
[[nodiscard]] auto readTexture(const WadEntry& entry) -> Result<MipTexture, WadError>;

} // namespace nx::assets
