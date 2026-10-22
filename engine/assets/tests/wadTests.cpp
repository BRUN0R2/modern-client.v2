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

/// A palette of 256 gray colors after its count, `colors`.
void appendPalette(Bytes& lump, std::uint16_t colors) {
    append(lump, colors);
    for (std::size_t color = 0; color < 256; ++color) {
        lump.insert(lump.end(), 3, static_cast<std::byte>(color));
    }
}

/// A picture lump of `width` by `height` pixels, pixel `i` being `i` modulo 256.
[[nodiscard]] auto picture(std::uint32_t width, std::uint32_t height, std::uint16_t colors = 256) -> Bytes {
    Bytes lump;
    append(lump, width);
    append(lump, height);
    for (std::size_t pixel = 0; pixel < std::size_t{width} * height; ++pixel) {
        lump.push_back(static_cast<std::byte>(pixel % 256));
    }
    appendPalette(lump, colors);
    lump.insert(lump.end(), 2, std::byte{0}); // padding, as the tools write it
    return lump;
}

TEST(WadTest, ReadsAPicture) {
    const Bytes file = wadFile({{.name = "CONBACK", .type = WadLumpType::Picture, .data = picture(32, 20)}});
    const auto wad = WadView::parse(file);
    ASSERT_TRUE(wad.has_value());
    const auto read = nx::assets::readPicture(wad->entries()[0]);
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    EXPECT_EQ(read->width, 32U);
    EXPECT_EQ(read->height, 20U);
    ASSERT_EQ(read->pixels.size(), 640U);
    EXPECT_EQ(read->pixels[33], std::byte{33});
    ASSERT_EQ(read->palette.size(), kPaletteBytes);
    EXPECT_EQ(read->palette[3 * 200], std::byte{200});

    const auto pictureError = [](Bytes lump, WadLumpType type = WadLumpType::Picture) {
        const Bytes broken = wadFile({{.name = "BROKEN", .type = type, .data = std::move(lump)}});
        const auto parsed = WadView::parse(broken);
        const auto result = nx::assets::readPicture(parsed->entries()[0]);
        return result ? WadError{} : result.error().code;
    };
    EXPECT_EQ(pictureError(picture(8, 8), WadLumpType::MipTexture), WadError::NotAPicture);
    EXPECT_EQ(pictureError(Bytes(4, std::byte{0})), WadError::BadPicture) << "no header";
    EXPECT_EQ(pictureError(picture(8, 8, 255)), WadError::BadPicture) << "255 colors";
    Bytes tooTall = picture(8, 8);
    tooTall[4] = std::byte{9};
    EXPECT_EQ(pictureError(std::move(tooTall)), WadError::BadPicture) << "its pixels past its end";
}

/// A console font of a 256 by 32 picture, rows 10 tall: character `c` is `c % 8 + 2` wide at
/// (c % 16 * 16, c / 16 % 3 * 10); its palette counts `colors`, and `trailer` bytes follow it.
[[nodiscard]] auto font(std::uint32_t widthField, std::uint16_t colors = 256, std::size_t trailer = 2)
    -> Bytes {
    constexpr std::uint32_t kHeight = 32;
    constexpr std::uint32_t kRowHeight = 10;
    Bytes lump;
    append(lump, widthField);
    append(lump, kHeight);
    append(lump, std::uint32_t{3}); // rows
    append(lump, kRowHeight);
    for (std::uint32_t character = 0; character < 256; ++character) {
        append(lump,
               static_cast<std::uint16_t>((character / 16 % 3 * kRowHeight * 256) + (character % 16 * 16)));
        append(lump, static_cast<std::uint16_t>((character % 8) + 2));
    }
    for (std::size_t pixel = 0; pixel < std::size_t{256} * kHeight; ++pixel) {
        lump.push_back(pixel % 7 == 0 ? std::byte{255} : std::byte{15});
    }
    appendPalette(lump, colors);
    lump.insert(lump.end(), trailer, std::byte{0});
    return lump;
}

TEST(WadTest, ReadsAConsoleFont) {
    const Bytes file = wadFile({{.name = "CONCHARS", .type = WadLumpType::Font, .data = font(12)}});
    const auto wad = WadView::parse(file);
    ASSERT_TRUE(wad.has_value());
    const auto read = nx::assets::readFont(wad->entries()[0]);
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    EXPECT_EQ(read->rowHeight, 10U);
    EXPECT_EQ(read->picture.width, 256U) << "whatever the width field says";
    EXPECT_EQ(read->picture.height, 32U);
    EXPECT_EQ(read->picture.pixels.size(), 256U * 32U);
    EXPECT_EQ(read->picture.pixels[7], std::byte{255});
    EXPECT_EQ(read->glyphs['A'].x, 16U); // 65: the second of its row
    EXPECT_EQ(read->glyphs['A'].y, 10U); // 65 / 16 = 4, 4 % 3 = 1
    EXPECT_EQ(read->glyphs['A'].width, 3U);
    EXPECT_EQ(read->glyphs[255].width, 9U);

    // fonts.wad: a width field of 16, sixteen to the picture's width, a palette counting its 768
    // bytes, and 64 bytes after it, which make its size the one Half-Life 1.1.0.0 writes.
    const Bytes newStyle = wadFile({{.name = "FONT0", .type = WadLumpType::Font, .data = font(16, 768, 64)}});
    const auto wadOfFonts = WadView::parse(newStyle);
    ASSERT_TRUE(wadOfFonts.has_value());
    const auto font0 = nx::assets::readFont(wadOfFonts->entries()[0]);
    ASSERT_TRUE(font0.has_value()) << nx::describe(font0.error());
    EXPECT_EQ(font0->picture.width, 256U);
}

TEST(WadTest, RejectsBrokenFonts) {
    const auto fontError = [](Bytes lump, WadLumpType type = WadLumpType::Font) {
        const Bytes broken = wadFile({{.name = "BROKEN", .type = type, .data = std::move(lump)}});
        const auto parsed = WadView::parse(broken);
        const auto result = nx::assets::readFont(parsed->entries()[0]);
        return result ? WadError{} : result.error().code;
    };
    EXPECT_EQ(fontError(font(12), WadLumpType::Picture), WadError::NotAFont);
    EXPECT_EQ(fontError(Bytes(100, std::byte{0})), WadError::BadFont) << "no header";
    EXPECT_EQ(fontError(font(12, 100)), WadError::BadFont) << "100 colors";
    Bytes flat = font(12);
    flat[12] = std::byte{0}; // rows 0 tall
    EXPECT_EQ(fontError(std::move(flat)), WadError::BadFont);
    Bytes outside = font(12);
    outside[16 + (4 * 'A') + 2] = std::byte{250}; // 'A' 250 wide at x 16
    EXPECT_EQ(fontError(std::move(outside)), WadError::BadFont);
}

} // namespace
