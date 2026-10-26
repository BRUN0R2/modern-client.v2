// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The light styles of a map (docs/05-rendering.md, section 8): each face blends up to four
/// lightmaps, each as bright as its style is in the frame. A style is a pattern of letters from 'a'
/// (dark) through 'm' (normal) to 'z', of which the original shows ten a second; the game sets them,
/// the world's when the map starts and the switchable lights' as they turn on and off.
export module nx.render:lightStyle;

import std;
import nx.core;
import nx.assets;

export namespace nx::render {

/// The styles the game sets: the faces' styles above them keep the normal brightness.
inline constexpr std::size_t kLightStyleCount = nx::assets::kLightStyleCount;

/// The brightness of a style at 'm', the normal one: what a lightmap sample is multiplied by.
inline constexpr std::uint16_t kNormalLightStyleValue = 264;

/// The brightness of every style in a frame, the original's d_lightstylevalue: 22 for each letter
/// after 'a' (264 at 'm'), and 256 for a style without a pattern.
using LightStyleValues = std::array<std::uint16_t, kLightStyleCount>;

/// The pattern each light style plays, as the game sets them, and how bright each is at a moment.
class LightStyles {
public:
    /// Sets the pattern of `style` (below kLightStyleCount); a character outside 'a' to 'z' is taken
    /// as the nearest of them. An empty pattern unsets the style.
    void set(std::size_t style, std::string_view pattern);

    /// Replaces all patterns with `patterns`, including unset styles, normalized as set() normalizes
    /// them. The caller keeps ownership of the source; later changes do not affect these patterns.
    void setPatterns(std::span<const std::string, kLightStyleCount> patterns);

    /// Every style's brightness `time` after the map started (not negative).
    [[nodiscard]] auto valuesAt(Seconds time) const noexcept -> LightStyleValues;

private:
    std::array<std::string, kLightStyleCount> m_patterns{};
};

} // namespace nx::render
