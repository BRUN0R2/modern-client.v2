// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The light of a map's sky and its enhanced highlights and reflection probe locations
/// (docs/05-rendering.md, sections 8 and 9.2).
export module cs.client:mapLight;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.render;

export namespace cs {

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

/// The lights of `lights` that reach `point`, the nearest first, the first `count` of them. Working
/// storage is bounded by `count`; a zero budget does not scan the lights.
[[nodiscard]] auto nearestLights(std::span<const nx::render::StudioLight> lights, nx::Vec3 point,
                                 std::size_t count) -> std::vector<nx::render::StudioLight>;

} // namespace cs
