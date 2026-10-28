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

[[nodiscard]] auto isPositive(float value) noexcept -> bool {
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

auto readBotRules(std::string_view text, std::string_view sourceName) -> nx::Result<BotRules, BotError> {
    auto rules = nx::data::parseToml<BotRules>(text, sourceName);
    if (!rules) {
        return nx::fail(BotError::InvalidData, std::string{sourceName}, std::move(rules.error()));
    }
    const bool isValid =
        isPositive(rules->fieldOfView.value()) && rules->fieldOfView.value() <= kWholeTurnDegrees &&
        rules->perceptionBudget > 0 && isPositive(rules->memoryLifetime.count()) && rules->memoryLimit > 0 &&
        isPositive(rules->attackDistance.value()) && isPositive(rules->arrivalDistance.value()) &&
        isPositive(rules->replanDistance.value()) && isPositive(rules->huntRadius.value()) &&
        isPositive(rules->quotaInterval.count());
    if (!isValid) {
        return nx::fail(BotError::InvalidData,
                        std::format("{}: every distance, time and budget more than none, and a field of view "
                                    "of a whole turn at most, are required",
                                    sourceName));
    }
    return std::move(*rules);
}

auto brainRulesOf(const BotRules& rules) noexcept -> nx::agents::Rules {
    return nx::agents::Rules{.memoryLifetime = rules.memoryLifetime,
                             .attackDistance = rules.attackDistance,
                             .arrivalDistance = rules.arrivalDistance,
                             .replanDistance = rules.replanDistance,
                             .memoryLimit = rules.memoryLimit};
}

} // namespace cs
