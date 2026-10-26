// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

namespace {

/// The team `team` plays against.
[[nodiscard]] auto opponentOf(Team team) noexcept -> Team {
    return team == Team::Terrorists ? Team::CounterTerrorists : Team::Terrorists;
}

/// `bonus` for `hostagesHeld` hostages, each counted while the sum is below its `upTo`.
[[nodiscard]] auto hostageBonusOf(const HostageBonus& bonus, std::uint32_t hostagesHeld) noexcept
    -> nx::Money {
    nx::Money sum{};
    for (std::uint32_t hostage = 0; hostage < hostagesHeld && sum < bonus.upTo; ++hostage) {
        sum = nx::addSaturated(sum, bonus.perHostage);
    }
    return sum;
}

} // namespace

auto RoundEndMoney::moneyOn(MapObjectives objectives) const noexcept -> nx::Money {
    return objectives.test(MapObjective::BombTarget) ? bombTargetMoney.value_or(money) : money;
}

auto EconomyRules::roundEnd(RoundEndReason reason) const noexcept -> RoundEndMoney {
    const auto found = std::ranges::find(roundEnds, reason, &RoundEndMoney::reason);
    return found != roundEnds.end() ? *found : RoundEndMoney{.reason = reason};
}

auto readEconomyRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<EconomyRules, EconomyError> {
    auto rules = nx::data::parseToml<EconomyRules>(text, sourceName);
    if (!rules) {
        return nx::fail(EconomyError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    std::set<RoundEndReason> reasons;
    for (const RoundEndMoney& end : rules->roundEnds) {
        if (!reasons.insert(end.reason).second) {
            return nx::fail(EconomyError::DataInvalid,
                            std::format("{}: the money of {} twice", sourceName, nx::enumName(end.reason)));
        }
    }
    return std::move(*rules);
}

auto addMoney(nx::Money money, nx::Money amount) noexcept -> nx::Money {
    return std::clamp(nx::addSaturated(money, amount), nx::Money{}, kMostMoney);
}

TeamAccounts::TeamAccounts(const EconomyRules& rules) noexcept
    : m_rules{&rules}, m_loserBonus{rules.loserBonus.first} {}

void TeamAccounts::roundEnded(RoundEndReason reason, MapObjectives objectives) noexcept {
    m_winner = winnerOf(reason);
    if (m_winner) {
        m_accounts[*m_winner] =
            nx::addSaturated(m_accounts[*m_winner], m_rules->roundEnd(reason).moneyOn(objectives));
    }
}

auto TeamAccounts::roundStarts(MapObjectives objectives, std::uint32_t hostagesHeld) noexcept
    -> nx::EnumArray<Team, nx::Money> {
    const LoserBonus& bonus = m_rules->loserBonus;
    if (m_winner) {
        m_accounts[*m_winner] =
            nx::addSaturated(m_accounts[*m_winner], hostageBonusOf(m_rules->hostageBonus, hostagesHeld));
        // A win that ends a streak of losses starts the bonus over.
        if (m_lossesInARow[*m_winner] > 1) {
            m_loserBonus = bonus.afterStreak;
        }
        m_lossesInARow[*m_winner] = 0;
        ++m_lossesInARow[opponentOf(*m_winner)];
    }
    // Whatever the round's end, a team losing round after round raises it.
    const bool isStreak =
        std::ranges::any_of(m_lossesInARow, [](std::uint32_t losses) { return losses > 1; });
    if (isStreak && m_loserBonus < bonus.most) {
        m_loserBonus = nx::addSaturated(m_loserBonus, bonus.step);
    }
    if (m_winner) {
        const Team loser = opponentOf(*m_winner);
        // The terrorists of a map with an escape zone are not helped to escape.
        if (loser != Team::Terrorists || !objectives.test(MapObjective::EscapeZone)) {
            m_accounts[loser] = nx::addSaturated(m_accounts[loser], m_loserBonus);
        }
    }
    m_winner.reset();
    return std::exchange(m_accounts, {});
}

void TeamAccounts::restartGame() noexcept {
    *this = TeamAccounts{*m_rules};
}

auto readStartMoney(float value) noexcept -> StartMoney {
    const std::int32_t money = nx::truncateAsX86(value);
    if (money > kMostMoney.value()) {
        return StartMoney{.money = kMostMoney, .setTo = static_cast<float>(kMostMoney.value())};
    }
    if (money < kStartMoney.value()) {
        return StartMoney{.money = kStartMoney, .setTo = static_cast<float>(kStartMoney.value())};
    }
    return StartMoney{.money = nx::Money{money}};
}

} // namespace cs
