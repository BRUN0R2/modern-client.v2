// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#if defined(_WIN32)
#include <windows.h> // WIN32_LEAN_AND_MEAN and NOMINMAX come from engine/platform/CMakeLists.txt
#endif

module nx.platform;

import std;
import nx.core;

namespace nx::platform {

void FrameRateMeter::frameStarted(Instant at) noexcept {
    if (!m_windowStart) {
        m_windowStart = at;
        return;
    }
    ++m_frames;
    const MonotonicClock::duration elapsed = at - *m_windowStart;
    if (elapsed < kFrameRateWindow) {
        return;
    }
    m_framesPerSecond = static_cast<double>(m_frames) / std::chrono::duration<double>{elapsed}.count();
    m_windowStart = at;
    m_frames = 0;
}

namespace {

/// The steps WakeLateness's margin moves by: kWakeLatenessStep shared so that, steady, the wakes later
/// than the margin are 1 - kWakeLatenessQuantile of them.
constexpr std::chrono::duration<double, std::micro> kLatenessStep{kWakeLatenessStep};
constexpr auto kLatenessRise =
    std::chrono::duration_cast<MonotonicClock::duration>(kLatenessStep * kWakeLatenessQuantile);
constexpr auto kLatenessFall =
    std::chrono::duration_cast<MonotonicClock::duration>(kLatenessStep * (1.0 - kWakeLatenessQuantile));

} // namespace

void WakeLateness::observe(MonotonicClock::duration late) noexcept {
    late = std::max(late, MonotonicClock::duration::zero());
    if (!m_isSeeded) {
        m_margin = late;
        m_isSeeded = true;
    } else if (late > m_margin) {
        m_margin += kLatenessRise;
    } else {
        m_margin = std::max(m_margin - kLatenessFall, MonotonicClock::duration::zero());
    }
}

void SleepTimer::sleepUntil(Instant deadline) noexcept {
    for (Instant now = MonotonicClock::now(); deadline - now > m_lateness.margin();
         now = MonotonicClock::now()) {
        const Instant wake = deadline - m_lateness.margin();
        sleepCoarselyUntil(wake);
        m_lateness.observe(MonotonicClock::now() - wake);
    }
    while (MonotonicClock::now() < deadline) {
        std::this_thread::yield(); // the core's other threads run meanwhile
    }
}

#if defined(_WIN32)

namespace {

/// The unit of Windows due times: 100 nanoseconds.
using DueTimeTicks = std::chrono::duration<LONGLONG, std::ratio_multiply<std::ratio<100>, std::nano>>;

} // namespace

auto SleepTimer::create() -> Result<SleepTimer, ClockError> {
    HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr) {
        return fail(
            ClockError::TimerUnavailable,
            std::format("CreateWaitableTimerExW (high resolution) failed with error {}", GetLastError()));
    }
    return SleepTimer{timer};
}

void SleepTimer::close() noexcept {
    if (m_timer != nullptr) {
        CloseHandle(m_timer);
        m_timer = nullptr;
    }
}

void SleepTimer::sleepCoarselyUntil(Instant wake) const noexcept {
    expects(m_timer != nullptr, "SleepTimer::sleepUntil: the timer was moved from");
    // Negative: relative to now. Never positive, which would be a date.
    const MonotonicClock::duration wait =
        std::max(wake - MonotonicClock::now(), MonotonicClock::duration::zero());
    LARGE_INTEGER dueTime{};
    dueTime.QuadPart = -std::chrono::ceil<DueTimeTicks>(wait).count();
    const BOOL armed = SetWaitableTimerEx(m_timer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
    invariant(armed != FALSE, "SleepTimer::sleepUntil: SetWaitableTimerEx failed on a valid timer");
    if (armed == FALSE) {
        return; // the handler let execution continue ("observe"): the clock waits the rest
    }
    WaitForSingleObject(m_timer, INFINITE);
}

#else

auto SleepTimer::create() -> Result<SleepTimer, ClockError> {
    return SleepTimer{nullptr};
}

void SleepTimer::close() noexcept {}

void SleepTimer::sleepCoarselyUntil(Instant wake) const noexcept {
    // An absolute sleep on the monotonic clock (clock_nanosleep in libstdc++); a signal that
    // interrupts it leaves the rest to sleepUntil.
    std::this_thread::sleep_until(wake);
}

#endif

} // namespace nx::platform
