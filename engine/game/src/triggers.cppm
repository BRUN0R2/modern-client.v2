// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The trigger volumes of a map (docs/07-simulation-and-physics.md, section 5.2): brush entities a
/// player fires by walking into them, as the original's game and engine fire them (CBaseTrigger,
/// SV_TouchLinks; ReGameDLL and ReHLDS, GPL, consulted: docs/20-legal-and-licensing.md, section 3).
export module nx.game:triggers;

import std;
import nx.core;
import nx.ecs;
import nx.collision;
import :components;

export namespace nx::game {

/// Whether `body` at `origin` touches the brush entity of `model` at `placement` (SV_TouchLinks): their
/// linked boxes meet, and the model's hull for the body's size holds its origin, the model not turned
/// (SV_HullForBsp).
[[nodiscard]] auto touchesVolume(const collision::BrushHulls& hulls, const Body& body, Vec3 origin,
                                 collision::ModelIndex model, const Transform& placement) -> bool;

/// A volume that fires its Targets when a player touches it (trigger_multiple, trigger_once): again
/// `wait` after, or never again for a negative `wait`.
struct Trigger {
    Seconds wait{};
    bool isTouchedByPlayers = true; ///< Not flagged "no clients".
    bool isSpent = false;           ///< Fired for good: it goes at its NextThink.
};

/// `toucher`, a Body, touches the triggers of `world` that its linked box meets and whose model's
/// hull for its size it is inside, in the world's order (SV_TouchLinks): each that is ready fires its
/// targets for it (CBaseTrigger::ActivateMultiTrigger), through the world of `hulls`; then it waits,
/// or is spent and goes a tenth of a second later.
void touchTriggers(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity toucher);

/// What a map's volume says it hurts by, of its "damagetype" (DMG_BURN, DMG_FREEZE, DMG_FALL), for the
/// game's rules to tell apart; none of those is generic. Time-based kinds come with their rules (TODO.md).
enum class VolumeHarmKind : std::uint8_t {
    Generic,
    Burn,
    Freeze,
    Fall,
    Count,
};

/// A volume that hurts what touches it, half a second apart (trigger_hurt): `damagePerSecond` over each
/// second, a negative one healing it; of `kind`. `clockEndsAt` (pev->dmgtime), `lastHurtAt`
/// (pev->pain_finished) and `touched` (pev->impulse, of any number of players, not the original's 32) are
/// what CBaseTrigger::HurtTouch keeps of the half second running.
struct HurtVolume {
    Damage damagePerSecond{};
    VolumeHarmKind kind = VolumeHarmKind::Generic;
    bool isTouchedByPlayers = true; ///< Not flagged "no clients".
    std::optional<GameTime> clockEndsAt{};
    std::optional<GameTime> lastHurtAt{};
    std::vector<ecs::Entity> touched{};
};

/// What a volume does to what touches it, for the game's rules to deal: a negative damage heals.
struct VolumeHarm {
    Damage damage{};
    VolumeHarmKind kind = VolumeHarmKind::Generic;

    [[nodiscard]] inline auto operator==(const VolumeHarm&) const noexcept -> bool = default;
};

/// `toucher`, a player's Body, touches the hurt volumes of `world` it is inside, as touchTriggers finds
/// them, in the world's order; what each deals it (CBaseTrigger::HurtTouch): half a second's damage, as a
/// new half second starts, or in the tick the last hurt something, or once more in a half second to one
/// it did not count yet; those it counted wait for the next.
[[nodiscard]] auto touchHurtVolumes(ecs::World& world, const collision::BrushHulls& hulls,
                                    ecs::Entity toucher) -> std::vector<VolumeHarm>;

} // namespace nx::game
