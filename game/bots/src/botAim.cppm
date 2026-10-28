// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a bot turns its view and how its aim errs by its skill (docs/10-counter-strike-gameplay.md,
/// section 11), as ReGameDLL_CS's reconstruction of Condition Zero's bot weighs them
/// (CCSBot::UpdateLookAngles, CCSBot::SetAimOffset, CCSBot::UpdateAimOffset, CCSBot::Upkeep; GPL,
/// consulted: docs/20 section 3). The original moves them each of its thirty commands a second; here
/// they move over each command's duration, the same on any tick rate.
export module cs.bots:botAim;

import std;
import nx.core;
import nx.math;
import :botRules;

export namespace cs {

/// How fast a bot's view turns about its pitch and its yaw (m_lookPitchVel, m_lookYawVel).
struct BotViewTurn {
    nx::DegreesPerSecond pitch{};
    nx::DegreesPerSecond yaw{};
};

/// `view` turned towards `desired` for `duration`, `turn` sped up by `spring` towards it
/// (UpdateLookAngles): its yaw set to the desired one, not turning, once nearer than
/// BotLookRules::snapTolerance; its pitch pulled BotLookRules::pitchStiffness times as hard, and held
/// within BotLookRules::pitchLimit; rolled none.
[[nodiscard]] auto turnView(nx::EulerAngles view, nx::EulerAngles desired, BotViewTurn& turn,
                            const BotSpring& spring, const BotLookRules& rules, nx::Seconds duration) noexcept
    -> nx::EulerAngles;

/// Whether a view turning as `turn` does turns faster than `speed` about its pitch or its yaw
/// (IsViewMoving).
[[nodiscard]] auto isTurningFaster(const BotViewTurn& turn, nx::DegreesPerSecond speed) noexcept -> bool;

/// How a bot's aim errs about where it means to aim (m_aimOffset): the error now, the one it moves to
/// (m_aimOffsetGoal), when it draws the next (m_aimOffsetTimestamp), and since when its view held steady
/// enough for it to focus (m_aimSpreadTimestamp), the game's start for a bot that just spawned.
struct BotAimError {
    nx::Vec3 offset{};
    nx::Vec3 goal{};
    nx::GameTime nextGoalAt{};
    nx::GameTime steadySince{};
};

/// What a bot's aim error is weighed by in a command: its skill, the range to where it aims, whether
/// its view turns faster than BotAimRules::steadyTurn, the command's instant and its duration.
struct BotAimMoment {
    BotSkill skill{};
    nx::HammerUnits range{};
    bool isTurning = false;
    nx::GameTime now{};
    nx::TickRate rate{};
    nx::Seconds duration{};
};

/// Moves `error` over `moment` (UpdateAimOffset): when its next error is due, it draws one on each axis
/// within the range's BotAimRules::errorPerRange times the share of skill the bot lacks, a skill it
/// focused its aim to counted, from `random`, and when to draw again (SetAimOffset); then its error
/// moves the share of the way to it BotAimRules::settleShare makes over the command.
void settleAimError(BotAimError& error, const BotAimMoment& moment, const BotAimRules& rules,
                    nx::Random& random) noexcept;

/// Where a bot of `skill`, moving at `velocity`, aims to meet `spot` of an enemy moving at
/// `enemyVelocity` (Upkeep): ahead of it by their velocities apart over BotAimRules::leadTime above
/// BotAimRules::leadingSkill, else at it.
[[nodiscard]] auto leadSpot(nx::Vec3 spot, nx::Vec3 enemyVelocity, nx::Vec3 velocity, BotSkill skill,
                            const BotAimRules& rules) noexcept -> nx::Vec3;

} // namespace cs
