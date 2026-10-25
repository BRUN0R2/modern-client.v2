// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The light of the world where things are (docs/05-rendering.md, section 9.2): the original's
/// R_LightVec, the lightmap a segment meets first and its light there; and the light a studio model is
/// drawn with, its R_StudioDynamicLight: the light under the model, or the sky's when open sky is
/// above it, and the way it goes. They work as Xash3D-FWGS reconstructs them (GPL, consulted:
/// docs/20-legal-and-licensing.md, section 3), without its extensions (r_lighting_extended): the light
/// comes from the world's faces alone, never from brush entities' nor from the ceiling.
///
/// Today: without the dynamic lights (dlights, elights), which come with the effects.
export module nx.render:light;

import std;
import nx.core;
import nx.math;
import nx.assets;
import :lightStyle;
import :surface;

namespace nx::render {

/// What the light reads of a face of the world, by its index.
struct FaceLight {
    Vec4 s{}; ///< Its texture's axes: s = dot(point, s.xyz) + s.w.
    Vec4 t{};
    std::optional<assets::SurfaceExtent> extent{}; ///< None: no point is on it.
    bool isSpecial = false;                        ///< Sky, water and the like: the light passes it.
    bool isSky = false;
    bool hasSamples = false; ///< As many as its extent and lightmaps need, in the file.
};

} // namespace nx::render

export namespace nx::render {

/// A light of the world, the original's colorVec: each channel from 0 to 255.
struct LightColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    [[nodiscard]] inline auto operator==(const LightColor&) const noexcept -> bool = default;
};

/// The light of the sky, which the game sets from the map's `light_environment` (the original's
/// sv_skycolor and sv_skyvec): a model with open sky above it takes it instead of the light under it.
struct SkyLight {
    /// Each channel as bright as the original's light, where a lightmap sample of 256 at the normal
    /// style is 264; 0 in each: no sky light.
    Vec3 color{};
    Vec3 direction{.z = -1.0F}; ///< Where its light goes: unit length when it has a color.
};

/// The light a studio model is drawn with, the original's alight_t.
struct ModelLight {
    float ambient = 0.0F;       ///< What every vertex gets, from 0 to 128.
    float shade = 0.0F;         ///< What a vertex facing the light gets on top: ambient + shade <= 255.
    Vec3 direction{.z = -1.0F}; ///< Where the light goes, unit length.
    Vec3 color{.x = 1.0F, .y = 1.0F, .z = 1.0F}; ///< Its color, its brightest channel 1.
};

/// The original's `direct`: the share of a model's light that comes from its direction, held from
/// kLeastDirectLight to 1.
inline constexpr float kDefaultDirectLight = 0.9F;
inline constexpr float kLeastDirectLight = 0.75F;

/// Where a model's light is found, and how.
struct ModelLightQuery {
    Vec3 origin{};
    SkyLight sky{};
    float direct = kDefaultDirectLight;
};

/// The light of a map's world: its faces' lightmaps, met along segments down its tree.
class WorldLight {
public:
    /// The light of `map`'s world, which must outlive it.
    explicit WorldLight(const assets::BspView& map);

    /// The light where the segment from `start` to `end` first meets a face of the world that is not
    /// special (sky, water...: SURF_DRAWTILED) at a point of its extent (R_LightVec): at the luxel
    /// nearest that point, the sample of each of its lightmaps times its style's brightness in
    /// `styles`, summed, divided by 256 and held to 255; black where the face has no samples, or where
    /// the segment meets no face. Fully bright on a map without light.
    [[nodiscard]] auto lightAlong(Vec3 start, Vec3 end, const LightStyleValues& styles) const -> LightColor;

    /// The light of a studio model at `query.origin` (R_StudioDynamicLight), its styles' brightness
    /// `styles`: the sky's, going its way, when the sky has a color and is the first face met against
    /// its way from 8 units above the origin; else the light along 2048 units down from there, going
    /// the way it grows dimmer across four points 16 units about the origin. Its brightest channel is
    /// its whole light: `query.direct` of it is shade (`direct` held from 0.75 to 1), the rest
    /// ambient, ambient no more than 128 and the two no more than 255.
    [[nodiscard]] auto modelLightAt(const ModelLightQuery& query, const LightStyleValues& styles) const
        -> ModelLight;

private:
    /// Whether the first face of the world the segment from `start` to `end` meets at a point of its
    /// extent is the sky's (SurfaceAtPoint).
    [[nodiscard]] auto meetsSky(Vec3 start, Vec3 end) const -> bool;

    const assets::BspView* m_map;
    std::vector<FaceLight> m_faces; ///< Every face of the map, by its index.
};

} // namespace nx::render
