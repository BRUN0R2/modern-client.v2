// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// FreeType stays in this unit (docs/02-coding-standards.md, section 2): the interface never shows
// its types.
#include <freetype/freetype.h>
#include <freetype/ftdriver.h>
#include <freetype/ftmodapi.h>
#include <freetype/ftoutln.h>
#include <freetype/tttables.h>
#include <freetype/tttags.h>
#include <ft2build.h>

module nx.ui;

import std;
import nx.core;

namespace nx::ui {

namespace {

/// One in Windows' fixed point: the scale from a face's units to pixels is a whole number of these.
constexpr std::int64_t kFixedOne = 65536;

/// Whether a line `line` tall can be one of a face of an em `em` big: a face's line is at least half its
/// em. A table that breaks it is broken, or made to size a face beyond any cell and so exhaust the
/// memory and the time of whoever draws its glyphs.
[[nodiscard]] constexpr auto isLineOfItsEm(std::int64_t line, std::int64_t em) noexcept -> bool {
    return 2 * line >= em;
}

/// `numerator / denominator`, both positive, rounded to the nearest whole number, halves up.
[[nodiscard]] constexpr auto roundedQuotient(std::int64_t numerator, std::int64_t denominator) noexcept
    -> std::int64_t {
    return ((2 * numerator) + denominator) / (2 * denominator);
}

/// The height of a line of a face scaled to `pixelsPerEm`: its ascent and descent, scaled together and
/// rounded.
[[nodiscard]] auto scaledLineHeight(const FontVerticalMetrics& vertical, std::int32_t pixelsPerEm) noexcept
    -> std::int64_t {
    return roundedQuotient(std::int64_t{pixelsPerEm} * (vertical.ascent + vertical.descent),
                           vertical.unitsPerEm);
}

/// Whether a VDMX ratio record (x, y from `yStart` to `yEnd`) takes in a square pixel: x:x is in its
/// range once the record is normalized to the device's x, or the record is the 0:0 one for all.
[[nodiscard]] constexpr auto takesSquarePixels(std::uint8_t x, std::uint8_t yStart,
                                               std::uint8_t yEnd) noexcept -> bool {
    return x == 0 ? yStart == 0 && yEnd == 0 : yStart <= x && x <= yEnd;
}

/// The bytes of `table` from `offset`, `size` of them; empty when the table is shorter.
[[nodiscard]] auto bytesAt(std::span<const std::byte> table, std::size_t offset, std::size_t size) noexcept
    -> std::span<const std::byte> {
    return offset <= table.size() && size <= table.size() - offset ? table.subspan(offset, size)
                                                                   : std::span<const std::byte>{};
}

/// The flags of a range of a gasp table (the OpenType specification): fit the glyphs to the pixels,
/// and smooth their edges.
constexpr std::uint16_t kGaspGridFit = 0x0001;
constexpr std::uint16_t kGaspDoGray = 0x0002;

/// How a face's gasp table asks for its glyphs to be drawn at `pixelsPerEm`: the flags of the first
/// range that reaches that size; fitted and smoothed when it has no table, or none that reaches it.
[[nodiscard]] auto gaspFlagsAt(std::span<const std::byte> table, std::int32_t pixelsPerEm) noexcept
    -> std::uint16_t {
    constexpr std::size_t kHeaderSize = 4; // version, numRanges
    constexpr std::size_t kRangeSize = 4;  // rangeMaxPPEM, rangeGaspBehavior
    constexpr std::size_t kRangeCountAt = 2;
    constexpr std::size_t kBehaviorAt = 2;
    if (table.size() < kHeaderSize) {
        return kGaspGridFit | kGaspDoGray;
    }
    const std::size_t count =
        std::min<std::size_t>(loadBigEndian<std::uint16_t>(table.subspan(kRangeCountAt)),
                              (table.size() - kHeaderSize) / kRangeSize);
    for (std::size_t index = 0; index < count; ++index) {
        const std::span<const std::byte> range =
            table.subspan(kHeaderSize + (index * kRangeSize), kRangeSize);
        if (pixelsPerEm <= loadBigEndian<std::uint16_t>(range)) {
            return loadBigEndian<std::uint16_t>(range.subspan(kBehaviorAt));
        }
    }
    return kGaspGridFit | kGaspDoGray;
}

/// FreeType's subdivision of a pixel (26.6 fixed point).
constexpr FT_Pos kSubpixels = 64;

/// The coverage of pixel `column` of `row`, a row of a bit a pixel, the leftmost the highest bit of
/// its byte: all or nothing.
[[nodiscard]] auto monoCoverage(std::span<const unsigned char> row, std::size_t column) noexcept
    -> std::uint8_t {
    constexpr std::uint8_t kFull = 255;
    const std::size_t shift = kBitsPerByte - 1 - (column % kBitsPerByte);
    return ((row[column / kBitsPerByte] >> shift) & 1U) != 0 ? kFull : std::uint8_t{0};
}

/// `glyph` struck twice, the second a pixel to the right, as Windows simulates bold: a pixel wider, each
/// pixel as covered as the more covered of itself and the one left of it.
[[nodiscard]] auto struckTwice(const Glyph& glyph) -> Glyph {
    Glyph wider = glyph;
    if (glyph.width == 0) {
        return wider;
    }
    const auto width = narrow<std::size_t>(glyph.width);
    wider.width = glyph.width + 1;
    wider.coverage.assign((width + 1) * narrow<std::size_t>(glyph.height), 0);
    for (std::size_t row = 0; row < narrow<std::size_t>(glyph.height); ++row) {
        for (std::size_t column = 0; column <= width; ++column) {
            const std::uint8_t self =
                column < width ? glyph.coverage[(row * width) + column] : std::uint8_t{0};
            const std::uint8_t left =
                column > 0 ? glyph.coverage[(row * width) + column - 1] : std::uint8_t{0};
            wider.coverage[(row * (width + 1)) + column] = std::max(self, left);
        }
    }
    return wider;
}

/// FreeType's error `error`, as a failure's detail.
[[nodiscard]] auto freeTypeError(std::string_view what, FT_Error error) -> std::string {
    return std::format("{}: FreeType error {:#04x}", what, error);
}

} // namespace

auto readDeviceHeights(std::span<const std::byte> table) -> std::vector<FontDeviceHeight> {
    // The header (version, numRecs, numRatios), the ratio records (bCharSet, xRatio, yStartRatio,
    // yEndRatio), then an offset to the group of each; a group's records are yPelHeight, yMax, yMin.
    constexpr std::size_t kHeaderSize = 6;
    constexpr std::size_t kRatioCountAt = 4;
    constexpr std::size_t kHorizontalRatioAt = 1;
    constexpr std::size_t kFirstVerticalRatioAt = 2;
    constexpr std::size_t kLastVerticalRatioAt = 3;
    constexpr std::size_t kTopAt = 2;
    constexpr std::size_t kBottomAt = 4;
    constexpr std::size_t kRatioSize = 4;
    constexpr std::size_t kOffsetSize = 2;
    constexpr std::size_t kGroupHeaderSize = 4;
    constexpr std::size_t kRecordSize = 6;
    constexpr std::uint8_t kAnsiCharacters = 1; // the only group a version 0 table offers Windows
    const std::span<const std::byte> header = bytesAt(table, 0, kHeaderSize);
    if (header.empty()) {
        return {};
    }
    const auto version = loadBigEndian<std::uint16_t>(header);
    const auto ratioCount = loadBigEndian<std::uint16_t>(header.subspan(kRatioCountAt));
    for (std::size_t ratio = 0; ratio < ratioCount; ++ratio) {
        const std::span<const std::byte> record =
            bytesAt(table, kHeaderSize + (ratio * kRatioSize), kRatioSize);
        const std::span<const std::byte> offset =
            bytesAt(table, kHeaderSize + (ratioCount * kRatioSize) + (ratio * kOffsetSize), kOffsetSize);
        if (record.empty() || offset.empty()) {
            return {};
        }
        const auto characters = loadBigEndian<std::uint8_t>(record);
        if (version == 0 && characters != kAnsiCharacters) {
            continue; // Windows ignores it
        }
        if (!takesSquarePixels(loadBigEndian<std::uint8_t>(record.subspan(kHorizontalRatioAt)),
                               loadBigEndian<std::uint8_t>(record.subspan(kFirstVerticalRatioAt)),
                               loadBigEndian<std::uint8_t>(record.subspan(kLastVerticalRatioAt)))) {
            continue;
        }
        const std::size_t groupOffset = loadBigEndian<std::uint16_t>(offset);
        const std::span<const std::byte> group = bytesAt(table, groupOffset, kGroupHeaderSize);
        if (group.empty()) {
            return {};
        }
        const std::size_t count = loadBigEndian<std::uint16_t>(group);
        const std::span<const std::byte> records =
            bytesAt(table, groupOffset + kGroupHeaderSize, count * kRecordSize);
        if (records.size() != count * kRecordSize) {
            return {};
        }
        std::vector<FontDeviceHeight> heights;
        heights.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const std::span<const std::byte> entry = records.subspan(index * kRecordSize, kRecordSize);
            const FontDeviceHeight height{.pixelsPerEm = loadBigEndian<std::uint16_t>(entry),
                                          .top = loadBigEndian<std::int16_t>(entry.subspan(kTopAt)),
                                          .bottom = loadBigEndian<std::int16_t>(entry.subspan(kBottomAt))};
            if (isLineOfItsEm(height.top - height.bottom, height.pixelsPerEm)) {
                heights.push_back(height); // else no height of the face
            }
        }
        std::ranges::stable_sort(heights, {},
                                 &FontDeviceHeight::pixelsPerEm); // sorted, as the table should be
        return heights;                                           // the first group that fits is the one
    }
    return {};
}

