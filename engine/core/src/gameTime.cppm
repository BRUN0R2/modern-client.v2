// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The simulation's time (docs/07-simulation-and-physics.md, section 1): counted in whole ticks
/// since the map started and a fraction of the next one, so that it is as precise after a month of
/// uptime as at the start. The original's float seconds are not: after a day, one is 8 ms apart from
/// the next, almost a tick.
export module nx.core:gameTime;

import std;
import :contracts;
import :quantity;
import :units;

export namespace nx {

/// The unit of TickRate.
struct TickRateTag {
    static constexpr std::string_view kSuffix = "Hz";
};

/// How many ticks the simulation runs a second (`sv_tickrate`). Never 0.
using TickRate = Quantity<std::uint32_t, TickRateTag>;

/// `sv_tickrate`'s default, the classic servers' rate.
inline constexpr TickRate kDefaultTickRate{100};

/// How long a tick at `rate` lasts.
[[nodiscard]] constexpr auto tickLengthOf(TickRate rate) noexcept -> Seconds {
    expects(rate.value() != 0, "tickLengthOf: a rate of ticks");
    return rate.value() != 0 ? Seconds{1.0F / static_cast<float>(rate.value())} : Seconds{};
}

/// A moment of the simulation: `tick()` whole ticks after the map started, and `fraction()` of the
/// next, from 0 up to 1, at the rate the simulation runs at, which converting it from or to seconds
/// takes.
class GameTime {
public:
    /// When the map started.
    constexpr GameTime() noexcept = default;

    /// `tick` ticks after the map started (before it when negative), and `fraction` of the next: from
    /// 0, below 1.
    [[nodiscard]] static auto atTick(std::int64_t tick, float fraction = 0.0F) noexcept -> GameTime;

    [[nodiscard]] constexpr auto tick() const noexcept -> std::int64_t { return m_tick; }
    [[nodiscard]] constexpr auto fraction() const noexcept -> float { return m_fraction; }

    /// The same moment of the tick `ticks` ticks later.
    [[nodiscard]] constexpr auto ticksLater(std::int64_t ticks) const noexcept -> GameTime {
        GameTime later = *this;
        later.m_tick += ticks;
        return later;
    }

    /// `elapsed` later (earlier when negative), at `rate`: the whole ticks of the fraction carried
    /// into the count. A duration that is the float nearest to a whole number of ticks lasts that
    /// number exactly: 0.1 s is ten ticks at 100, not ten and a hair, which would land on the eleventh.
    [[nodiscard]] auto after(Seconds elapsed, TickRate rate) const noexcept -> GameTime;

    /// How long after `earlier` it is, at `rate`: negative when before it.
    [[nodiscard]] auto since(GameTime earlier, TickRate rate) const noexcept -> Seconds;

    [[nodiscard]] constexpr auto operator<=>(const GameTime&) const noexcept = default;

private:
    std::int64_t m_tick = 0;
    float m_fraction = 0.0F;
};

} // namespace nx
