// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: a TrueType font built byte by byte, whose glyphs are squares a test can draw in its
// head. Include after `import std;`.

namespace nx::test {

/// What a SquareFont is made with.
struct SquareFontDesc {
    std::string family = "Square";  ///< Its name ID 1.
    std::string style = "Regular";  ///< Its name ID 2.
    std::uint16_t weight = 400;     ///< Its OS/2 usWeightClass.
    bool isItalic = false;          ///< Its OS/2 fsSelection's italic bit.
    std::uint16_t winAscent = 2049; ///< Tahoma's, so that the font sizes as Tahoma does.
    std::uint16_t winDescent = 423;
    /// The VDMX records of a version 1 table for square pixels (yPelHeight, yMax, yMin); no table when
    /// empty.
    std::vector<std::array<std::int16_t, 3>> deviceHeights{};
    /// The ranges of a gasp table (rangeMaxPPEM, rangeGaspBehavior); no table when empty.
    std::vector<std::array<std::uint16_t, 2>> gaspRanges{};
};

/// A TrueType font of the family "Square", style "Regular" unless its desc names others, with an em
/// of 2048 units and no hinting instructions. At 16 pixels per em, a pixel is 128 units:
/// - 'A' is a square half an em wide (8 pixels) sitting on the baseline, its left on the pen;
/// - 'B' is the same square a quarter of a pixel to the right;
/// - the space has no outline;
/// each advancing 1280 units (10 pixels), the space 512 (4 pixels). Nothing else has a glyph.
class SquareFont {
public:
    static constexpr std::int16_t kUnitsPerEm = 2048;
    static constexpr std::int16_t kSide = 1024;
    static constexpr std::int16_t kQuarterPixelAt16 = 32;
    static constexpr std::uint16_t kAdvance = 1280;
    static constexpr std::uint16_t kSpaceAdvance = 512;

    [[nodiscard]] static auto build(const SquareFontDesc& desc = {}) -> std::vector<std::byte> {
        return assemble(tablesOf(desc), 0);
    }

