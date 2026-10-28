// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// When a bot's attack is down (docs/10-counter-strike-gameplay.md, section 11), as ReGameDLL_CS's
/// reconstruction of Condition Zero's bot fires (CCSBot::FireWeaponAtEnemy, CBot::BotThink,
/// TogglePrimaryAttack, StartRapidFire; GPL, consulted: docs/20 section 3): it weighs a press at each of
/// its full thinks, its buttons set anew then and held until the next; it presses aimed at its enemy,
/// past its attack delay and its pause, the next pause drawn by its weapon, its skill and the range;
/// firing a pistol as fast as it may, its attack goes up and down each command in between.
export module cs.bots:botTrigger;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import cs.shared;
import :botRules;

export namespace cs {

/// What a bot's trigger keeps between its commands: the enemy it took on and when (m_enemy,
/// m_currentEnemyAcquireTimestamp), when it thinks fully next (m_flNextFullBotThink), when it may press
/// next (m_fireWeaponTimestamp), whether it pressed at its last full think, whether it fires as fast as
/// it may (m_isRapidFiring), and whether its attack was down in its last command.
struct BotTrigger {
    std::optional<nx::ecs::Entity> enemy{};
    nx::GameTime enemySince{};
    nx::GameTime nextThinkAt{};
    nx::GameTime nextPressAt{};
    bool isPressed = false;
    bool isRapid = false;
    bool isDown = false;
};

/// What a bot weighs in a command to press its attack: the target it pursues, its eye, its view's yaw,
/// where it aims at the enemy it attacks (none while it attacks none), the slot of the weapon it holds,
/// its skill and attack delay, and the command's instant.
struct BotShot {
    std::optional<nx::ecs::Entity> pursued{};
    nx::Vec3 eye{};
    nx::Degrees viewYaw{};
    std::optional<nx::Vec3> aimSpot{};
    std::optional<WeaponSlot> heldSlot{};
    BotSkill skill{};
    nx::Seconds attackDelay{};
    nx::GameTime now{};
    nx::TickRate rate{};
};

/// Whether a view of `viewYaw` from `eye` points at `aimSpot` across the ground, as FireWeaponAtEnemy
/// weighs it: within the angle `halfWidth` spans beside it at its range. A spot straight above or below
/// is aimed at.
[[nodiscard]] auto isAimedAt(nx::Vec3 eye, nx::Degrees viewYaw, nx::Vec3 aimSpot,
                             nx::HammerUnits halfWidth) noexcept -> bool;

/// Whether a bot's attack is down in the command of `shot`, `trigger` kept for its next: a new target
/// pursued is its enemy from then on; attacking none, its attack is up and it fires as fast as it may no
/// more. At a full think, each BotFireRules::thinkInterval in whole ticks, the nearest, it presses, held
/// until the next, once its pause and its attack delay went by, aimed at where it aims (isAimedAt,
/// BotFireRules::aimHalfWidth), and draws its next pause from `random` (BotFireRules), a think less, as
/// the original takes that latency off it; between, its attack stays as it was set, or goes up and down
/// each command while it fires as fast as it may.
[[nodiscard]] auto pullTrigger(BotTrigger& trigger, const BotShot& shot, const BotFireRules& rules,
                               nx::Random& random) noexcept -> bool;

} // namespace cs
