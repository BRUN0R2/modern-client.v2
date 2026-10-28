// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.core;

import std;

namespace nx {

namespace {

/// The ticks `elapsed` lasts at `rate`: the whole number of them when `elapsed` is the float nearest to
/// it, as a duration written in seconds is (0.1 s is 0.100000001, ten ticks and a hair more at 100),
/// else its exact share of them.
[[nodiscard]] auto ticksIn(Seconds elapsed, TickRate rate) noexcept -> double {
    const auto perSecond = static_cast<double>(rate.value());
    const double ticks = double{elapsed.count()} * perSecond;
    const double nearest = std::round(ticks);
    return static_cast<float>(nearest / perSecond) == elapsed.count() ? nearest : ticks;
}

} // namespace

auto GameTime::atTick(std::int64_t tick, float fraction) noexcept -> GameTime {
    expects(fraction >= 0.0F && fraction < 1.0F, "GameTime::atTick: a fraction from 0, below 1");
    GameTime time;
    time.m_tick = tick;
    time.m_fraction = std::clamp(fraction, 0.0F, std::nextafter(1.0F, 0.0F));
    return time;
}

auto GameTime::after(Seconds elapsed, TickRate rate) const noexcept -> GameTime {
    expects(rate.value() != 0, "GameTime::after: a rate of ticks");
    const double ticks = double{m_fraction} + ticksIn(elapsed, rate);
    const double whole = std::floor(ticks);
    GameTime later;
    later.m_tick = m_tick + static_cast<std::int64_t>(whole);
    later.m_fraction = static_cast<float>(ticks - whole);
    if (later.m_fraction >= 1.0F) {
        // A fraction just below 1 that rounds up to it is the next tick's start.
        later.m_tick += 1;
        later.m_fraction = 0.0F;
    }
    return later;
}

auto GameTime::since(GameTime earlier, TickRate rate) const noexcept -> Seconds {
    expects(rate.value() != 0, "GameTime::since: a rate of ticks");
    if (rate.value() == 0) {
        return Seconds{};
    }
    const double ticks =
        static_cast<double>(m_tick - earlier.m_tick) + (double{m_fraction} - double{earlier.m_fraction});
    return Seconds{static_cast<float>(ticks / static_cast<double>(rate.value()))};
}

} // namespace nx
