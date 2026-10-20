// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::kMipLevels;
using nx::assets::kPaletteBytes;
using nx::assets::kTextureNameBytes;
using nx::assets::readTexture;
using nx::assets::WadError;
using nx::assets::WadLumpType;
using nx::assets::WadView;

using Bytes = std::vector<std::byte>;

template <class Value>
void append(Bytes& bytes, Value value) {
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        bytes.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (index * 8)) & 0xFF));
    }
}

void appendName(Bytes& bytes, std::string_view name) {
    for (std::size_t index = 0; index < kTextureNameBytes; ++index) {
        bytes.push_back(index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{0});
    }
}

/// A mip texture lump: every pixel of level `i` is `i`, and color `c` of the palette is (c, c, c).
[[nodiscard]] auto mipTexture(std::string_view name, std::uint32_t width, std::uint32_t height,
                              std::uint16_t paletteColors = 256) -> Bytes {
    Bytes lump;
    appendName(lump, name);
    append(lump, width);
    append(lump, height);
    std::uint32_t offset = 40;
    for (std::size_t level = 0; level < kMipLevels; ++level) {
        append(lump, offset);
        offset += (width >> level) * (height >> level);
    }
    for (std::size_t level = 0; level < kMipLevels; ++level) {
        lump.insert(lump.end(), std::size_t{width >> level} * (height >> level),
                    static_cast<std::byte>(level));
    }
    append(lump, paletteColors);
    for (std::size_t color = 0; color < 256; ++color) {
        lump.insert(lump.end(), 3, static_cast<std::byte>(color));
    }
    lump.insert(lump.end(), 2, std::byte{0}); // padding, as the tools write it
    return lump;
}

struct Lump {
    std::string name;
    WadLumpType type = WadLumpType::MipTexture;
    Bytes data;
    std::uint8_t compression = 0;
};

/// A WAD3 file: the header, the lumps, then the directory.
[[nodiscard]] auto wadFile(const std::vector<Lump>& lumps) -> Bytes {
    Bytes file{std::byte{'W'}, std::byte{'A'}, std::byte{'D'}, std::byte{'3'}};
    append(file, static_cast<std::uint32_t>(lumps.size()));
    append(file, std::uint32_t{0}); // the directory offset, written below
    std::vector<std::uint32_t> offsets;
    for (const Lump& lump : lumps) {
        offsets.push_back(static_cast<std::uint32_t>(file.size()));
        file.insert(file.end(), lump.data.begin(), lump.data.end());
    }
    const auto directory = static_cast<std::uint32_t>(file.size());
    for (std::size_t index = 0; index < lumps.size(); ++index) {
        const Lump& lump = lumps[index];
        append(file, offsets[index]);
        append(file, static_cast<std::uint32_t>(lump.data.size()));
        append(file, static_cast<std::uint32_t>(lump.data.size()));
        append(file, std::to_underlying(lump.type));
        append(file, lump.compression);
        append(file, std::uint16_t{0});
        appendName(file, lump.name);
    }
    Bytes directoryOffset;
    append(directoryOffset, directory);
    std::ranges::copy(directoryOffset, file.begin() + 8);
    return file;
}

[[nodiscard]] auto parseError(const Bytes& file) -> WadError {
    const auto wad = WadView::parse(file);
    EXPECT_FALSE(wad.has_value());
    return wad ? WadError{} : wad.error().code;
}

TEST(WadTest, ListsTheDirectoryAndFindsNamesWhateverTheCase) {
    const Bytes file = wadFile({
        {.name = "{BLUE", .data = mipTexture("{BLUE", 16, 16)},
        {.name = "CONCHARS", .type = WadLumpType::Font, .data = Bytes(8, std::byte{1})},
    });
    const auto wad = WadView::parse(file);
    ASSERT_TRUE(wad.has_value()) << nx::describe(wad.error());
    ASSERT_EQ(wad->entries().size(), 2U);
    EXPECT_EQ(wad->entries()[0].name, "{BLUE");
    EXPECT_EQ(wad->entries()[1].type, WadLumpType::Font);
    EXPECT_EQ(wad->entries()[1].data.size(), 8U);

    const auto* found = wad->find("{blue");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "{BLUE");
    EXPECT_EQ(wad->find("missing"), nullptr);
}

TEST(WadTest, TheFirstEntryOfANameWins) {
    const Bytes file = wadFile({
        {.name = "AAATRIGGER", .data = mipTexture("AAATRIGGER", 16, 16)},
        {.name = "aaatrigger", .data = mipTexture("aaatrigger", 32, 32)},
    });
    const auto wad = WadView::parse(file);
    ASSERT_TRUE(wad.has_value()) << nx::describe(wad.error());
    const auto* found = wad->find("AAATrigger");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, wad->entries().data());
}

