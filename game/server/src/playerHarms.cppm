// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What hurts a body of the game: the world and the attacks of its players, as the victim's TraceAttack and
/// TakeDamage take them, the blood they shed, and a player's death (docs/10-counter-strike-gameplay.md,
/// section 7.4).
export module cs.server:playerHarms;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.game;
import cs.shared;
import :gameState;

namespace cs {

/// What a mark stamped on `entity` of `world` is on: the world for none, else the entity's brush model where
/// the game has it now.
[[nodiscard]] auto stampTargetOf(const nx::ecs::World& world, std::optional<nx::ecs::Entity> entity)
    -> StampTarget;

/// A body an attack draws blood from, as SpawnBlood and TraceBleed take it: the body, the damage the
/// attack dealt it once where it met it took its share, where it met it, and the palette entry of its
/// blood's color.
struct Bleeding {
    nx::ecs::Entity body{};
    nx::Damage dealt{};
    HitGroup hitGroup = HitGroup::Generic;
    std::uint8_t color = 0;
};

/// `victim` feels `attack` of a player of `team`, none for an entity's body: where it came from, back
/// along its way (PlayerOwnState::hurtFrom).
void feelHurt(Player& victim, const nx::game::Attack& attack, std::optional<Team> team);

/// The blood `bleeding`'s body sheds as `attack` meets it, none for an attack that dealt it nothing:
/// a spray of the tick where the attack met it, the way it went, of its color, its size (spraySizeOf,
/// SpawnBlood) and where it met it; then on what is solid behind it, through the world and the brush
/// entities, the bodies left be, along the ways it traces (bleedingWaysOf), as far as BloodRules::reach,
/// a mark of the tick where one of them leaves its decal (bloodDecalOf) on the world or a brush entity
/// (TraceBleed, UTIL_BloodDecalTrace); drawn from `random`.
void bleed(GameState& game, const nx::game::Attack& attack, const Bleeding& bleeding, nx::Random& random);

/// `player` dies (Killed), killed by `killer`, none for the world: every client is told of it
/// (KillNotice), it is heard dying, moves no more (MovementState::isDead), plays its death by where it
/// was last met, is no longer solid, and its weapons are gone with it; it counts a death and its killer,
/// another player, a frag for an enemy, or one less for a teammate or for itself, none for the world, and
/// gets the money of an enemy's kill, or loses a teammate's (CHalfLifeMultiplay::PlayerKilled,
/// KillMoney); then the round's end is weighed (checkWinConditions).
void die(GameState& game, Player& player, Player* killer);

/// `player` is dealt `harm` of the world (CBasePlayer::TakeDamage): alive, its armor takes its share
/// and it loses its whole points, keeping a harm kept over time (harmPlayer), and is heard in its pain
/// by where it was last met (painOf); killed by it, it dies (die).
void hurt(GameState& game, Player& player, const PlayerHarm& harm);

/// `player` lands, hurt by `damage` (PostThink): it splats when the damage is more than its health,
/// met in no group, is hurt by its fall (hurt), and loses the pitch of its kick.
void land(GameState& game, Player& player, nx::Damage damage);

/// `player`, alive, touches the map's volumes that hurt it (nx::game::touchHurtVolumes): each hurts it
/// (hurt), or heals it for a negative damage.
void touchHurtVolumes(GameState& game, Player& player);

/// Over the head of `entity`, a solid player's body, hostage or entity's body, as the server's traces
/// meet it, standing or ducked (playerHeadTopOf, hostageHeadTopOf, entityHeadTopOf); over the middle
/// of its box's top for one of a model of no hitbox, its box what a trace meets of it then.
[[nodiscard]] auto headTopOf(GameState& game, nx::ecs::Entity entity) -> nx::Vec3;

/// `attacker`'s `attack` on `victim`, alive, as the victim's TraceAttack and TakeDamage take it: it is
/// met in the attack's hit group, its damage that group's share of the players'; a helmet it met
/// sparks where it met it, off its face (sparksOffHelmet, HelmetSparks); its view punched
/// (punchByHit) and, where its armor lets it (shedsBlood), its blood streamed from a head the way the
/// attack went, at its speed (streamSpeedOf, BloodSplat), and shed (bleed) but by a teammate while
/// mp_friendlyfire is 0, whose attack then hurts it none (FPlayerCanTakeDamage); a teammate's attack
/// deals its share of it (PlayerAttackRules); then it flinches, large as its attacker's weapon makes
/// it but in the legs and ducked, knocked back while it
/// moves slow enough, else small, slowed down; its armor takes its share, its attacker's weapon's
/// times the armor's ratio (WeaponHarm::armorRatio), and it loses its whole points (harmPlayer), heard
/// in its pain (painOf), what it lost told to the attacker's client over its head (DamageDealt,
/// headTopOf); killed by it, it
/// dies, killed by `attacker` (die). The punch's and the blood's draws come from streams of the
/// attacker's command and the victim.
void attackPlayer(GameState& game, Player& attacker, Player& victim, const nx::game::Attack& attack);

/// `player`'s `attack` on `entity`, as its weapon's TraceAttack and ApplyMultiDamage deal it: to
/// another player's body as attackPlayer deals it; to another entity as its kind takes it
/// (nx::game::attackEntity), a hostage it hurts costing it its money (hostageHarmCost), and an
/// entity's body of a type that bleeds (EntityDefinition::bloodColor) shedding the blood of what it
/// was dealt (bleed), drawn from a stream of the attack's command and the entity; what it dealt a
/// hostage or an entity's body told to the player's client over its head as the attack met it, before
/// a death posed it anew (DamageDealt, headTopOf).
void attackEntity(GameState& game, Player& player, nx::ecs::Entity entity, const nx::game::Attack& attack);

} // namespace cs
