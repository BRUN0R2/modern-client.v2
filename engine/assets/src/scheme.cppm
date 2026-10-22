// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// VGUI2's schemes (ADR-0020): the colors, the settings, the fonts and the borders a scheme
/// (resource/TrackerScheme.res, resource/ClientScheme.res) gives its windows, typed from its
/// KeyValues as the files write them. Which color a setting names, which font a screen's height
/// gets and how a border is drawn are the toolkit's (nx.ui); this reads what the file says.
export module nx.assets:scheme;

import std;
import nx.core;
import :keyValues;

export namespace nx::assets {

/// What a scheme says that cannot be read; the detail names the entry.
enum class SchemeError : std::uint8_t {
    NotAScheme, ///< The text has no `Scheme` block.
    BadColor,   ///< A color that is not three or four whole numbers from 0 to 255.
    BadFont,    ///< A font's tall, weight, yres or range is not the numbers it must be.
    BadBorder,  ///< A border's inset or a line's offset is not the numbers it must be, or a line has no
                ///< color, or a border names one that does not exist.
};

/// A color of a scheme: red, green, blue and how opaque, 255 when the scheme leaves it out.
struct SchemeColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;

    [[nodiscard]] inline auto operator==(const SchemeColor&) const noexcept -> bool = default;
};

/// The color `text` writes: three or four whole numbers from 0 to 255 ("216 222 211 255"); none for
/// other text, such as the name of a color.
[[nodiscard]] auto schemeColorOf(std::string_view text) noexcept -> std::optional<SchemeColor>;

/// A named color of the scheme's `Colors`.
struct NamedColor {
    std::string name{};
    SchemeColor color{};
};

/// A setting of the scheme's `BaseSettings`: the name a control asks for, those of its blocks joined
/// to it by a dot ("Menu.TextInset"), and its text: the name of a color, a color, a picture, a
/// number.
struct SchemeSetting {
    std::string name{};
    std::string value{};
};

/// What can be asked of a font's letters, as VGUI2 asks it of the system.
enum class FontFlag : std::uint8_t {
    Antialias,
    Underline,
    Italic,
    DropShadow,
    Outline,
    Symbol, ///< Its letters are symbols (Marlett): no other face stands in for it.
    Additive,
    Rotary,
    Count,
};

/// A range, inclusive at both ends.
template <class Value>
struct SchemeRange {
    Value first{};
    Value last{};

    [[nodiscard]] inline auto contains(Value value) const noexcept -> bool {
        return value >= first && value <= last;
    }
    [[nodiscard]] inline auto operator==(const SchemeRange&) const noexcept -> bool = default;
};

/// One way of drawing a font, in the order the scheme lists them.
struct FontCandidate {
    std::string face{};      ///< `name`: "Tahoma".
    std::int32_t tall = 0;   ///< Its height in pixels.
    std::int32_t weight = 0; ///< Its boldness, as Windows weighs it (0 to 1000).
    std::int32_t blur = 0;
    std::int32_t scanLines = 0;
    Flags<FontFlag> flags{};
    std::optional<SchemeRange<std::uint32_t>> heights{}; ///< `yres`: the screens' heights it is for.
    std::optional<SchemeRange<char32_t>> characters{};   ///< `range`: the characters it draws.
};

/// A named font of the scheme's `Fonts`, and its candidates.
struct SchemeFont {
    std::string name{};
    std::vector<FontCandidate> candidates{};
};

/// The sides of a border.
enum class BorderSide : std::uint8_t {
    Left,
    Right,
    Top,
    Bottom,
    Count,
};

/// A line of a side of a border, from outside in: the color it is drawn in, by name, and how far
/// its two ends stop short of the side's.
struct BorderLine {
    std::string color{};
    std::int32_t startOffset = 0;
    std::int32_t endOffset = 0;
};

/// A named border of the scheme's `Borders`: another's name, or its own inset and lines.
struct SchemeBorder {
    std::string name{};
    std::string sameAs{};                ///< The border it is, by name; empty for its own.
    std::array<std::int32_t, 4> inset{}; ///< Left, top, right and bottom.
    EnumArray<BorderSide, std::vector<BorderLine>> sides{};
};

/// A VGUI2 scheme: what its `Colors`, `BaseSettings`, `Fonts`, `Borders` and `CustomFontFiles` say,
/// each in the file's order.
struct Scheme {
    std::vector<NamedColor> colors{};
    std::vector<SchemeSetting> settings{};
    std::vector<SchemeFont> fonts{};
    std::vector<SchemeBorder> borders{};
    std::vector<std::string> customFontFiles{}; ///< Fonts the scheme brings, by their paths.

    /// The color `name` gives, as VGUI2 looks a control's color up: the setting of that name, else
    /// the name itself; that text a color, or the name of one of `colors`. None when it is neither.
    [[nodiscard]] auto color(std::string_view name) const -> std::optional<SchemeColor>;
    /// The text of the setting `name` ("Menu.TextInset"); none when there is no such setting.
    [[nodiscard]] auto setting(std::string_view name) const noexcept -> std::optional<std::string_view>;
    /// The font `name`; null when there is none.
    [[nodiscard]] auto font(std::string_view name) const noexcept -> const SchemeFont*;
    /// The border `name`, the one it is the same as followed to one with lines of its own; null when
    /// there is none.
    [[nodiscard]] auto border(std::string_view name) const noexcept -> const SchemeBorder*;
};

/// The scheme of `entries`, the KeyValues of a scheme file: its `Scheme` block. Names are compared in
/// any case, as VGUI2 compares them; keys it does not know are left out, as VGUI2 leaves them.
/// Errors: NotAScheme, BadColor, BadFont, BadBorder.
[[nodiscard]] auto readScheme(std::span<const KeyValuesEntry> entries) -> Result<Scheme, SchemeError>;

} // namespace nx::assets