auto fontMetricsFor(const FontVerticalMetrics& vertical, std::span<const FontDeviceHeight> deviceHeights,
                    std::int32_t cellHeight) -> FontMetrics {
    expects(cellHeight >= 1 && cellHeight <= kMaxCellHeight, "fontMetricsFor: a cell height in [1, 1024]");
    expects(vertical.unitsPerEm > 0 && isLineOfItsEm(vertical.ascent + vertical.descent, vertical.unitsPerEm),
            "fontMetricsFor: a face whose line is at least half its em");
    expects(std::ranges::is_sorted(deviceHeights, {}, &FontDeviceHeight::pixelsPerEm),
            "fontMetricsFor: device heights in the order of their ems");
    const auto lineOf = [](const FontDeviceHeight& device) {
        return device.top - device.bottom;
    };
    const auto deviceAt = [&](std::int32_t pixelsPerEm) -> const FontDeviceHeight* {
        const auto found =
            std::ranges::lower_bound(deviceHeights, pixelsPerEm, {}, &FontDeviceHeight::pixelsPerEm);
        return found != deviceHeights.end() && found->pixelsPerEm == pixelsPerEm ? &*found : nullptr;
    };

    // The em to take among `pixelsPerEm` and those weighed before: the first whose line is exactly
    // the cell, else the largest whose line fits.
    std::int32_t chosen = 0;
    bool isExact = false;
    const auto weigh = [&](std::int32_t pixelsPerEm, std::int64_t line) {
        if (line == cellHeight && (!isExact || pixelsPerEm < chosen)) {
            chosen = pixelsPerEm;
            isExact = true;
        } else if (line < cellHeight && !isExact && pixelsPerEm > chosen) {
            chosen = pixelsPerEm;
        }
    };

    // The device heights first, when the table reaches past the cell.
    if (std::ranges::any_of(deviceHeights,
                            [&](const FontDeviceHeight& device) { return lineOf(device) > cellHeight; })) {
        for (const FontDeviceHeight& device : deviceHeights) {
            weigh(device.pixelsPerEm, lineOf(device));
        }
    }
    // Else every em, by its device height where it has one, else by its scaled line: those up to the
    // largest whose scaled line fits, and those of the device heights.
    if (chosen == 0) {
        std::int32_t scaledFit =
            std::max(1, narrow<std::int32_t>(std::int64_t{cellHeight} * vertical.unitsPerEm /
                                             (vertical.ascent + vertical.descent)));
        while (scaledFit > 1 && scaledLineHeight(vertical, scaledFit) > cellHeight) {
            --scaledFit;
        }
        while (scaledLineHeight(vertical, scaledFit + 1) <= cellHeight) {
            ++scaledFit;
        }
        const auto lineAt = [&](std::int32_t pixelsPerEm) {
            const FontDeviceHeight* device = deviceAt(pixelsPerEm);
            return device != nullptr ? std::int64_t{lineOf(*device)}
                                     : scaledLineHeight(vertical, pixelsPerEm);
        };
        for (std::int32_t pixelsPerEm = 1; pixelsPerEm <= scaledFit; ++pixelsPerEm) {
            weigh(pixelsPerEm, lineAt(pixelsPerEm));
        }
        for (const FontDeviceHeight& device : deviceHeights) {
            weigh(device.pixelsPerEm, lineOf(device));
        }
    }
    chosen = std::max(chosen, 1); // even one pixel per em is taller than the cell: the smallest em

    if (const FontDeviceHeight* device = deviceAt(chosen); device != nullptr) {
        return {.pixelsPerEm = chosen, .ascent = device->top, .descent = -device->bottom};
    }
    // A scaled line is split as the cell asked for is: the face's ascent and descent at the scale of
    // the cell, a whole number of Windows' fixed-point steps.
    const std::int64_t scale = std::int64_t{cellHeight} * kFixedOne / (vertical.ascent + vertical.descent);
    return {.pixelsPerEm = chosen,
            .ascent = narrow<std::int32_t>(roundedQuotient(vertical.ascent * scale, kFixedOne)),
            .descent = narrow<std::int32_t>(roundedQuotient(vertical.descent * scale, kFixedOne))};
}

