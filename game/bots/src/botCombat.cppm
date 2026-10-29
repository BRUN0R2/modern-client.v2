// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a bot moves as it fights (docs/10-counter-strike-gameplay.md, section 11), as ReGameDLL_CS's
/// reconstruction of Condition Zero's bot does (AttackState::OnEnter and OnUpdate, CBot::IsPlayerFacingMe,
/// CCSBot::IsNotMoving; GPL, consulted: docs/20 section 3): a fight it begins it dodges or not, by its skill;
/// dodging, it keeps its combat range and, while its enemy faces it, slides from side to side, now and
/// then jumping.
export module cs.bots:botCombat;

import std;
import nx.core;
import nx.math;
import cs.shared;
import :botRules;

export namespace cs {

/// The ways a bot dodges (AttackState's DodgeStateType): holding still, sliding left or right, jumping.
enum class BotDodge : std::uint8_t {
    SteadyOn,
    SlideLeft,
    SlideRight,
    Jump,
    Count,
};

/// What a bot's fighting keeps between its commands: whether it fights, whether it dodges this fight
/// (m_dodge), the way it dodges (m_dodgeState), when it draws another (m_nextDodgeStateTimestamp), whether
/// it drew one yet this fight (m_firstDodge), and the range it keeps from its enemy (m_combatRange).
struct BotCombat {
    bool isFighting = false;
    bool dodges = false;
    BotDodge dodge = BotDodge::SteadyOn;
    nx::GameTime nextDodgeAt{};
    bool hasDodged = false;
    nx::HammerUnits range{};
};

/// What a fighting bot weighs in a command: its origin and how it moves, its enemy's origin and where its
/// view looks, its skill, whether it holds a knife, how fast it may run, and the command's instant.
struct BotFight {
    nx::Vec3 origin{};
    nx::Vec3 velocity{};
    nx::Vec3 enemyOrigin{};
    nx::EulerAngles enemyView{};
    BotSkill skill{};
    bool holdsKnife = false;
    nx::UnitsPerSecond maxSpeed{};
    nx::GameTime now{};
    nx::TickRate rate{};
};

/// How a fighting bot moves in a command, along its view: ahead (back when negative), to its right (left
/// when negative), and whether it jumps.
struct BotFightMove {
    nx::UnitsPerSecond forward{};
    nx::UnitsPerSecond side{};
    bool jumps = false;
};

/// The fighting of a bot that spawns: its combat range drawn from `random` within rules.combatRange
/// (ResetValues), no fight begun.
[[nodiscard]] auto spawnedCombat(const BotCombatRules& rules, nx::Random& random) noexcept -> BotCombat;

/// Whether a player at `enemyOrigin` looking along `enemyView` faces `origin` (IsPlayerFacingMe): its view
/// points toward it across rather than away.
[[nodiscard]] auto isFacing(nx::Vec3 enemyOrigin, nx::EulerAngles enemyView, nx::Vec3 origin) noexcept
    -> bool;

/// How a bot moves in the command of `fight`, `combat` kept for its next, by `rules`, drawing from
/// `random`. As a fight begins it dodges it at random, dodgeChance of its skill, knifeDodgeFactor times
/// that with a knife (OnEnter). Dodging, it keeps its combat range within rangeSlack, ahead or back, below
/// rangeSkill; and while its enemy faces it within dodgeRange it holds still or slides left or right, a
/// way drawn each dodgeTime other than the last, the first of a fight also a jump at jumpChance, from
/// jumpSkill and moving faster than movingSpeed; else it holds still, a way drawn as soon as it faces it.
[[nodiscard]] auto fightMove(BotCombat& combat, const BotFight& fight, const BotCombatRules& rules,
                             nx::Random& random) noexcept -> BotFightMove;

/// Its fight is over: the next begins anew (AttackState::OnExit).
inline void endFight(BotCombat& combat) noexcept {
    combat.isFighting = false;
}

} // namespace cs
