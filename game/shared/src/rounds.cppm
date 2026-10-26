// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A map's rounds (docs/10-counter-strike-gameplay.md, section 3), as ReGameDLL_CS's
/// CHalfLifeMultiplay plays them (GPL, consulted: docs/20 section 3): each starts frozen for
/// mp_freezetime, its players barely moving and unable to fire, then runs for mp_roundtime, until
/// what its map is played for ends it, or its time runs out; the next starts kRestartDelay after it
/// ends. sv_restart restarts the game when it asks, the round playing on until then.
///
/// Today: rounds ended by their time running out, and by what the game ends them for (the draw of a
/// game whose only team has no player alive); the other ends that what the players do brings (a team
/// eliminated by the other, the bomb, the hostages, the VIP, the escape), Game Commencing, the teams'
/// scores and the end of the map come with them (TODO.md).
export module cs.shared:rounds;

import std;
import nx.core;
import :player;

export namespace cs {

/// How fast a player may move while its round is frozen: barely, as ResetMaxSpeed holds it.
inline constexpr nx::UnitsPerSecond kFreezeSpeed{1.0F};
/// How long after a round ends the next starts (GetRoundRestartDelay).
inline constexpr nx::Seconds kRestartDelay{5.0F};
/// How far past a time-out its round's start is put (CheckRoundTimeExpired's m_fRoundStartTime): the
/// round's remaining time is its length and this again, so that it does not run out at once.
inline constexpr nx::Seconds kTimeOutGrace{60.0F};
/// How much longer than mp_freezetime the first freeze of a map lasts.
inline constexpr nx::Seconds kFirstFreezeExtra{2.0F};
/// The class of a map's hostages.
inline constexpr std::string_view kHostageClass = "hostage_entity";

/// What a map's rounds are played for, as the game tells it from the classes of its entities
/// (CheckMapConditions, and CheckRoundTimeExpired's search for a hostage).
enum class MapObjective : std::uint8_t {
    BombTarget,    ///< A `func_bomb_target` or an `info_bomb_target`.
    Hostages,      ///< A `hostage_entity`.
    EscapeZone,    ///< A `func_escapezone`.
    VipSafetyZone, ///< A `func_vip_safetyzone`.
    Count,
};

using MapObjectives = nx::Flags<MapObjective>;

/// The objective an entity of class `classname` gives its map; none.
[[nodiscard]] auto objectiveOfClass(std::string_view classname) noexcept -> std::optional<MapObjective>;

/// Why a round ended, of those the game ends rounds by today (ScenarioEventEndRound): the others
/// come with what ends them (docs/10, section 3.1).
enum class RoundEndReason : std::uint8_t {
    TargetSaved,          ///< Its time ran out on a map with a bomb target, the bomb not planted.
    HostagesNotRescued,   ///< Its time ran out on a map with hostages.
    TerroristsNotEscaped, ///< Its time ran out on a map with an escape zone.
    VipNotEscaped,        ///< Its time ran out on a map with a VIP safety zone.
    /// Nobody was alive, a team having no player (TeamExterminationCheck's draw).
    RoundDraw,
    Count,
};

/// The team a round ended by `reason` was won by; none for a draw.
[[nodiscard]] auto winnerOf(RoundEndReason reason) noexcept -> std::optional<Team>;

/// How a round whose time ran out ends on a map played for `objectives`: by the first of a bomb
/// target, hostages, an escape zone and a VIP safety zone it has (CheckRoundTimeExpired); none for a
/// map played for none, whose rounds run on.
[[nodiscard]] auto timeOutReasonOf(MapObjectives objectives) noexcept -> std::optional<RoundEndReason>;

/// How long a round's phases last, as mp_freezetime and mp_roundtime set them, in whole seconds:
/// read as each round starts.
struct RoundLengths {
    nx::Seconds freeze{6.0F};
    nx::Seconds round{300.0F};
};

/// mp_freezetime and mp_roundtime as the game reads them as each round starts, and what it sets them
/// to where it holds them (ReadMultiplayCvars).
struct RoundCvars {
    RoundLengths lengths{};
    std::optional<float> freezeSeconds{}; ///< What mp_freezetime is set to where it was held.
    std::optional<float> roundMinutes{};  ///< What mp_roundtime is set to where it was held.
};

/// mp_freezetime's `freezeSeconds` and mp_roundtime's `roundMinutes` read as ReadMultiplayCvars reads
/// them: the minutes times 60 in float, each cut to whole seconds as the original's processors cut
/// them (nx::truncateAsX86); the round held to 60 to 540 seconds, mp_roundtime then set to 1 or 9, and
/// the freeze to 0 to 60, mp_freezetime then set to 0 or 60.
[[nodiscard]] auto readRoundCvars(float freezeSeconds, float roundMinutes) noexcept -> RoundCvars;

/// The restart sv_restartround's `restartRound` and sv_restart's `restart` ask for, as the game reads
/// them once a second (CheckRestartRound): sv_restartround cut to whole seconds, else sv_restart; above
/// 0, held to kMostRestart, which then sets both back to 0; none otherwise, both then left as they are.
[[nodiscard]] auto restartDelayOf(float restartRound, float restart) noexcept -> std::optional<nx::Seconds>;

/// The longest restart sv_restartround and sv_restart ask for (CheckRestartRound).
inline constexpr nx::Seconds kMostRestart{60.0F};

/// What the rules did as they played (Rounds::think).
enum class RoundEvent : std::uint8_t {
    FreezeEnded,   ///< The round's freeze ran out: its time runs, and its players move and fire.
    Ended,         ///< The round ended (Rounds::end says why).
    RoundStarted,  ///< The next round started, frozen: its players spawn again.
    GameRestarted, ///< The game restarted (sv_restart), frozen: its players spawn as they joined.
    Count,
};

using RoundEvents = nx::Flags<RoundEvent>;

/// What a player's HUD counts the round's time down from, as the game tells it as the player spawns
/// and as the round's freeze ends (SyncRoundTimer, the RoundTime message): the whole seconds the
/// round had left then, and when.
struct RoundTimer {
    std::uint32_t seconds{};
    nx::GameTime toldAt{};

