// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How the game asks an entity to be drawn (docs/05-rendering.md, sections 8 and 10): its
/// `rendermode`, `renderamt`, `rendercolor` and `renderfx`, numbered as the original numbers them.
export module nx.render:renderMode;

import std;

export namespace nx::render {

/// How an entity is drawn, its `rendermode` (R_SetRenderMode).
enum class RenderMode : std::uint8_t {
    Normal,       ///< Opaque: a brush entity lit as the world, a sprite as its texture is.
    TransColor,   ///< A brush entity's `rendercolor` instead of its textures, unlit; a sprite as
                  ///< TransTexture. As opaque as `renderamt` says.
    TransTexture, ///< Its textures, unlit, as opaque as `renderamt` says; a brush entity's faces back
                  ///< to front.
    Glow,         ///< A brush entity as TransTexture, its faces unsorted (any unknown mode too); a sprite
                  ///< added to what is behind, over everything, dimmer and larger the farther it is.
    TransAlpha,   ///< "Solid": a brush entity lit and opaque, its masked textures cut where their alpha
                  ///< is 0.25 or less; a sprite blended, its depth not written.
    TransAdd,     ///< Its textures times `renderamt`, unlit, added to what is behind.
    Count,
};

/// What an entity's `renderfx` does to how it is drawn. Today the renderer honors NoDissipation, which
/// keeps a glowing sprite as bright and as large at any distance; the others come with the effects of
/// entities (TODO.md).
enum class RenderEffect : std::uint8_t {
    None,
    PulseSlow,
    PulseFast,
    PulseSlowWide,
    PulseFastWide,
    FadeSlow,
    FadeFast,
    SolidSlow,
    SolidFast,
    StrobeSlow,
    StrobeFast,
    StrobeFaster,
    FlickerSlow,
    FlickerFast,
    NoDissipation,
    Distort,
    Hologram,
    DeadPlayer,
    Explode,
    GlowShell,
    ClampMinScale,
    Count,
};

/// An entity's `rendercolor`, each channel from 0 to 255.
struct RenderColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    [[nodiscard]] inline auto operator==(const RenderColor&) const noexcept -> bool = default;
};

} // namespace nx::render