    /// A TrueType collection (.ttc) of a font for each of `fonts`, in their order.
    [[nodiscard]] static auto collection(std::span<const SquareFontDesc> fonts) -> std::vector<std::byte> {
        Bytes bytes;
        for (const char letter : {'t', 't', 'c', 'f'}) {
            bytes.push_back(static_cast<std::byte>(letter));
        }
        put(bytes, std::uint32_t{0x0001'0000});
        put(bytes, static_cast<std::uint32_t>(fonts.size()));
        std::size_t offset = bytes.size() + (fonts.size() * 4);
        Bytes faces;
        for (const SquareFontDesc& font : fonts) {
            put(bytes, static_cast<std::uint32_t>(offset));
            const Bytes face = assemble(tablesOf(font), offset); // its tables where the face lands
            faces.insert(faces.end(), face.begin(), face.end());
            offset += face.size();
        }
        bytes.insert(bytes.end(), faces.begin(), faces.end());
        return bytes;
    }

private:
    using Bytes = std::vector<std::byte>;

    struct Table {
        std::string_view tag; // in the order of their tags, as the directory lists them
        Bytes bytes;
    };

    [[nodiscard]] static auto tablesOf(const SquareFontDesc& desc) -> std::vector<Table> {
        std::vector<Table> tables;
        tables.push_back({"OS/2", windowsMetrics(desc)});
        if (!desc.deviceHeights.empty()) {
            tables.push_back({"VDMX", deviceHeights(desc)});
        }
        tables.push_back({"cmap", characterMap()});
        if (!desc.gaspRanges.empty()) {
            tables.push_back({"gasp", gridFitting(desc)});
        }
        const auto [glyphs, locations] = glyphData();
        tables.push_back({"glyf", glyphs});
        tables.push_back({"head", header()});
        tables.push_back({"hhea", horizontalHeader(desc)});
        tables.push_back({"hmtx", horizontalMetrics()});
        tables.push_back({"loca", locations});
        tables.push_back({"maxp", maximumProfile()});
        tables.push_back({"name", names(desc)});
        tables.push_back({"post", postScript()});
        return tables;
    }

    static constexpr std::uint16_t kGlyphCount = 4; // .notdef, 'A', 'B', the space

    template <class Value>
    static void put(Bytes& bytes, Value value) {
        auto raw = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
        if constexpr (std::endian::native == std::endian::little) {
            std::ranges::reverse(raw); // TrueType is big-endian
        }
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }

    static void putAll(Bytes& bytes, std::initializer_list<std::int16_t> values) {
        for (const std::int16_t value : values) {
            put(bytes, value);
        }
    }

    static void padToFour(Bytes& bytes) {
        while (bytes.size() % 4 != 0) {
            bytes.push_back(std::byte{0});
        }
    }

    [[nodiscard]] static auto windowsMetrics(const SquareFontDesc& desc) -> Bytes {
        Bytes bytes;
        put(bytes, std::uint16_t{1});                  // version
        put(bytes, std::int16_t{1024});                // xAvgCharWidth
        put(bytes, desc.weight);                       // usWeightClass
        put(bytes, std::uint16_t{5});                  // usWidthClass
        put(bytes, std::uint16_t{0});                  // fsType
        putAll(bytes, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); // sub- and superscripts, strikeout
        put(bytes, std::int16_t{0});                   // sFamilyClass
        bytes.insert(bytes.end(), 10, std::byte{0});   // panose
        bytes.insert(bytes.end(), 16, std::byte{0});   // ulUnicodeRange1-4
        for (const char letter : {'N', 'X', 'T', 'S'}) {
            bytes.push_back(static_cast<std::byte>(letter)); // achVendID
        }
        const auto selection =
            static_cast<std::uint16_t>((desc.isItalic ? 0x01 : 0) | (desc.weight >= 700 ? 0x20 : 0));
        put(bytes,
            selection != 0 ? selection : std::uint16_t{0x40}); // fsSelection: italic, bold, else regular
        put(bytes, std::uint16_t{0x20});                       // usFirstCharIndex
        put(bytes, std::uint16_t{0x42});                       // usLastCharIndex
        putAll(bytes, {1638, -410, 0});                        // sTypoAscender, sTypoDescender, sTypoLineGap
        put(bytes, desc.winAscent);                            // usWinAscent
        put(bytes, desc.winDescent);                           // usWinDescent
        put(bytes, std::uint32_t{1});                          // ulCodePageRange1: Latin 1
        put(bytes, std::uint32_t{0});                          // ulCodePageRange2
        return bytes;
    }

    [[nodiscard]] static auto deviceHeights(const SquareFontDesc& desc) -> Bytes {
        Bytes bytes;
        const auto count = static_cast<std::uint16_t>(desc.deviceHeights.size());
        putAll(bytes, {1, 1, 1});                   // version 1, one group, one ratio
        bytes.insert(bytes.end(), 4, std::byte{1}); // any characters, 1:1
        put(bytes, std::uint16_t{12});              // the group's offset
        put(bytes, count);
        bytes.push_back(static_cast<std::byte>(desc.deviceHeights.front()[0]));
        bytes.push_back(static_cast<std::byte>(desc.deviceHeights.back()[0]));
        for (const auto& [pixelsPerEm, top, bottom] : desc.deviceHeights) {
            putAll(bytes, {pixelsPerEm, top, bottom});
        }
        return bytes;
    }

    [[nodiscard]] static auto gridFitting(const SquareFontDesc& desc) -> Bytes {
        Bytes bytes;
        put(bytes, std::uint16_t{1}); // version
        put(bytes, static_cast<std::uint16_t>(desc.gaspRanges.size()));
        for (const auto& [largest, behavior] : desc.gaspRanges) {
            put(bytes, largest);
            put(bytes, behavior);
        }
        return bytes;
    }

    [[nodiscard]] static auto characterMap() -> Bytes {
        // One Windows Unicode subtable of format 4: the space to glyph 3, 'A' and 'B' to 1 and 2.
        Bytes bytes;
        putAll(bytes, {0, 1});         // version, one subtable
        putAll(bytes, {3, 1});         // Windows, Unicode BMP
        put(bytes, std::uint32_t{12}); // its offset
        constexpr std::int16_t kSegments = 3;
        putAll(bytes, {4, 16 + (8 * kSegments), 0}); // format, length, language
        putAll(bytes, {2 * kSegments, 4, 1, 2});     // segCountX2, searchRange, entrySelector, rangeShift
        putAll(bytes, {0x20, 0x42, -1});             // endCode
        put(bytes, std::int16_t{0});                 // reservedPad
        putAll(bytes, {0x20, 0x41, -1});             // startCode
        putAll(bytes, {3 - 0x20, 1 - 0x41, 1});      // idDelta
        putAll(bytes, {0, 0, 0});                    // idRangeOffset
        return bytes;
    }

    /// A glyph of one square contour from (left, 0) to (left + kSide, kSide), clockwise.
    [[nodiscard]] static auto square(std::int16_t left) -> Bytes {
        Bytes bytes;
        putAll(bytes, {1, left, 0, static_cast<std::int16_t>(left + kSide), kSide}); // contours, bounds
        putAll(bytes, {3, 0});                         // last point, no instructions
        bytes.insert(bytes.end(), 4, std::byte{0x01}); // each point on the curve
        putAll(bytes, {left, 0, kSide, 0});            // x, each from the last
        putAll(bytes, {0, kSide, 0, -kSide});          // y, each from the last
        return bytes;
    }

    [[nodiscard]] static auto glyphData() -> std::pair<Bytes, Bytes> {
        const Bytes squareA = square(0);
        const Bytes squareB = square(kQuarterPixelAt16);
        Bytes glyphs = squareA;
        glyphs.insert(glyphs.end(), squareB.begin(), squareB.end());
        Bytes locations; // short offsets, halved: .notdef and the space have no outline
        for (const std::size_t offset :
             {std::size_t{0}, std::size_t{0}, squareA.size(), glyphs.size(), glyphs.size()}) {
            put(locations, static_cast<std::uint16_t>(offset / 2));
        }
        return {glyphs, locations};
    }

    [[nodiscard]] static auto header() -> Bytes {
        Bytes bytes;
        put(bytes, std::uint32_t{0x0001'0000}); // version
        put(bytes, std::uint32_t{0x0001'0000}); // fontRevision
        put(bytes, std::uint32_t{0});           // checksumAdjustment
        put(bytes, std::uint32_t{0x5F0F'3CF5}); // magicNumber
        put(bytes, std::uint16_t{0x0009});      // flags: baseline at 0, whole pixels per em
        put(bytes, kUnitsPerEm);
        bytes.insert(bytes.end(), 16, std::byte{0}); // created, modified
        putAll(bytes, {0, 0, static_cast<std::int16_t>(kQuarterPixelAt16 + kSide), kSide}); // bounds
        putAll(bytes, {0, 8, 2, 0, 0}); // macStyle, lowestRecPPEM, fontDirectionHint, short offsets, format
        return bytes;
    }

    [[nodiscard]] static auto horizontalHeader(const SquareFontDesc& desc) -> Bytes {
        Bytes bytes;
        put(bytes, std::uint32_t{0x0001'0000});
        putAll(bytes,
               {static_cast<std::int16_t>(desc.winAscent), static_cast<std::int16_t>(-desc.winDescent), 0});
        put(bytes, kAdvance);                                                        // advanceWidthMax
        putAll(bytes, {0, 0, static_cast<std::int16_t>(kQuarterPixelAt16 + kSide)}); // bearings, extent
        putAll(bytes, {1, 0, 0, 0, 0, 0, 0});                                        // caret, reserved
        put(bytes, std::int16_t{0});                                                 // metricDataFormat
        put(bytes, kGlyphCount);                                                     // numberOfHMetrics
        return bytes;
    }

    [[nodiscard]] static auto horizontalMetrics() -> Bytes {
        Bytes bytes;
        for (const auto& [advance, bearing] :
             std::array{std::pair{kAdvance, std::int16_t{0}}, std::pair{kAdvance, std::int16_t{0}},
                        std::pair{kAdvance, kQuarterPixelAt16}, std::pair{kSpaceAdvance, std::int16_t{0}}}) {
            put(bytes, advance);
            put(bytes, bearing);
        }
        return bytes;
    }

    [[nodiscard]] static auto maximumProfile() -> Bytes {
        Bytes bytes;
        put(bytes, std::uint32_t{0x0001'0000});
        put(bytes, kGlyphCount);
        putAll(bytes, {4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0}); // points, contours, zones...: no instructions
        return bytes;
    }

    [[nodiscard]] static auto names(const SquareFontDesc& desc) -> Bytes {
        // The family (1) and the style (2), for Windows, in UTF-16 big-endian.
        const std::array<std::string_view, 2> texts{desc.family, desc.style};
        Bytes bytes;
        putAll(bytes, {0, 2, 6 + (12 * 2)}); // format, count, stringOffset
        std::int16_t offset = 0;
        for (std::int16_t id = 1; const std::string_view name : texts) {
            const auto length = static_cast<std::int16_t>(name.size() * 2);
            putAll(bytes, {3, 1, 0x409, id++, length, offset});
            offset = static_cast<std::int16_t>(offset + length);
        }
        for (const std::string_view name : texts) {
            for (const char letter : name) {
                put(bytes, static_cast<std::uint16_t>(letter));
            }
        }
        return bytes;
    }

    [[nodiscard]] static auto postScript() -> Bytes {
        Bytes bytes;
        put(bytes, std::uint32_t{0x0003'0000}); // version 3: no glyph names
        bytes.insert(bytes.end(), 28, std::byte{0});
        return bytes;
    }

    /// The offset table and the directory, then each table, padded to four bytes; `base` bytes into
    /// the file, which the tables' offsets count from.
    [[nodiscard]] static auto assemble(const std::vector<Table>& tables, std::size_t base) -> Bytes {
        constexpr std::size_t kDirectoryEntrySize = 16;
        const auto count = static_cast<std::uint16_t>(tables.size());
        Bytes bytes;
        put(bytes, std::uint32_t{0x0001'0000});
        put(bytes, count);
        const auto power = static_cast<std::uint16_t>(std::bit_floor(count));
        put(bytes, static_cast<std::uint16_t>(power * 16));
        put(bytes, static_cast<std::uint16_t>(std::countr_zero(power)));
        put(bytes, static_cast<std::uint16_t>((count - power) * 16));
        std::size_t offset = base + bytes.size() + (tables.size() * kDirectoryEntrySize);
        Bytes data;
        for (const Table& table : tables) {
            for (const char letter : table.tag) {
                bytes.push_back(static_cast<std::byte>(letter));
            }
            Bytes padded = table.bytes;
            padToFour(padded);
            std::uint32_t checksum = 0;
            for (std::size_t word = 0; word < padded.size(); word += 4) {
                std::uint32_t value = 0;
                for (std::size_t index = 0; index < 4; ++index) {
                    value = (value << 8U) | std::to_integer<std::uint32_t>(padded[word + index]);
                }
                checksum += value;
            }
            put(bytes, checksum);
            put(bytes, static_cast<std::uint32_t>(offset + data.size()));
            put(bytes, static_cast<std::uint32_t>(table.bytes.size()));
            data.insert(data.end(), padded.begin(), padded.end());
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        return bytes;
    }
};

} // namespace nx::test
