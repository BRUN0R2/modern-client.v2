// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;

namespace cs {

namespace {

/// A class of entity that gives its map an objective.
struct ObjectiveClass {
    std::string_view classname;
    MapObjective objective;
};

constexpr std::array kObjectiveClasses{
    ObjectiveClass{.classname = "func_bomb_target", .objective = MapObjective::BombTarget},
    ObjectiveClass{.classname = "info_bomb_target", .objective = MapObjective::BombTarget},
    ObjectiveClass{.classname = kHostageClass, .objective = MapObjective::Hostages},
    ObjectiveClass{.classname = "func_escapezone", .objective = MapObjective::EscapeZone},
    ObjectiveClass{.classname = "func_vip_safetyzone", .objective = MapObjective::VipSafetyZone},
};

/// How a round whose time ran out ends on a map played for an objective.
struct TimeOut {
    MapObjective objective;
    RoundEndReason reason;
};

/// In the order CheckRoundTimeExpired looks for them.
constexpr std::array kTimeOuts{
    TimeOut{.objective = MapObjective::BombTarget, .reason = RoundEndReason::TargetSaved},
    TimeOut{.objective = MapObjective::Hostages, .reason = RoundEndReason::HostagesNotRescued},
    TimeOut{.objective = MapObjective::EscapeZone, .reason = RoundEndReason::TerroristsNotEscaped},
    TimeOut{.objective = MapObjective::VipSafetyZone, .reason = RoundEndReason::VipNotEscaped},
};

constexpr nx::EnumArray<RoundEndReason, std::optional<Team>> kWinners{
    nx::EnumArray<RoundEndReason, std::optional<Team>>::Storage{Team::CounterTerrorists, Team::Terrorists,
                                                                Team::CounterTerrorists, Team::Terrorists}};

constexpr float kSecondsPerMinute = 60.0F;

[[nodiscard]] auto isLength(nx::Seconds length) noexcept -> bool {
    return std::isfinite(length.count()) && length >= nx::Seconds{};
}

[[nodiscard]] auto areLengths(const RoundLengths& lengths) noexcept -> bool {
    return isLength(lengths.freeze) && isLength(lengths.round);
}

/// The whole seconds of `time`, cut, none below 0, the most a std::uint32_t holds above it.
[[nodiscard]] auto wholeSecondsOf(nx::Seconds time) noexcept -> std::uint32_t {
    return nx::tryRound<std::uint32_t>(std::trunc(std::max(time.count(), 0.0F)))
        .value_or(std::numeric_limits<std::uint32_t>::max());
}

} // namespace

auto objectiveOfClass(std::string_view classname) noexcept -> std::optional<MapObjective> {
    const auto found = std::ranges::find(kObjectiveClasses, classname, &ObjectiveClass::classname);
    return found != kObjectiveClasses.end() ? std::optional{found->objective} : std::nullopt;
}

auto winnerOf(RoundEndReason reason) noexcept -> std::optional<Team> {
    return kWinners[reason];
}

auto timeOutReasonOf(MapObjectives objectives) noexcept -> std::optional<RoundEndReason> {
    const auto found = std::ranges::find_if(
        kTimeOuts, [objectives](const TimeOut& timeOut) { return objectives.test(timeOut.objective); });
    return found != kTimeOuts.end() ? std::optional{found->reason} : std::nullopt;
}

auto roundLengthsOf(float freezeSeconds, float roundMinutes) noexcept -> RoundLengths {
    return {.freeze = nx::Seconds{std::trunc(freezeSeconds)},
            .round = nx::Seconds{std::trunc(roundMinutes * kSecondsPerMinute)}};
}

auto RoundTimer::shownAt(nx::GameTime now, nx::TickRate rate) const noexcept -> std::uint32_t {
    return wholeSecondsOf(nx::Seconds{static_cast<float>(seconds)} - now.since(toldAt, rate));
}

Rounds::Rounds(MapObjectives objectives, const RoundLengths& lengths, nx::GameTime now) noexcept
    : m_objectives{objectives}, m_roundLength{lengths.round}, m_startedAt{now}, m_length{lengths.freeze} {}

auto Rounds::start(MapObjectives objectives, const RoundLengths& lengths, nx::GameTime now) noexcept
    -> Rounds {
    nx::expects(areLengths(lengths), "Rounds::start: lengths finite and not negative");
    Rounds rounds{objectives, lengths, now};
    rounds.m_length += kFirstFreezeExtra;
    return rounds;
}

auto Rounds::remainingAt(nx::GameTime now, nx::TickRate rate) const noexcept -> nx::Seconds {
    return m_length - now.since(m_startedAt, rate);
}

auto Rounds::timerAt(nx::GameTime now, nx::TickRate rate) const noexcept -> RoundTimer {
    return {.seconds = wholeSecondsOf(remainingAt(now, rate)), .toldAt = now};
}

void Rounds::restartGameIn(nx::Seconds delay, nx::GameTime now, nx::TickRate rate) noexcept {
    nx::expects(isLength(delay), "Rounds::restartGameIn: a delay finite and not negative");
    m_restartAt = now.after(delay, rate);
    m_restartsGame = true;
}

auto Rounds::think(nx::GameTime now, nx::TickRate rate, const RoundLengths& lengths) noexcept -> RoundEvents {
    nx::expects(areLengths(lengths), "Rounds::think: lengths finite and not negative");
    RoundEvents events;
    if (m_isFrozen) {
        if (remainingAt(now, rate) <= nx::Seconds{}) {
            m_isFrozen = false;
            m_startedAt = now;
            m_length = m_roundLength;
            events.set(RoundEvent::FreezeEnded);
        }
    } else if (!m_end && remainingAt(now, rate) <= nx::Seconds{}) {
        if (const std::optional<RoundEndReason> reason = timeOutReasonOf(m_objectives)) {
            m_end = reason;
            m_restartAt = now.after(kRestartDelay, rate);
            events.set(RoundEvent::Ended);
        }
        // A map played for nothing too: its round runs out again after its length and the grace.
        m_startedAt = now.after(kTimeOutGrace, rate);
    }
    if (m_restartAt && *m_restartAt <= now) {
        events.set(m_restartsGame ? RoundEvent::GameRestarted : RoundEvent::RoundStarted);
        *this = Rounds{m_objectives, lengths, now};
    }
    return events;
}

} // namespace cs
