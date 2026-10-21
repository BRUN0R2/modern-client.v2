// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The original's picture (docs/05-rendering.md, section 11): `texgamma` brightens the palettes of
/// the textures when they load; the light of a lightmap sample, the sum of its lightstyles, goes
/// through `lightgamma`, `brightness` and `gamma`; the drawn color is the texture's times its light,
/// no brighter. Each curve is applied where the original applies it, so the picture is its own down
/// to the rounding: a texture's palette is mapped once, and the light through a table of 1024
/// entries the shader reads.
export module nx.render:gamma;

import std;
import nx.core;

export namespace nx::render {

/// The original's picture settings, each held to the range the original allows.
struct GammaSettings {
    float gamma = 2.5F;        ///< `gamma`, 1.8 to 3.
    float brightness = 1.0F;   ///< `brightness`, 0 to 3; CS 1.6 ships 1 in config.cfg.
    float textureGamma = 2.0F; ///< `texgamma`, 1.8 to 3.
    float lightGamma = 2.5F;   ///< `lightgamma`, 1.8 to 3.
};

/// The entries of the light table: the summed light of a sample, from 0 to 1023.
inline constexpr std::size_t kLightTableSize = 1024;

/// The values of a color channel of a palette, 0 to 255, and what each becomes.
inline constexpr std::size_t kChannelValues = 256;
using ChannelTable = std::array<std::uint8_t, kChannelValues>;

/// `texgamma`: what each palette value of a texture becomes.
[[nodiscard]] auto textureGammaTable(const GammaSettings& settings) -> ChannelTable;

/// `lightgamma`, `brightness` and `gamma`: the light, from 0 to 1023, of each summed light from 0 to
/// 1023, the original's lightgammatable. Studio models read it whole (R_StudioLighting).
[[nodiscard]] auto lightGammaTable(const GammaSettings& settings)
    -> std::array<std::uint16_t, kLightTableSize>;

/// The light, from 0 to 255, of each summed light from 0 to 1023: the upper 8 bits of
/// lightGammaTable, which the world's lightmaps draw.
[[nodiscard]] auto lightTable(const GammaSettings& settings) -> std::array<std::uint8_t, kLightTableSize>;

/// The bits the original shifts out of a summed light times lightScale, to index lightTable.
inline constexpr std::uint32_t kLightScaleShift = 14;

/// The original's `lightscale`, 2^(1/lightgamma) in 8.8 fixed point: a luxel's light, the sum over its
/// lightmaps of each sample (0 to 255) times its style's brightness, times this and shifted right by
/// kLightScaleShift, is its index in lightTable, held to 1023.
[[nodiscard]] auto lightScale(const GammaSettings& settings) -> std::uint32_t;

} // namespace nx::render
