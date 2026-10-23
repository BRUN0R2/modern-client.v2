// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a bullet leaves where it meets the world or a brush entity (docs/10-counter-strike-gameplay.md,
/// section 7.3): the decal Counter-Strike's game names for it, by what the entity is and how it is
/// drawn (ReGameDLL_CS's CBaseEntity::DamageDecal and CBreakable::DamageDecal, GPL, consulted: docs/20
/// section 3).
export module cs.client:bulletMarks;

import std;
import nx.core;
import nx.assets;

export namespace cs {

/// The kind of decal a bullet leaves on a surface.
enum class BulletMark : std::uint8_t {
    None,        ///< None: on a solid entity (kRenderTransAlpha).
    Gunshot,     ///< One of kGunshotDecals: on the world, on an entity drawn normally.
    Bulletproof, ///< kBulletproofDecal: on an entity drawn otherwise, on unbreakable glass.
    BrokenGlass, ///< One of kBrokenGlassDecals: on a breakable of glass.
};

/// The decals of decals.wad bullets leave, as the game names them (DECAL_GUNSHOT1 to 5,
/// DECAL_GLASSBREAK1 to 3, DECAL_BPROOF1).
inline constexpr auto kGunshotDecals =
    std::to_array<std::string_view>({"{shot1", "{shot2", "{shot3", "{shot4", "{shot5"});
inline constexpr auto kBrokenGlassDecals = std::to_array<std::string_view>({"{break1", "{break2", "{break3"});
inline constexpr std::string_view kBulletproofDecal = "{bproof1";

/// Every decal bullets leave: those a level reads before it is played.
[[nodiscard]] auto bulletDecals() -> std::vector<std::string_view>;

/// The mark a bullet leaves on the brush entity of the map `entity`: on a func_breakable, by its
/// "material", BrokenGlass for glass (its default) and Bulletproof for unbreakable glass; otherwise, and
/// on a breakable of another material, by its "rendermode": None for kRenderTransAlpha, Gunshot for
/// kRenderNormal, Bulletproof for any other.
[[nodiscard]] auto bulletMarkOf(const nx::assets::BspEntity& entity) -> BulletMark;

/// The decal `mark` leaves, one of its decals at random from `random` when it has several; none for
/// None.
[[nodiscard]] auto bulletDecalOf(BulletMark mark, nx::Random& random) -> std::optional<std::string_view>;

} // namespace cs