struct Font::Face {
    std::shared_ptr<const std::vector<std::byte>> file; // FreeType reads the face from it (Font::open)
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    bool isSmoothed = false;   ///< Its glyphs antialiased, at its size.
    bool isFitted = true;      ///< Its glyphs fitted to the pixels by its instructions, at its size.
    bool isEmboldened = false; ///< Its glyphs struck twice, a pixel apart.

    /// The table `tag` of the face; empty when it has none.
    [[nodiscard]] auto table(FT_ULong tag) const -> std::vector<FT_Byte> {
        std::vector<FT_Byte> bytes;
        if (FT_ULong length = 0; FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) == 0) {
            bytes.resize(length);
            if (FT_Load_Sfnt_Table(face, tag, 0, bytes.data(), &length) != 0) {
                bytes.clear();
            }
        }
        return bytes;
    }

    Face() = default;
    Face(const Face&) = delete;
    auto operator=(const Face&) -> Face& = delete;
    Face(Face&&) = delete;
    auto operator=(Face&&) -> Face& = delete;
    ~Face() {
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }
};

auto Font::open(std::shared_ptr<const std::vector<std::byte>> file, const FontRequest& request)
    -> Result<Font, FontError> {
    expects(file != nullptr, "Font::open: a font file");
    if (request.cellHeight < 1 || request.cellHeight > kMaxCellHeight) {
        return fail(FontError::BadHeight, std::format("{} pixels", request.cellHeight));
    }
    auto face = std::make_unique<Face>();
    face->file = std::move(file);
    if (const FT_Error error = FT_Init_FreeType(&face->library); error != 0) {
        return fail(FontError::NotAFont, freeTypeError("starting", error));
    }
    // Windows hints by the faces' instructions on both axes, as FreeType's version 35 interpreter
    // does; the later ones leave the horizontal axis alone.
    FT_UInt interpreter = TT_INTERPRETER_VERSION_35;
    if (const FT_Error error =
            FT_Property_Set(face->library, "truetype", "interpreter-version", &interpreter);
        error != 0) {
        return fail(FontError::NotAFont, freeTypeError("choosing the TrueType interpreter", error));
    }
    if (const FT_Error error = FT_New_Memory_Face(
            face->library, reinterpret_cast<const FT_Byte*>(face->file->data()), // NOLINT: FreeType's bytes
            narrow<FT_Long>(face->file->size()), narrow<FT_Long>(request.faceIndex), &face->face);
        error != 0) {
        return fail(FontError::NotAFont, freeTypeError(std::format("face {}", request.faceIndex), error));
    }
    if ((face->face->face_flags & FT_FACE_FLAG_SCALABLE) == 0) {
        return fail(FontError::NotOutline, std::format("{:?}", face->face->family_name != nullptr
                                                                   ? std::string_view{face->face->family_name}
                                                                   : std::string_view{}));
    }

    FontVerticalMetrics vertical{.unitsPerEm = face->face->units_per_EM,
                                 .ascent = face->face->ascender,
                                 .descent = -face->face->descender};
    // FreeType's version of an OS/2 table a font lacks (an Apple font's).
    constexpr FT_UShort kMissingTableVersion = 0xFFFF;
    const auto* windows = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face->face, FT_SFNT_OS2));
    if (windows != nullptr && windows->version != kMissingTableVersion &&
        windows->usWinAscent + windows->usWinDescent > 0) {
        vertical.ascent = windows->usWinAscent;
        vertical.descent = windows->usWinDescent;
    }
    if (vertical.unitsPerEm <= 0 || !isLineOfItsEm(vertical.ascent + vertical.descent, vertical.unitsPerEm)) {
        return fail(FontError::NotAFont,
                    std::format("a line of {} units for an em of {}", vertical.ascent + vertical.descent,
                                vertical.unitsPerEm));
    }
    const std::vector<FT_Byte> deviceHeights = face->table(TTAG_VDMX);
    const FontMetrics metrics = fontMetricsFor(
        vertical, readDeviceHeights(std::as_bytes(std::span{deviceHeights})), request.cellHeight);
    if (const FT_Error error = FT_Set_Pixel_Sizes(face->face, 0, narrow<FT_UInt>(metrics.pixelsPerEm));
        error != 0) {
        return fail(FontError::NotAFont,
                    freeTypeError(std::format("{} pixels per em", metrics.pixelsPerEm), error));
    }
    // Asked to smooth its glyphs, Windows does as the face's gasp table says at its size: Tahoma has
    // them neither smoothed nor unfitted from 9 to 16 pixels per em. Without smoothing, it fits them.
    face->isEmboldened = request.isEmboldened;
    if (request.smoothing == FontSmoothing::Antialiased) {
        const std::vector<FT_Byte> gasp = face->table(TTAG_gasp);
        const std::uint16_t flags = gaspFlagsAt(std::as_bytes(std::span{gasp}), metrics.pixelsPerEm);
        face->isSmoothed = (flags & kGaspDoGray) != 0;
        face->isFitted = (flags & kGaspGridFit) != 0;
    }
    return Font{std::move(face), metrics, request.smoothing};
}

