// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The light the game sets for a map, as nxview plays the game until the server spawns its entities:
/// the patterns of its light styles, and the light of its sky (docs/05-rendering.md, sections 8 and
/// 9.2).
export module tool.nxview:mapLight;

import std;
import nx.core;
import nx.assets;
import nx.render;

export namespace nxview {

/// The light styles the game sets for `map`: the world's patterns, as Quake's world.qc sets them (GPL,
/// consulted for behavior: docs/20 section 3), under the names the Half-Life FGDs give the styles of a
/// light; and those of the switchable lights, from their entities. Style 12, the FGDs' underwater
/// mutation, has no public pattern: it stays unset.
[[nodiscard]] auto mapLightStyles(const nx::assets::BspView& map) -> nx::render::LightStyles;

/// The light of `map`'s sky, as the game sets it from its `light_environment` (the original's
/// sv_skycolor and sv_skyvec), as ReGameDLL's CEnvLight sets it (GPL, consulted: docs/20 section 3):
/// its `_light`, one gray level, "r g b" or "r g b brightness", the compilers' light, made the engine's
/// (pow(level / 114, 0.6) * 264, each cut to a whole number); and the way it aims, its `angles` and its
/// `pitch` in the order the map gives them, `pitch` up for an aim. The last of them sets it; without
/// one, the sky has no light.
[[nodiscard]] auto skyLightOf(const nx::assets::BspView& map) -> nx::render::SkyLight;

} // namespace nxview
