// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The hostages of a map as entities of the game's world (docs/10-counter-strike-gameplay.md, section
/// 4), as ReGameDLL's CHostage makes and plays them (Spawn, SetActivity, IdleThink, TraceAttack,
/// TakeDamage, RePosition; GPL, consulted: docs/20 section 3): each idling in a sequence of its model's
/// idle ones, drawn by weight, its frame advanced by the server as it thinks, a thirtieth of a second
/// apart, its head straight ahead, dropped to the floor; hurt by the attacks that meet it, flinching,
/// and killed, playing its death; put back where it started, whole, as each round
/// starts; solid as a box to the players and to the server's traces, which a line meets by its
/// hitboxes, posed as its last think left it; a passive human to the game's rules.
///
/// Today: idling, hurt and killed. Following, rescue, its pain's sounds and its blood come with what
/// they need (TODO.md).
export module cs.shared:hostages;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;
import :studio;
import :movement;
import :economy;
import :hitGroups;

export namespace cs {

/// A hostage's box about its origin, at its feet (VEC_HOSTAGE_HULL_MIN, VEC_HOSTAGE_HULL_MAX).
inline constexpr nx::Aabb kHostageBox{.min = {.x = -10.0F, .y = -10.0F, .z = 0.0F},
                                      .max = {.x = 10.0F, .y = 10.0F, .z = 62.0F}};
/// The health a hostage is made and put back with (its max_health).
inline constexpr nx::HitPoints kHostageHealth{100.0F};

/// A hostage of the map, as the game makes it: the model it plays, by its index among the played
/// models, where it is, and how it is turned.
struct HostageSpawn {
    std::uint32_t model = 0;
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
};

/// A hostage of the game's world, as the original's CHostage keeps it: which of the map's it is, by
/// the index of its spawn; the model it plays; where it started and how it was turned (m_vStart,
/// m_vStartAngles); what it does (m_Activity), none until it is set; its controllers, its head
/// straight ahead; its health; the hit group the last attack met it in (m_LastHitGroup); until when it
/// flinches (m_flFlinchTime); when it thinks its tenth of a second's part next (m_flNextFullThink);
/// and whether it is dead. Where it is, its Transform; how it plays its sequence, its Animation.
struct Hostage {
    std::uint32_t spawn = 0;
    std::uint32_t model = 0;
    nx::Vec3 start{};
    nx::EulerAngles startAngles{};
    std::optional<Activity> activity{};
    ControllerSettings controllers{};
    nx::HitPoints health{kHostageHealth};
    std::uint32_t lastHitGroup = 0;
    nx::GameTime flinchUntil{};
    nx::GameTime nextFullThink{};
    bool isDead = false;
};

/// What the hostages of a world play and take attacks by: the models they play, by index, and what
/// the hit groups make of an attack's damage. A resource of it, which registerHostages gives.
struct HostageData {
    std::span<const PlayedModel> models{};
    const HitGroupRules* hitGroups = nullptr;
};

/// How a hostage of the world shows: which of the map's it is, by the index of its spawn; where it is
/// and how it is turned; the sequence it plays and where in it the client estimates it is; and its
/// controllers.
struct ShownHostage {
    std::uint32_t spawn = 0;
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    nx::assets::SequenceIndex sequence{};
    SequenceFrame frame{};
    ControllerSettings controllers{};
};

/// Registers the hostages' components in `world`, made ready by nx::game::registerComponents, the
/// models they play, `models`, and what the hit groups make of an attack's damage, `hitGroups`; both
/// must outlive it. Errors: those of registering a component.
[[nodiscard]] auto registerHostages(nx::ecs::World& world, std::span<const PlayedModel> models,
                                    const HitGroupRules& hitGroups)
    -> nx::Result<void, nx::ecs::ComponentError>;

/// Makes in `world` a hostage of each of `spawns`, in their order, as CHostage::Spawn makes it, whole:
/// idling in a sequence of its model's idle ones, drawn by weight (lookupActivity), from its first
/// frame; its head's controllers straight ahead; dropped to the floor through what is solid in `world`,
/// whose map's hulls are `hulls`; solid as kHostageBox, a line meeting its hitboxes where its last think
/// left them (SV_HullForStudioModel: all in solid and not stopped for a model without any), a box its
/// box grown by the one traced; taking attacks (harmHostage, its DamageMethod); thinking first in 0.1
/// to 0.2 seconds, its tenth of a second's part first as long after, drawn at random; the draws of each
/// from a stream of its own. A spawn of a model `world`'s hostages do not play is left out.
void spawnHostages(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                   std::span<const HostageSpawn> spawns);

/// What an attack does to a hostage, as CHostage takes it (its DamageMethod): nothing once it is dead;
/// else its hit group is the trace's, and its damage scaled by that group's share of the monsters, as
/// their TraceAttack scales it, then of the hostages, as their TakeDamage does (GetModifiedDamage;
/// HitGroupRules), in double precision; its health at most, which it loses.
/// Alive, it flinches (ACT_SMALL_FLINCH) for 0.75 seconds; else its health is none, it plays the death
/// of where it was met (ACT_DIE_HEADSHOT for none and the head, ACT_DIESIMPLE for the chest and the
/// right arm, ACT_DIEFORWARD for the stomach and the right leg, ACT_DIEBACKWARD for the left arm and
/// leg), is no longer solid nor thinks, and is killed. The draws come from a stream of the tick's and the
/// hostage's.
[[nodiscard]] auto harmHostage(nx::ecs::World& world, nx::ecs::Entity entity, const nx::game::Attack& attack)
    -> nx::game::Harm;

/// What the player who made an attack loses for `harm` to a hostage, as TakeDamage takes it from its
/// account (AddAccount): `costs.perDamage` for each whole point of its damage, and `costs.perKill` more
/// when it killed it.
[[nodiscard]] auto hostageHarmCost(const nx::game::Harm& harm, const HostageHarmCosts& costs) noexcept
    -> nx::Money;

/// Puts the hostages of `world` back as a round starts, as RestartRound puts them back while it counts
/// their keepers' `bonus` (CHostage::RePosition): in the world's order, the rest left as they are once
/// the bonus counted for those alive reached its `upTo` (the original's loop stops there); each whole
/// and solid again, where it started, turned as it was, dropped to the floor, idling (a new idle drawn
/// for one dead or flinching), thinking again in 0.1 to 0.2 seconds, its tenth of a second's part as
/// long after, drawn at random from a stream of the tick's. Returns how many alive it counted.
auto repositionHostages(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                        const HostageBonus& bonus) -> std::uint32_t;

/// How each hostage of `world` shows at `time`, in the world's order: its frame where the client
/// estimates it from its last think's (estimatedFrameOf).
[[nodiscard]] auto hostagesShownAt(nx::ecs::World& world, nx::GameTime time) -> std::vector<ShownHostage>;

} // namespace cs
