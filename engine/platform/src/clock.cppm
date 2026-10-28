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

/// The share of each frame's time in FrameRateMeter's average, the rest the average before it: the
/// original's net_graph's (FRAMERATE_AVG_FRAC).
inline constexpr double kFrameTimeShare = 0.5;
/// The longest a frame counts for in FrameRateMeter's average: as long as the original's Host_FilterTime
/// lets a frame's time be (host_frametime).
inline constexpr std::chrono::milliseconds kLongestFrameTime{250};

/// The frames a second the client shows, measured as the original's net_graph measures the rate it
/// writes, which changes with every frame (docs/05-rendering.md, section 12): an average of the frames'
/// times, each frame's moving it kFrameTimeShare of the way to its own, from none. One measure for
/// whatever shows the frame rate, cl_showfps and net_graph alike, so that they never tell two rates as
/// the original's do.
class FrameRateMeter {
public:
    /// A frame started `at`, no sooner than the one before: the time since that one, held to
    /// kLongestFrameTime, moves the average. The first frame only starts the count.
    void frameStarted(Instant at) noexcept;

    /// The frames a second of the average frame's time: none before a frame's time is counted.
    [[nodiscard]] auto framesPerSecond() const noexcept -> std::optional<double>;

private:
    std::optional<Instant> m_lastStart;                 ///< None before the first frame.
    std::chrono::duration<double> m_averageFrameTime{}; ///< None counted while zero.
};

/// The share of the OS's wakes that WakeLateness expects no later than its margin.
inline constexpr double kWakeLatenessQuantile = 0.99;
/// How far one wake moves WakeLateness's margin: up by this share of the quantile when the wake was
/// later than the margin, down by the rest of it when it was not.
inline constexpr std::chrono::microseconds kWakeLatenessStep{20};

/// How late the OS wakes a thread whose timer is due, as a running estimate of the kWakeLatenessQuantile
/// quantile of what the wakes were late by (deterministic frugal streaming): the first wake sets it;
/// each one after moves it a step toward the quantile, so that a wake far later than the others, a
/// thread the OS put aside, moves it no more than one step.
class WakeLateness {
public:
    /// A wake `late` after the time its timer was due; one that came early counts as on time.
    void observe(MonotonicClock::duration late) noexcept;

    /// How long before a deadline to be woken so as to be awake by it, but one wake in a hundred: none
    /// before a wake is observed.
    [[nodiscard]] inline auto margin() const noexcept -> MonotonicClock::duration { return m_margin; }

private:
    MonotonicClock::duration m_margin{};
    bool m_isSeeded = false; ///< Whether a wake was observed.
};

/// Why a SleepTimer could not be created.
enum class ClockError : std::uint8_t {
    TimerUnavailable, ///< The OS has no high-resolution timer (Windows before 10 1803).
};

/// Waits for deadlines with microsecond precision: the pace of the server tick (sv_tickrate) and of
/// the client frame (fps_max). No OS timer is that precise: Windows' high-resolution waitable timer,
/// which this owns there because the standard sleep rounds to the scheduler period (15.6 ms by
/// default), still wakes its thread 0.4 to 0.8 ms late on some systems whatever the wait, and Linux's
/// absolute sleep some tens of microseconds late. So the OS's timer waits for all but the last
/// stretch, which ends as much before the deadline as the OS was seen to be late (WakeLateness), and
/// the clock is read until the deadline for that stretch, the thread yielding its core between
/// reads (docs/05-rendering.md, section 12).
///
/// For the threads that pace a loop, never for jobs (docs/02-coding-standards.md, section 8). One
/// per thread: sleepUntil is not meant to be called by two threads at once.
class SleepTimer {
public:
    /// Errors: TimerUnavailable.
    [[nodiscard]] static auto create() -> Result<SleepTimer, ClockError>;

    inline SleepTimer(SleepTimer&& other) noexcept
        : m_timer{std::exchange(other.m_timer, nullptr)}, m_lateness{other.m_lateness} {}
    inline auto operator=(SleepTimer&& other) noexcept -> SleepTimer& {
        if (this != &other) {
            close();
            m_timer = std::exchange(other.m_timer, nullptr);
            m_lateness = other.m_lateness;
        }
        return *this;
    }
    SleepTimer(const SleepTimer&) = delete;
    auto operator=(const SleepTimer&) -> SleepTimer& = delete;
    inline ~SleepTimer() { close(); }

    /// Returns at `deadline` or after it, never before, and late by no more than the clock's reads
    /// take but when the OS gives the thread's core to another for longer. Returns at once for a
    /// deadline that has passed.
    void sleepUntil(Instant deadline) noexcept;

private:
    inline explicit SleepTimer(void* timer) noexcept : m_timer{timer} {}
    void close() noexcept;
    /// Waits on the OS's timer until `wake` or after it, as late as the OS wakes the thread.
    void sleepCoarselyUntil(Instant wake) const noexcept;

    void* m_timer = nullptr; // Windows: the waitable timer handle; null elsewhere
    WakeLateness m_lateness;
};

} // namespace nx::platform
