// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

// The header: the magic, the entry count and the directory offset.
constexpr std::array kMagic{std::byte{'W'}, std::byte{'A'}, std::byte{'D'}, std::byte{'3'}};
constexpr std::size_t kCountOffset = 4;
constexpr std::size_t kDirectoryOffsetOffset = 8;
constexpr std::size_t kHeaderBytes = 12;

// A directory entry: the data's offset and size in the file (and its uncompressed size, at 8),
// its type, its compression (and two bytes of padding), and its name.
constexpr std::size_t kEntryBytes = 32;
constexpr std::size_t kEntryDiskSizeOffset = 4;
constexpr std::size_t kEntryTypeOffset = 12;
constexpr std::size_t kEntryCompressionOffset = 13;
constexpr std::size_t kEntryNameOffset = 16;

// A mip texture: its name, width and height, and the offsets of its levels from its start. The
// palette follows the last level: a 16-bit color count, then the colors.
constexpr std::size_t kTextureWidthOffset = 16;
constexpr std::size_t kTextureHeightOffset = 20;
constexpr std::size_t kTextureLevelsOffset = 24;
constexpr std::size_t kTextureHeaderBytes = kTextureLevelsOffset + (kMipLevels * sizeof(std::uint32_t));
constexpr std::size_t kPaletteCountBytes = sizeof(std::uint16_t);

/// Whether `length` bytes at `offset` lie inside `bytes`; 64-bit, so 32-bit fields cannot wrap.
[[nodiscard]] auto fits(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t length) noexcept
    -> bool {
    return offset <= bytes.size() && length <= bytes.size() - offset;
}

} // namespace

auto WadView::parse(std::span<const std::byte> bytes) -> Result<WadView, WadError> {
    if (bytes.size() < kHeaderBytes) {
        return fail(WadError::TooSmall,
                    std::format("{} bytes, less than the {} of the header", bytes.size(), kHeaderBytes));
    }
    if (!std::ranges::equal(bytes.first(kMagic.size()), kMagic)) {
        return fail(WadError::NotWad3, "the file does not start with \"WAD3\"");
    }
    const auto count = loadLittleEndian<std::uint32_t>(bytes.subspan(kCountOffset));
    const auto directory = loadLittleEndian<std::uint32_t>(bytes.subspan(kDirectoryOffsetOffset));
    if (!fits(bytes, directory, std::uint64_t{count} * kEntryBytes)) {
        return fail(
            WadError::DirectoryOutOfBounds,
            std::format("{} entries at {} are outside the file's {} bytes", count, directory, bytes.size()));
    }

    std::vector<WadEntry> entries;
    entries.reserve(count); // bounded by the file size, checked above
    for (std::size_t index = 0; index < count; ++index) {
        const auto record = bytes.subspan(directory + (index * kEntryBytes), kEntryBytes);
        WadEntry entry{
            .name = nameOf(record.subspan(kEntryNameOffset, kTextureNameBytes)),
            .type = static_cast<WadLumpType>(record[kEntryTypeOffset]),
            .data = {},
        };
        if (record[kEntryCompressionOffset] != std::byte{0}) {
            return fail(WadError::CompressedLump, std::format("entry {} {:?}", index, entry.name));
        }
        const auto offset = loadLittleEndian<std::uint32_t>(record);
        const auto size = loadLittleEndian<std::uint32_t>(record.subspan(kEntryDiskSizeOffset));
        if (!fits(bytes, offset, size)) {
            return fail(WadError::LumpOutOfBounds,
                        std::format("entry {} {:?}: {} bytes at {} are outside the file's {} bytes", index,
                                    entry.name, size, offset, bytes.size()));
        }
        entry.data = bytes.subspan(offset, size);
        entries.push_back(std::move(entry));
    }
    return WadView{std::move(entries)};
}

auto parseMipTexture(std::span<const std::byte> lump, TexturePixels pixels) -> Result<MipTexture, WadError> {
    if (lump.size() < kTextureHeaderBytes) {
        return fail(WadError::BadTexture, std::format("{} bytes, less than the {} of a mip texture header",
                                                      lump.size(), kTextureHeaderBytes));
    }
    MipTexture texture{
        .name = nameOf(lump.first(kTextureNameBytes)),
        .width = loadLittleEndian<std::uint32_t>(lump.subspan(kTextureWidthOffset)),
        .height = loadLittleEndian<std::uint32_t>(lump.subspan(kTextureHeightOffset)),
        .levels = {},
        .palette = {},
    };
    if (texture.width == 0 || texture.height == 0 || texture.width % kTextureSizeMultiple != 0 ||
        texture.height % kTextureSizeMultiple != 0) {
        return fail(WadError::BadTexture,
                    std::format("{:?}: {}x{} is not a nonzero multiple of {}", texture.name, texture.width,
                                texture.height, kTextureSizeMultiple));
    }
    const auto levelOffset = [lump](std::size_t level) {
        return loadLittleEndian<std::uint32_t>(
            lump.subspan(kTextureLevelsOffset + (level * sizeof(std::uint32_t))));
    };
    const bool isInWad =
        std::ranges::all_of(std::views::iota(std::size_t{0}, kMipLevels),
                            [&levelOffset](std::size_t level) { return levelOffset(level) == 0; });
    if (isInWad && pixels == TexturePixels::MayBeInWad) {
        return texture;
    }
    std::uint64_t levelsEnd = 0;
    for (std::size_t level = 0; level < kMipLevels; ++level) {
        const std::uint64_t offset = levelOffset(level);
        const std::uint64_t size = std::uint64_t{texture.width >> level} * (texture.height >> level);
        if (!fits(lump, offset, size)) {
            return fail(WadError::BadTexture,
                        std::format("{:?}: level {} ({} bytes at {}) is outside its {} bytes", texture.name,
                                    level, size, offset, lump.size()));
        }
        texture.levels.at(level) = lump.subspan(narrow<std::size_t>(offset), narrow<std::size_t>(size));
        levelsEnd = offset + size;
    }
    if (!fits(lump, levelsEnd, kPaletteCountBytes + kPaletteBytes)) {
        return fail(WadError::BadTexture,
                    std::format("{:?}: the palette after the last level is outside its {} bytes",
                                texture.name, lump.size()));
    }
    const auto paletteStart = narrow<std::size_t>(levelsEnd);
    const auto colors = loadLittleEndian<std::uint16_t>(lump.subspan(paletteStart));
    if (colors != kPaletteColors) {
        return fail(WadError::BadTexture,
                    std::format("{:?}: {} palette colors, not {}", texture.name, colors, kPaletteColors));
    }
    texture.palette = lump.subspan(paletteStart + kPaletteCountBytes, kPaletteBytes);
    return texture;
}

auto WadView::find(std::string_view name) const noexcept -> const WadEntry* {
    const auto found = std::ranges::find_if(
        m_entries, [name](const WadEntry& entry) { return equalsIgnoringCase(entry.name, name); });
    return found == m_entries.end() ? nullptr : &*found;
}

auto readTexture(const WadEntry& entry) -> Result<MipTexture, WadError> {
    if (entry.type != WadLumpType::MipTexture) {
        return fail(WadError::NotATexture,
                    std::format("{:?} has type {:#04x}", entry.name, std::to_underlying(entry.type)));
    }
    return parseMipTexture(entry.data, TexturePixels::Required);
}

} // namespace nx::assets
