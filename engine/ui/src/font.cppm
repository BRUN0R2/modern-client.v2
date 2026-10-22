// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Fonts (ADR-0020): the TrueType faces VGUI2's schemes name (Tahoma, Verdana...), sized and drawn
/// as Windows sized and drew them for the original. A scheme asks for a font by its cell height
/// (`tall`), the height of a line; Windows turns that into a whole number of pixels per em by the
/// face's tables, which `fontMetricsFor` reproduces: measured on Windows' GDI for every height from 4
/// to 120 in the faces the schemes name, regular and bold, and in the OpenType specification's VDMX
/// (docs/12-ui-hud-and-console.md, section 2.1). FreeType draws the glyphs, hinted by the faces' own
/// instructions as Windows hints them, with or without antialiasing.
export module nx.ui:font;

import std;
import nx.core;

export namespace nx::ui {

/// What a font file or the use of a face cannot do.
enum class FontError : std::uint8_t {
    NotAFont,   ///< The bytes are no font FreeType reads, or have no such face; the detail says why.
    NotOutline, ///< The face is made of bitmaps (a .fon), which this reader does not size yet.
    BadHeight,  ///< The cell height is not a whole number of pixels from 1 to kMaxCellHeight.
    NoGlyph,    ///< The face has no glyph for the character.
    BadGlyph,   ///< The face's glyph cannot be loaded or drawn; the detail says why.
    Unreadable, ///< The font file cannot be read (cause: the file system's).
};

/// The tallest cell a font is sized to, in pixels: past it, a glyph's bitmap outgrows any atlas.
inline constexpr std::int32_t kMaxCellHeight = 1024;
/// How many ems a glyph may stretch across, each way: more is no letter, but a font made to exhaust
/// the memory or the time of whoever draws it.
inline constexpr std::int32_t kMaxGlyphEms = 8;

/// How a face draws the edges of its glyphs (VGUI2's `antialias` flag).
enum class FontSmoothing : std::uint8_t {
    None,        ///< Each pixel in or out, as Windows draws without antialiasing.
    Antialiased, ///< Each pixel as much as the glyph covers it, at the sizes the face's gasp table smooths.
    Count,
};

/// The heights Windows sizes a face by, in the face's units (its OS/2 table, else its hhea).
struct FontVerticalMetrics {
    std::int32_t unitsPerEm = 0;
    std::int32_t ascent = 0;  ///< Above the baseline: usWinAscent.
    std::int32_t descent = 0; ///< Below the baseline, as a distance: usWinDescent.
};

/// The highest and lowest pixel of a face's glyphs at a size, as its VDMX table records them.
struct FontDeviceHeight {
    std::int32_t pixelsPerEm = 0;
    std::int32_t top = 0;    ///< The highest row above the baseline (yMax).
    std::int32_t bottom = 0; ///< The lowest row, negative below the baseline (yMin).

    [[nodiscard]] inline auto operator==(const FontDeviceHeight&) const noexcept -> bool = default;
};

/// A face at a size, in pixels: the em it is scaled to and its line, as Windows reports them
/// (TEXTMETRIC's tmAscent and tmDescent; their sum is tmHeight).
struct FontMetrics {
    std::int32_t pixelsPerEm = 0;
    std::int32_t ascent = 0;  ///< From the top of the line to the baseline.
    std::int32_t descent = 0; ///< From the baseline to the bottom of the line.

