// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Real-time clocks: monotonic time for frame and tick pacing, the wall clock, and a precise wait
/// for deadlines. Simulation time is not real time: it is counted in ticks (GameTime, docs/07).
export module nx.platform:clock;

import std;
import nx.core;

export namespace nx::platform {

/// Real time that only moves forward, unaffected by changes of the system date: frame and tick
/// pacing, timeouts, load times.
using MonotonicClock = std::chrono::steady_clock;
/// A point in MonotonicClock time.
using Instant = MonotonicClock::time_point;

/// Date and time (UTC): logs, demos, ban expiry. The one wall clock game code reads
/// (docs/02-coding-standards.md, section 4.1).
using WallClock = std::chrono::system_clock;

/// Measures real time from its start.
class Stopwatch {
public:
    /// Starts now.
    inline Stopwatch() noexcept : m_start{MonotonicClock::now()} {}

    /// The time elapsed since the start.
    [[nodiscard]] inline auto elapsed() const noexcept -> MonotonicClock::duration {
        return MonotonicClock::now() - m_start;
    }

    /// The time elapsed until now, which becomes the new start.
    inline auto restart() noexcept -> MonotonicClock::duration {
        const Instant now = MonotonicClock::now();
        return now - std::exchange(m_start, now);
    }

private:
    Instant m_start;
};

/// How long FrameRateMeter counts the frames of before it tells their rate again.
inline constexpr std::chrono::seconds kFrameRateWindow{1};

/// The frames a second the client shows, counted over each whole kFrameRateWindow
/// (docs/05-rendering.md, section 12): one measure for whatever shows the frame rate, cl_showfps and
/// net_graph alike, so that they never tell two rates as the original's do.
class FrameRateMeter {
public:
    /// A frame started `at`, no sooner than the one before. Once the frames since the window's first
    /// span kFrameRateWindow, their rate is told and a new window starts with this one.
    void frameStarted(Instant at) noexcept;

    /// The frames a second of the last window counted: the frames that started after its first, over
    /// the time from its first to its last. None until a window is counted.
    [[nodiscard]] inline auto framesPerSecond() const noexcept -> std::optional<double> {
        return m_framesPerSecond;
    }

private:
    std::optional<Instant> m_windowStart; ///< The start of the window's first frame; none before one.
    std::uint64_t m_frames = 0;           ///< Started since the window's first.
    std::optional<double> m_framesPerSecond;
};

/// Why a SleepTimer could not be created.
enum class ClockError : std::uint8_t {
    TimerUnavailable, ///< The OS has no high-resolution timer (Windows before 10 1803).
};

/// Waits for deadlines with sub-millisecond precision: the pace of the server tick (sv_tickrate)
/// and of the client frame (fps_max). The standard sleep is not enough on Windows, where it
/// rounds to the scheduler period (15.6 ms by default), so this owns a high-resolution waitable
/// timer there; on Linux the standard absolute sleep is already precise.
///
/// For the threads that pace a loop, never for jobs (docs/02-coding-standards.md, section 8). One
/// per thread: sleepUntil is not meant to be called by two threads at once.
class SleepTimer {
public:
    /// Errors: TimerUnavailable.
    [[nodiscard]] static auto create() -> Result<SleepTimer, ClockError>;

    inline SleepTimer(SleepTimer&& other) noexcept : m_timer{std::exchange(other.m_timer, nullptr)} {}
    inline auto operator=(SleepTimer&& other) noexcept -> SleepTimer& {
        if (this != &other) {
            close();
            m_timer = std::exchange(other.m_timer, nullptr);
        }
        return *this;
    }
    SleepTimer(const SleepTimer&) = delete;
    auto operator=(const SleepTimer&) -> SleepTimer& = delete;
    inline ~SleepTimer() { close(); }

    /// Returns at `deadline` or after it, never before; late by the wake-up latency of the OS
    /// (typically well under a millisecond). Returns at once for a deadline that has passed.
    void sleepUntil(Instant deadline) const noexcept;

private:
    inline explicit SleepTimer(void* timer) noexcept : m_timer{timer} {}
    void close() noexcept;

    void* m_timer = nullptr; // Windows: the waitable timer handle; null elsewhere
};

} // namespace nx::platform
