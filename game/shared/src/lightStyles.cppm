// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The light styles the server sets as a map starts (docs/05-rendering.md, section 8), sent as
/// patterns in PlayerSnapshot; the client alone evaluates their brightness at the frame's time.
export module cs.shared:lightStyles;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.assets;

export namespace cs {

/// The server's light-style patterns, independent of rendering; an empty pattern leaves a style unset.
struct MapLightStyles {
    std::array<std::string, nx::assets::kLightStyleCount> patterns{};

    [[nodiscard]] inline auto operator==(const MapLightStyles&) const noexcept -> bool = default;
};

/// A light style the game sets as a map starts: its number, below nx::assets::kLightStyleCount, and
/// the pattern it plays.
struct WorldLightStyle {
    std::uint32_t style{};
    std::string pattern{}; ///< Letters from 'a' (dark) through 'm' (normal) to 'z', ten a second.
};

/// data/cstrike/lightStyles.toml: the light styles the game sets as a map starts.
struct WorldLightStyles {
    std::vector<WorldLightStyle> styles{};
};

/// Why the light styles could not be read.
enum class LightStyleError : std::uint8_t {
    DataInvalid, ///< The text is not of the file's schema (cause: the DataError).
    BadPattern,  ///< A pattern is empty, or has a character outside 'a' to 'z'.
    StyleTwice,  ///< A style is given twice.
};

/// Reads `text`, the file `sourceName`: each style once, its pattern of letters from 'a' to 'z'.
/// Errors: DataInvalid, BadPattern, StyleTwice.
[[nodiscard]] auto readWorldLightStyles(std::string_view text, std::string_view sourceName)
    -> nx::Result<WorldLightStyles, LightStyleError>;

/// The light styles the game sets for `map`: `world`'s, the patterns the game sets as a map starts;
/// then those of the switchable lights, from their entities.
[[nodiscard]] auto mapLightStyles(const nx::assets::BspView& map, const WorldLightStyles& world)
    -> MapLightStyles;

} // namespace cs

template <>
struct nx::meta::Description<cs::WorldLightStyle> {
    static constexpr std::string_view kName = "cs::WorldLightStyle";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WorldLightStyle::style>(
            "style", nx::data::Range{.minimum = 0.0,
                                     .maximum = static_cast<double>(nx::assets::kLightStyleCount - 1)}),
        nx::meta::field<&cs::WorldLightStyle::pattern>("pattern"),
    };
};

template <>
struct nx::meta::Description<cs::WorldLightStyles> {
    static constexpr std::string_view kName = "cs::WorldLightStyles";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WorldLightStyles::styles>("styles"),
    };
};