TEST(WadTest, ANameMayFillItsSixteenBytes) {
    const Bytes file = wadFile({{.name = "SIXTEEN_LETTERS_", .data = mipTexture("x", 16, 16)}});
    const auto wad = WadView::parse(file);
    ASSERT_TRUE(wad.has_value()) << nx::describe(wad.error());
    EXPECT_EQ(wad->entries()[0].name, "SIXTEEN_LETTERS_");
}

TEST(WadTest, ReadsAMipTexture) {
    const Bytes file = wadFile({{.name = "CRATE01", .data = mipTexture("CRATE01", 64, 32)}});
    const auto wad = WadView::parse(file);
    ASSERT_TRUE(wad.has_value()) << nx::describe(wad.error());

    const auto texture = readTexture(wad->entries()[0]);
    ASSERT_TRUE(texture.has_value()) << nx::describe(texture.error());
    EXPECT_EQ(texture->name, "CRATE01");
    EXPECT_EQ(texture->width, 64U);
    EXPECT_EQ(texture->height, 32U);
    for (std::size_t level = 0; level < kMipLevels; ++level) {
        const auto pixels = texture->levels.at(level);
        EXPECT_EQ(pixels.size(), (std::size_t{64} >> level) * (std::size_t{32} >> level)) << level;
        EXPECT_TRUE(std::ranges::all_of(pixels, [level](std::byte pixel) {
            return pixel == static_cast<std::byte>(level);
        })) << level;
    }
    ASSERT_EQ(texture->palette.size(), kPaletteBytes);
    EXPECT_EQ(texture->palette[3 * 200], std::byte{200});
}

TEST(WadTest, RejectsBrokenFiles) {
    EXPECT_EQ(parseError(Bytes(5, std::byte{0})), WadError::TooSmall);

    Bytes wad2 = wadFile({});
    wad2[3] = std::byte{'2'};
    EXPECT_EQ(parseError(wad2), WadError::NotWad3);

    Bytes tooManyEntries = wadFile({{.name = "A", .data = mipTexture("A", 16, 16)}});
    tooManyEntries[4] = std::byte{2};
    EXPECT_EQ(parseError(tooManyEntries), WadError::DirectoryOutOfBounds);

    Bytes hugeCount = wadFile({});
    std::ranges::fill(std::span{hugeCount}.subspan(4, 4), std::byte{0xFF});
    EXPECT_EQ(parseError(hugeCount), WadError::DirectoryOutOfBounds);

    Bytes lumpOutside = wadFile({{.name = "A", .data = mipTexture("A", 16, 16)}});
    const std::size_t directory = lumpOutside.size() - 32;
    lumpOutside[directory + 5] = std::byte{0x40}; // the size grows by 16 KiB
    EXPECT_EQ(parseError(lumpOutside), WadError::LumpOutOfBounds);

    EXPECT_EQ(parseError(wadFile({{.name = "A", .data = mipTexture("A", 16, 16), .compression = 1}})),
              WadError::CompressedLump);
}

TEST(WadTest, RejectsBrokenTextures) {
    const auto textureError = [](Bytes lump, WadLumpType type = WadLumpType::MipTexture) {
        const Bytes file = wadFile({{.name = "T", .type = type, .data = std::move(lump)}});
        const auto wad = WadView::parse(file);
        EXPECT_TRUE(wad.has_value()) << nx::describe(wad.error());
        if (!wad) {
            return WadError{};
        }
        const auto texture = readTexture(wad->entries()[0]);
        EXPECT_FALSE(texture.has_value());
        return texture ? WadError{} : texture.error().code;
    };

    EXPECT_EQ(textureError(mipTexture("T", 16, 16), WadLumpType::Picture), WadError::NotATexture);
    EXPECT_EQ(textureError(Bytes(39, std::byte{0})), WadError::BadTexture) << "shorter than its header";
    EXPECT_EQ(textureError(mipTexture("T", 0, 16)), WadError::BadTexture) << "no width";
    EXPECT_EQ(textureError(mipTexture("T", 20, 16)), WadError::BadTexture) << "not a multiple of 16";
    EXPECT_EQ(textureError(mipTexture("T", 16, 16, 255)), WadError::BadTexture) << "255 palette colors";

    Bytes levelOutside = mipTexture("T", 16, 16);
    levelOutside[24 + 13] = std::byte{0x10}; // level 3 starts beyond the lump
    EXPECT_EQ(textureError(std::move(levelOutside)), WadError::BadTexture);

    Bytes noPalette = mipTexture("T", 16, 16);
    noPalette.resize(noPalette.size() - kPaletteBytes);
    EXPECT_EQ(textureError(std::move(noPalette)), WadError::BadTexture);
}

} // namespace