Font::Font(std::unique_ptr<Face> face, FontMetrics metrics, FontSmoothing smoothing) noexcept
    : m_face{std::move(face)}, m_metrics{metrics}, m_smoothing{smoothing} {}

Font::Font(Font&& other) noexcept = default;

auto Font::operator=(Font&& other) noexcept -> Font& = default;

Font::~Font() = default;

auto Font::family() const noexcept -> std::string_view {
    return m_face->face->family_name != nullptr ? std::string_view{m_face->face->family_name}
                                                : std::string_view{};
}

auto Font::style() const noexcept -> std::string_view {
    return m_face->face->style_name != nullptr ? std::string_view{m_face->face->style_name}
                                               : std::string_view{};
}

auto Font::glyph(char32_t character) const -> Result<Glyph, FontError> {
    const FT_UInt index = FT_Get_Char_Index(m_face->face, character);
    if (index == 0) {
        return fail(FontError::NoGlyph, std::format("U+{:04X}", std::uint32_t{character}));
    }
    // The faces' own hinting, never FreeType's automatic one: Windows has none.
    const bool isSmooth = m_face->isSmoothed;
    const auto load =
        static_cast<FT_Int32>(FT_LOAD_NO_AUTOHINT | (m_face->isFitted ? 0 : FT_LOAD_NO_HINTING) |
                              (isSmooth ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO));
    if (const FT_Error error = FT_Load_Glyph(m_face->face, index, load); error != 0) {
        return fail(FontError::BadGlyph,
                    freeTypeError(std::format("loading U+{:04X}", std::uint32_t{character}), error));
    }
    const FT_GlyphSlot slot = m_face->face->glyph;
    // Hinted, the advance is a whole number of pixels; a pixel more struck twice.
    const auto advance = narrow<std::int32_t>((slot->advance.x + (kSubpixels / 2)) / kSubpixels) +
                         (m_face->isEmboldened ? 1 : 0);
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE && slot->outline.n_points == 0) {
        return Glyph{.advance = advance}; // nothing to draw: a space
    }
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_BBox box{};
        FT_Outline_Get_CBox(&slot->outline, &box);
        const FT_Pos largest = FT_Pos{kMaxGlyphEms} * m_metrics.pixelsPerEm * kSubpixels;
        if (box.xMax - box.xMin > largest || box.yMax - box.yMin > largest) {
            return fail(FontError::BadGlyph,
                        std::format("U+{:04X}: {} by {} pixels at {} pixels per em", std::uint32_t{character},
                                    (box.xMax - box.xMin) / kSubpixels, (box.yMax - box.yMin) / kSubpixels,
                                    m_metrics.pixelsPerEm));
        }
    }
    if (const FT_Error error = FT_Render_Glyph(slot, isSmooth ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO);
        error != 0) {
        return fail(FontError::BadGlyph,
                    freeTypeError(std::format("drawing U+{:04X}", std::uint32_t{character}), error));
    }

    const FT_Bitmap& bitmap = slot->bitmap;
    Glyph glyph{.left = slot->bitmap_left,
                .top = slot->bitmap_top,
                .width = narrow<std::int32_t>(bitmap.width),
                .height = narrow<std::int32_t>(bitmap.rows),
                .advance = advance,
                .coverage = std::vector<std::uint8_t>(std::size_t{bitmap.width} * bitmap.rows)};
    const bool isMono = bitmap.pixel_mode == FT_PIXEL_MODE_MONO;
    if (!isMono && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
        return fail(FontError::BadGlyph,
                    std::format("U+{:04X}: pixel mode {}", std::uint32_t{character}, bitmap.pixel_mode));
    }
    const std::size_t stride = std::size_t{static_cast<std::uint32_t>(std::abs(bitmap.pitch))};
    for (std::size_t row = 0; row < bitmap.rows; ++row) {
        // A negative pitch stores the rows from the bottom up.
        const std::size_t stored = bitmap.pitch >= 0 ? row : bitmap.rows - 1 - row;
        const std::span<const unsigned char> bytes{bitmap.buffer + (stored * stride), stride};
        for (std::size_t column = 0; column < bitmap.width; ++column) {
            glyph.coverage[(row * bitmap.width) + column] =
                isMono ? monoCoverage(bytes, column) : bytes[column];
        }
    }
    return m_face->isEmboldened ? struckTwice(glyph) : glyph;
}

} // namespace nx::ui
