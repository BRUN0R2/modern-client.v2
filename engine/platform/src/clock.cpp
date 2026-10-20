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

void SleepTimer::sleepUntil(Instant deadline) const noexcept {
    expects(m_timer != nullptr, "SleepTimer::sleepUntil: the timer was moved from");
    // The timer and the monotonic clock may disagree by a fraction of a tick: wait again until the
    // clock agrees that the deadline has passed.
    for (Instant now = MonotonicClock::now(); now < deadline; now = MonotonicClock::now()) {
        LARGE_INTEGER dueTime{};
        dueTime.QuadPart = -std::chrono::ceil<DueTimeTicks>(deadline - now).count(); // negative: relative
        const BOOL armed = SetWaitableTimerEx(m_timer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
        invariant(armed != FALSE, "SleepTimer::sleepUntil: SetWaitableTimerEx failed on a valid timer");
        if (armed == FALSE) {
            return; // the handler let execution continue ("observe")
        }
        WaitForSingleObject(m_timer, INFINITE);
    }
}

#else

auto SleepTimer::create() -> Result<SleepTimer, ClockError> {
    return SleepTimer{nullptr};
}

void SleepTimer::close() noexcept {}

void SleepTimer::sleepUntil(Instant deadline) const noexcept {
    // An absolute sleep on the monotonic clock (clock_nanosleep in libstdc++), resumed if a signal
    // interrupts it.
    while (MonotonicClock::now() < deadline) {
        std::this_thread::sleep_until(deadline);
    }
}

#endif

} // namespace nx::platform
