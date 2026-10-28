// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The money of the rounds (docs/10-counter-strike-gameplay.md, section 6): data/cstrike/economy.toml,
/// with ReGameDLL_CS's values and rules (m_rgRewardAccountRules, TerminateRound's accounts,
/// RestartRound's loser bonus, AddAccount and MarkLivingPlayersOnTeamAsNotReceivingMoneyNextRound;
/// GPL, consulted: docs/20 section 3): what the winners of a round get by why it ended, the bonus the
/// losers get, which grows while a team loses round after round, and what each team's players are
/// paid as the next round starts.
///
/// Today: the money of the rounds whose time ran out and of those a team won by eliminating the other;
/// the kills', the bomb's and the hostages', and buying, come with them (TODO.md).
export module cs.shared:economy;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :player;
import :rounds;

export namespace cs {

/// Why the money's data could not be read.
enum class EconomyError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// What the winners of a round ended by `reason` get, on a map with a bomb target what they get there
/// instead when it says so (Round_Cts and Round_Ts pay the bomb's rewards there), and whether its losers
/// alive as it ended are paid as the next round starts.
struct RoundEndMoney {
    RoundEndReason reason{};
    nx::Money money{};
    std::optional<nx::Money> bombTargetMoney{};
    bool paysLivingLosers = true;

    /// What its winners get on a map played for `objectives`.
    [[nodiscard]] auto moneyOn(MapObjectives objectives) const noexcept -> nx::Money;
};

/// The bonus the losers of a round get (m_iLoserBonus): `first` as a game starts; `afterStreak` once
/// a team wins after losing more than once in a row; then `step` more as each round starts while a
/// team has lost more than once in a row, while it is below `most`.
struct LoserBonus {
    nx::Money first{};
    nx::Money afterStreak{};
    nx::Money step{};
    nx::Money most{};
};

/// What the winners of a round get as the next round starts for the hostages still held
/// (RestartRound): `perHostage` for each, counted until the sum reaches `upTo`.
struct HostageBonus {
    nx::Money perHostage{};
    nx::Money upTo{};
};

/// What a player who hurts a hostage loses (CHostage::TakeDamage): `perDamage` for each whole point of
/// the damage it took, and `perKill` more when it killed it.
struct HostageHarmCosts {
    nx::Money perDamage{};
    nx::Money perKill{};
};

/// What a player gets for killing a player of the other team (REWARD_KILLED_ENEMY), and loses for
/// killing one of its own (PAYBACK_FOR_KILLED_TEAMMATES) (CHalfLifeMultiplay::PlayerKilled).
struct KillMoney {
    nx::Money enemy{};
    nx::Money teammate{};
};

/// data/cstrike/economy.toml.
struct EconomyRules {
    LoserBonus loserBonus{};
    HostageBonus hostageBonus{};
    HostageHarmCosts hostageHarm{};
    KillMoney kills{};
    std::vector<RoundEndMoney> roundEnds{}; ///< One for each reason at most.

    /// What the file says of a round ended by `reason`: nothing for its winners, its losers paid,
    /// when it says nothing.
    [[nodiscard]] auto roundEnd(RoundEndReason reason) const noexcept -> RoundEndMoney;
};

/// Reads `text`, the file `sourceName`: each round end's money once at most. Errors: DataInvalid.
[[nodiscard]] auto readEconomyRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<EconomyRules, EconomyError>;

/// mp_startmoney as the game reads it, and what it sets it to where it holds it (CheckStartMoney).
struct StartMoney {
    nx::Money money{kStartMoney};
    std::optional<float> setTo{}; ///< What mp_startmoney is set to where it was held.
};

/// mp_startmoney's `value` read as CheckStartMoney reads it, then the money given: cut to whole money
/// as the original's processors cut it (nx::truncateAsX86), held to kStartMoney to kMostMoney, the
/// cvar then set to the bound.
[[nodiscard]] auto readStartMoney(float value) noexcept -> StartMoney;

/// `money` with `amount` added, held from none to kMostMoney (AddAccount).
[[nodiscard]] auto addMoney(nx::Money money, nx::Money amount) noexcept -> nx::Money;

/// The money of a game's teams between its rounds: what each team's players are paid as the next
/// round starts, and the losses in a row its loser bonus grows by.
class TeamAccounts {
public:
    /// The accounts of a game as it starts by `rules`, which must outlive them: nothing to pay, no
    /// losses, the loser bonus `first`.
    explicit TeamAccounts(const EconomyRules& rules) noexcept;

