// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The light the game sets for a map, as the client plays the game until the server spawns its entities:
/// the patterns of its light styles, and the light of its sky (docs/05-rendering.md, sections 8 and
/// 9.2).
export module cs.client:mapLight;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;
import nx.assets;
import nx.render;

export namespace cs {

/// A light style the game sets as a map starts: its number, below nx::render::kLightStyleCount, and
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
    -> nx::render::LightStyles;

/// The light of `map`'s sky, as the game sets it from its `light_environment` (the original's
/// sv_skycolor and sv_skyvec), as ReGameDLL's CEnvLight sets it (GPL, consulted: docs/20 section 3):
/// its `_light`, one gray level, "r g b" or "r g b brightness", the compilers' light, made the engine's
/// (pow(level / 114, 0.6) * 264, each cut to a whole number); and the way it aims, its `angles` and its
/// `pitch` in the order the map gives them, `pitch` up for an aim. The last of them sets it; without
/// one, the sky has no light.
[[nodiscard]] auto skyLightOf(const nx::assets::BspView& map) -> nx::render::SkyLight;

/// The lights of `map`'s `light` and `light_spot` entities, which give models highlights in the enhanced
/// look (docs/adr/0025-enhanced-rendering.md): each where its origin is, its `_light` "r g b brightness"
/// in linear light (300 white without one, as the compilers default it), reaching farther the
/// brighter it is.
[[nodiscard]] auto highlightLightsOf(const nx::assets::BspView& map) -> std::vector<nx::render::StudioLight>;

/// Where `map`'s reflection probes go (nx.render:probes), the first the first baked: at the eye of
/// each spawn, the middle of each bomb site, buy zone and hostage rescue zone, then the middle of
/// its empty leaves, each at least kProbeSpacing from those before it; nx::render::kMostProbes at most.
[[nodiscard]] auto probePositionsOf(const nx::assets::BspView& map) -> std::vector<nx::Vec3>;

/// The lights of `lights` that reach `point`, the nearest first, the first `count` of them.
[[nodiscard]] auto nearestLights(std::span<const nx::render::StudioLight> lights, nx::Vec3 point,
                                 std::size_t count) -> std::vector<nx::render::StudioLight>;

} // namespace cs

template <>
struct nx::meta::Description<cs::WorldLightStyle> {
    static constexpr std::string_view kName = "cs::WorldLightStyle";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WorldLightStyle::style>(
            "style", nx::data::Range{.minimum = 0.0,
                                     .maximum = static_cast<double>(nx::render::kLightStyleCount - 1)}),
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
