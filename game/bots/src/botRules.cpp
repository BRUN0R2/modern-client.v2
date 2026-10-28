// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.data;
import nx.agents;

namespace cs {

namespace {

/// The widest cone a bot may see in: all around it.
constexpr float kWholeTurnDegrees = 360.0F;
/// The steepest a view may look up or down: straight up or down.
constexpr float kQuarterTurnDegrees = 90.0F;

[[nodiscard]] auto isPositive(float value) noexcept -> bool {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] auto isNoneOrMore(float value) noexcept -> bool {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] auto isValid(const BotPause& pause) noexcept -> bool {
    return isNoneOrMore(pause.shortest.count()) && isNoneOrMore(pause.longest.count()) &&
           pause.shortest <= pause.longest;
}

[[nodiscard]] auto isValid(const BotSpring& spring) noexcept -> bool {
    return isPositive(spring.stiffness.value()) && isPositive(spring.damping.value()) &&
           isPositive(spring.maxAcceleration.value());
}

[[nodiscard]] auto isValid(const BotSkillLevel& level) noexcept -> bool {
    return !level.difficulties.empty() && isNoneOrMore(level.reactionTime.count()) &&
           isNoneOrMore(level.attackDelay.count());
}

/// Whether every difficulty has a level of skill a bot joining at it plays with.
[[nodiscard]] auto coversEveryDifficulty(std::span<const BotSkillLevel> levels) -> bool {
    return std::ranges::all_of(
        nx::EnumArray<BotDifficulty, bool>::keys(), [levels](BotDifficulty difficulty) {
            return std::ranges::any_of(levels, [difficulty](const BotSkillLevel& level) {
                return std::ranges::contains(level.difficulties, difficulty);
            });
        });
}

[[nodiscard]] auto isValid(const BotLookRules& look) noexcept -> bool {
    return isValid(look.attacking) && isValid(look.roaming) && isPositive(look.pitchStiffness.value()) &&
           isPositive(look.pitchLimit.value()) && look.pitchLimit.value() <= kQuarterTurnDegrees;
}

[[nodiscard]] auto isValid(const BotAimRules& aim) noexcept -> bool {
    return isPositive(aim.slowestFocus.count()) && isPositive(aim.quickestFocus.count()) &&
           isPositive(aim.steadyTurn.value()) && isNoneOrMore(aim.soonestNewError.count()) &&
           aim.soonestNewError <= aim.latestNewError && isPositive(aim.settleRate.value()) &&
           isNoneOrMore(aim.leadTime.count());
}

[[nodiscard]] auto isValid(const BotFireRules& fire) noexcept -> bool {
    return isPositive(fire.aimHalfWidth.value()) && isPositive(fire.thinkInterval.count()) &&
           isValid(fire.pistolPause) && isValid(fire.distantPause) && isValid(fire.burstPause);
}

} // namespace

auto readBotRules(std::string_view text, std::string_view sourceName) -> nx::Result<BotRules, BotError> {
    auto rules = nx::data::parseToml<BotRules>(text, sourceName);
    if (!rules) {
        return nx::fail(BotError::InvalidData, std::string{sourceName}, std::move(rules.error()));
    }
    const bool isValidRules =
        isPositive(rules->fieldOfView.value()) && rules->fieldOfView.value() <= kWholeTurnDegrees &&
        rules->perceptionBudget > 0 && isPositive(rules->memoryLifetime.count()) && rules->memoryLimit > 0 &&
        isPositive(rules->attackDistance.value()) && isPositive(rules->arrivalDistance.value()) &&
        isPositive(rules->replanDistance.value()) && isPositive(rules->huntRadius.value()) &&
        isPositive(rules->quotaInterval.count());
    if (!isValidRules) {
        return nx::fail(BotError::InvalidData,
                        std::format("{}: every distance, time and budget more than none, and a field of view "
                                    "of a whole turn at most, are required",
                                    sourceName));
    }
    const auto isValidLevel = [](const BotSkillLevel& level) {
        return isValid(level);
    };
    if (!std::ranges::all_of(rules->skillLevels, isValidLevel) ||
        !coversEveryDifficulty(rules->skillLevels)) {
        return nx::fail(BotError::InvalidData,
                        std::format("{}: each difficulty needs a level of skill, and each level its "
                                    "difficulties and times of none or more",
                                    sourceName));
    }
    if (!isValid(rules->look) || !isValid(rules->aim) || !isValid(rules->fire)) {
        return nx::fail(BotError::InvalidData,
                        std::format("{}: the springs, the pitch's limit (a quarter turn at most), the aim's "
                                    "focus and rates and the press more than none, and each pause of none or "
                                    "more, its longest no shorter, are required",
                                    sourceName));
    }
    return std::move(*rules);
}

auto brainRulesOf(const BotRules& rules, nx::Seconds reactionTime) noexcept -> nx::agents::Rules {
    return nx::agents::Rules{.memoryLifetime = rules.memoryLifetime,
                             .attackDistance = rules.attackDistance,
                             .arrivalDistance = rules.arrivalDistance,
                             .replanDistance = rules.replanDistance,
                             .memoryLimit = rules.memoryLimit,
                             .reactionTime = reactionTime};
}

} // namespace cs