    /// The height of a line: at most the cell height asked for, sometimes less.
    [[nodiscard]] inline auto height() const noexcept -> std::int32_t { return ascent + descent; }
    [[nodiscard]] inline auto operator==(const FontMetrics&) const noexcept -> bool = default;
};

/// The device heights Windows sizes a face by, read from its VDMX table: the records of the group for
/// a square pixel, when Windows takes them (a version 1 table, or a version 0 table made for the
/// ANSI characters), in the order of their ems; none otherwise, or when the table is cut short.
[[nodiscard]] auto readDeviceHeights(std::span<const std::byte> table) -> std::vector<FontDeviceHeight>;

/// The size Windows gives a face with `vertical` metrics (its line at least half its em) and
/// `deviceHeights` (in the order of their ems) when a font is asked for by its cell height
/// `cellHeight` (from 1 to kMaxCellHeight). Of the device heights, when they reach past the cell:
/// the em of the first whose line is exactly the cell, else the largest em whose line fits. Without
/// one that fits, the same of every em, each by its device height where it has one, else by its
/// line scaled and rounded; a scaled line is split by the face's ascent and descent at the scale of
/// the cell.
[[nodiscard]] auto fontMetricsFor(const FontVerticalMetrics& vertical,
                                  std::span<const FontDeviceHeight> deviceHeights, std::int32_t cellHeight)
    -> FontMetrics;

/// A glyph drawn at a size: a bitmap of how much of each pixel it covers, placed from the pen.
struct Glyph {
    std::int32_t left = 0;                ///< From the pen to the bitmap's left edge, in pixels.
    std::int32_t top = 0;                 ///< From the baseline up to the bitmap's top edge.
    std::int32_t width = 0;               ///< Columns of the bitmap.
    std::int32_t height = 0;              ///< Rows of the bitmap.
    std::int32_t advance = 0;             ///< How far the pen moves past the glyph.
    std::vector<std::uint8_t> coverage{}; ///< Row by row from the top: 0 for none, 255 for all of a pixel.
};

/// What a font is asked to be, as a scheme asks for it.
struct FontRequest {
    std::int32_t cellHeight = 0; ///< VGUI2's `tall`: the height of a line, in pixels.
    FontSmoothing smoothing = FontSmoothing::None;
    std::uint32_t faceIndex = 0; ///< The face of a collection (.ttc); 0 for a single face.
    /// Bold simulated, as Windows simulates it: each glyph struck twice, the second a pixel to the
    /// right, and a pixel wider.
    bool isEmboldened = false;
};

/// A face of a font file at a size, drawing its glyphs. Each font owns its own FreeType instance, so
/// fonts may be used on different threads, one thread at a time each.
class Font {
public:
    /// The face of `file`, a TrueType or OpenType font, that `request` asks for, sized as Windows
    /// sizes it. Errors: NotAFont, NotOutline, BadHeight.
    [[nodiscard]] static auto open(std::shared_ptr<const std::vector<std::byte>> file,
                                   const FontRequest& request) -> Result<Font, FontError>;

    Font(Font&& other) noexcept;
    auto operator=(Font&& other) noexcept -> Font&;
    Font(const Font&) = delete;
    auto operator=(const Font&) -> Font& = delete;
    ~Font();

    /// The family the face belongs to, as its file names it ("Tahoma").
    [[nodiscard]] auto family() const noexcept -> std::string_view;
    /// The face's style in its family ("Regular", "Bold").
    [[nodiscard]] auto style() const noexcept -> std::string_view;
    [[nodiscard]] inline auto metrics() const noexcept -> const FontMetrics& { return m_metrics; }
    [[nodiscard]] inline auto smoothing() const noexcept -> FontSmoothing { return m_smoothing; }

    /// The glyph of `character`, hinted and drawn as the font's smoothing says. Asked to smooth, a font
    /// does as Windows does, as its gasp table says at its size: smoothed or not, fitted to the pixels
    /// or not (Tahoma from 9 to 16 pixels per em: not smoothed). Errors: NoGlyph, BadGlyph (also a
    /// glyph wider or taller than kMaxGlyphEms ems).
    [[nodiscard]] auto glyph(char32_t character) const -> Result<Glyph, FontError>;

private:
    struct Face; // FreeType's instance and the face it opened

    Font(std::unique_ptr<Face> face, FontMetrics metrics, FontSmoothing smoothing) noexcept;

    std::unique_ptr<Face> m_face;
    FontMetrics m_metrics;
    FontSmoothing m_smoothing;
};

} // namespace nx::ui