    /// The whole seconds it shows at `now`, the game running at `rate`: those told, less the time
    /// since, cut to whole seconds, none below 0, as the original's client counts them down.
    [[nodiscard]] auto shownAt(nx::GameTime now, nx::TickRate rate) const noexcept -> std::uint32_t;
};

/// The rounds of a map, as its game plays them: one at a time, each frozen as it starts, then running
/// until it ends.
class Rounds {
public:
    /// The rounds of a map played for `objectives`, the first starting frozen at `now` as the map
    /// starts, `lengths` long, its freeze kFirstFreezeExtra longer.
    [[nodiscard]] static auto start(MapObjectives objectives, const RoundLengths& lengths,
                                    nx::GameTime now) noexcept -> Rounds;

    /// Whether the round is frozen: its players barely move (kFreezeSpeed) and do not fire. A round
    /// that ended may be: its freeze still runs out.
    [[nodiscard]] inline auto isFrozen() const noexcept -> bool { return m_isFrozen; }
    /// Why the round ended; none while it plays.
    [[nodiscard]] inline auto end() const noexcept -> std::optional<RoundEndReason> { return m_end; }
    /// When the next round starts: none before the round ends or a restart is asked for.
    [[nodiscard]] inline auto restartAt() const noexcept -> std::optional<nx::GameTime> {
        return m_restartAt;
    }

    /// How long the round has left at `now`, the game running at `rate`: of its freeze while frozen,
    /// else of its time; below 0 once it ran out.
    [[nodiscard]] auto remainingAt(nx::GameTime now, nx::TickRate rate) const noexcept -> nx::Seconds;
    /// What the game tells a player's HUD at `now` of the round's time (SyncRoundTimer): its whole
    /// seconds left, none below 0.
    [[nodiscard]] auto timerAt(nx::GameTime now, nx::TickRate rate) const noexcept -> RoundTimer;

    /// Restarts the game `delay` after `now` (sv_restart), the round playing on until then: its next
    /// round starts then, or kRestartDelay after the round ends when it ends before, and its players
    /// spawn as they joined.
    void restartGameIn(nx::Seconds delay, nx::GameTime now, nx::TickRate rate) noexcept;

    /// Ends the round by `reason` at `now`, the game running at `rate`, as what the players did ends it
    /// (TerminateRound): the next round starts kRestartDelay after, the game restarting then when a
    /// restart is asked for. Returns whether it ended it: a round that ended ends no more, as the
    /// original's CheckWinConditions looks no further once the round of a started game is decided.
    [[nodiscard]] auto endRound(RoundEndReason reason, nx::GameTime now, nx::TickRate rate) noexcept -> bool;

    /// Plays the rules at `now`, the game running at `rate`, in the original's order (Think): a
    /// freeze that ran out lets the round's time run; a time that ran out ends the round as its map's
    /// objectives say (timeOutReasonOf), the next round kRestartDelay after; then the next round
    /// starts when it is due, frozen, `lengths` long. Returns what they did.
    auto think(nx::GameTime now, nx::TickRate rate, const RoundLengths& lengths) noexcept -> RoundEvents;

private:
    Rounds(MapObjectives objectives, const RoundLengths& lengths, nx::GameTime now) noexcept;

    MapObjectives m_objectives;
    nx::Seconds m_roundLength; ///< Of the round's time, after its freeze.
    nx::GameTime m_startedAt;  ///< Of the freeze while frozen, else of the round's time.
    nx::Seconds m_length;      ///< Of the freeze while frozen, else of the round's time.
    bool m_isFrozen = true;
    std::optional<RoundEndReason> m_end{};
    std::optional<nx::GameTime> m_restartAt{};
    bool m_restartsGame = false; ///< The next round restarts the game (sv_restart).
};

} // namespace cs