    /// A round ended by `reason` on a map played for `objectives` (TerminateRound): what its winners get
    /// there added to their team's account, and it is the round the next start pays for.
    void roundEnded(RoundEndReason reason, MapObjectives objectives) noexcept;

    /// As the next round starts on a map played for `objectives`, `hostagesHeld` of its hostages not
    /// rescued (RestartRound): after a round won, the hostage bonus for them added to the winners'
    /// account, the losses in a row counted, the loser bonus set by them (LoserBonus) and added to the
    /// losers', but the terrorists' on a map with an escape zone. Returns what each team's players are
    /// paid, the accounts emptied for the round that starts.
    [[nodiscard]] auto roundStarts(MapObjectives objectives, std::uint32_t hostagesHeld) noexcept
        -> nx::EnumArray<Team, nx::Money>;

    /// As the game restarts (sv_restart): as a game starts.
    void restartGame() noexcept;

    /// The bonus the losers of the next round won get, as the losses so far set it.
    [[nodiscard]] inline auto loserBonus() const noexcept -> nx::Money { return m_loserBonus; }
    /// How many rounds `team` lost in a row, those of the round ended not counted until the next starts.
    [[nodiscard]] inline auto lossesInARow(Team team) const noexcept -> std::uint32_t {
        return m_lossesInARow[team];
    }

private:
    const EconomyRules* m_rules;
    nx::EnumArray<Team, nx::Money> m_accounts{};
    nx::EnumArray<Team, std::uint32_t> m_lossesInARow{};
    nx::Money m_loserBonus;
    std::optional<Team> m_winner{}; ///< Of the round the next start pays for.
};

} // namespace cs

template <>
struct nx::meta::Description<cs::RoundEndMoney> {
    static constexpr std::string_view kName = "cs::RoundEndMoney";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::RoundEndMoney::reason>("reason"),
        nx::meta::field<&cs::RoundEndMoney::money>("money", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::RoundEndMoney::bombTargetMoney>("bombTargetMoney",
                                                             nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::RoundEndMoney::paysLivingLosers>("paysLivingLosers"),
    };
};

template <>
struct nx::meta::Description<cs::LoserBonus> {
    static constexpr std::string_view kName = "cs::LoserBonus";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::LoserBonus::first>("first", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::LoserBonus::afterStreak>("afterStreak", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::LoserBonus::step>("step", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::LoserBonus::most>("most", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::KillMoney> {
    static constexpr std::string_view kName = "cs::KillMoney";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::KillMoney::enemy>("enemy", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::KillMoney::teammate>("teammate", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::HostageHarmCosts> {
    static constexpr std::string_view kName = "cs::HostageHarmCosts";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HostageHarmCosts::perDamage>("perDamage", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HostageHarmCosts::perKill>("perKill", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::HostageBonus> {
    static constexpr std::string_view kName = "cs::HostageBonus";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HostageBonus::perHostage>("perHostage", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HostageBonus::upTo>("upTo", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::EconomyRules> {
    static constexpr std::string_view kName = "cs::EconomyRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::EconomyRules::loserBonus>("loserBonus"),
        nx::meta::field<&cs::EconomyRules::hostageBonus>("hostageBonus"),
        nx::meta::field<&cs::EconomyRules::hostageHarm>("hostageHarm"),
        nx::meta::field<&cs::EconomyRules::kills>("kills"),
        nx::meta::field<&cs::EconomyRules::roundEnds>("roundEnds"),
    };
};
