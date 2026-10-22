// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Structured logging (docs/02-coding-standards.md, section 10).
///
/// Messages are std::format strings checked at compile time. Every record carries its level,
/// category, call site and time. Categories belong to the module that logs, so nx.core does not
/// know the engine's subsystems:
///
///     inline constexpr nx::log::Category kAssetsLog{"assets"};
///     nx::log::info(kAssetsLog, "loaded map {} in {}", name, elapsed);
export module nx.core:log;

import std;

export namespace nx::log {

/// Severity of a record, from the most verbose to the most severe.
enum class Level : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Count,
};

/// The subsystem a record comes from, a constant of the module that logs ("assets", "net"). Its
/// name also names the `log_level_<name>` cvar.
struct Category {
    std::string_view name;
};

/// One logged event, as sinks receive it. The views live only during Sink::write.
struct Record {
    Level level = Level::Info;
    std::string_view category{};
    std::string_view message{};
    std::source_location where{};
    std::chrono::system_clock::time_point time{};
};

/// Destination of log records. Logger serializes the calls it makes into its sinks.
class Sink {
public:
    Sink() = default;
    Sink(const Sink&) = delete;
    Sink(Sink&&) = delete;
    auto operator=(const Sink&) -> Sink& = delete;
    auto operator=(Sink&&) -> Sink& = delete;
    virtual ~Sink() = default;

    /// Writes one record; called under the logger's lock, one record at a time.
    virtual void write(const Record& record) noexcept = 0;
    /// Pushes what write() buffered to its destination.
    inline virtual void flush() noexcept {}
};

/// One human-readable line per record:
///     2026-10-20T21:03:11.123Z Warning [net] client 3 exceeded its budget (snapshot.cpp:88)
[[nodiscard]] auto formatLine(const Record& record) -> std::string;

/// Writes formatLine(record) to stderr.
class ConsoleSink final : public Sink {
public:
    void write(const Record& record) noexcept override;
    void flush() noexcept override;
};

/// Dispatches records to its sinks. Thread-safe; logging is not meant for hot paths (use
/// RateLimit there).
class Logger {
public:
    Logger() = default;
    /// `sinks` must not hold nullptr.
    explicit Logger(std::vector<std::unique_ptr<Sink>> sinks);

    /// Adds a sink, which must not be null.
    void addSink(std::unique_ptr<Sink> sink);

    /// Threshold for categories without their own level. Default: Level::Info.
    void setMinimumLevel(Level level);

    /// Threshold for one category, overriding the minimum level.
    void setCategoryLevel(Category category, Level level);

    /// The threshold that applies to `category`: its own level, or the minimum level.
    [[nodiscard]] auto categoryLevel(Category category) const -> Level;

    /// Whether a record of `level` in `category` would be written: the cheap test the logging
    /// functions make before formatting.
    [[nodiscard]] auto isEnabled(Category category, Level level) const -> bool;

    /// Hands a record to every sink, stamped with the time of the call.
    void write(Level level, Category category, std::string_view message, std::source_location where) noexcept;
    /// Flushes every sink.
    void flush() noexcept;

private:
    [[nodiscard]] auto thresholdFor(std::string_view category) const -> Level;

    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<Sink>> m_sinks;
    std::vector<std::pair<std::string, Level>> m_categoryLevels;
    Level m_minimumLevel = Level::Info;
};

/// Logger used by the logging functions below. Until an application installs its own, a
/// built-in logger writes Level::Info and above to the console. Like the contract handler, this
/// is process-wide by nature; DefaultLoggerScope installs one for a limited time.
[[nodiscard]] auto defaultLogger() noexcept -> Logger&;

/// Installs `logger` (not owned) and returns the previous one; `nullptr` restores the built-in
/// console logger. A thread that called defaultLogger() may still be writing through the previous
/// logger after the swap, so a logger must outlive every thread that may log while it is
/// installed: swap loggers at start-up, at shutdown and in tests, while no other thread logs.
auto setDefaultLogger(Logger* logger) noexcept -> Logger*;

/// Installs a logger for the lifetime of this object and restores the previous one afterwards.
class DefaultLoggerScope {
public:
    inline explicit DefaultLoggerScope(Logger& logger) noexcept : m_previous{setDefaultLogger(&logger)} {}
    inline ~DefaultLoggerScope() { setDefaultLogger(m_previous); }

    DefaultLoggerScope(const DefaultLoggerScope&) = delete;
    DefaultLoggerScope(DefaultLoggerScope&&) = delete;
    auto operator=(const DefaultLoggerScope&) -> DefaultLoggerScope& = delete;
    auto operator=(DefaultLoggerScope&&) -> DefaultLoggerScope& = delete;

private:
    Logger* m_previous;
};

/// A format string checked against its arguments at compile time, plus the call site.
template <class... Args>
struct FormatAt {
    std::format_string<Args...> format;
    std::source_location where;

    template <class Text>
        requires std::convertible_to<const Text&, std::string_view>
    consteval FormatAt(const Text& text, std::source_location location = std::source_location::current())
        : format{text}, where{location} {}
};

/// Logs through the default logger, formatting the message only when `level` is enabled for
/// `category`. trace, debug, info, warning and error below name the level.
template <class... Args>
void write(Level level, Category category, FormatAt<std::type_identity_t<Args>...> format, Args&&... args) {
    auto& logger = defaultLogger();
    if (logger.isEnabled(category, level)) {
        logger.write(level, category, std::format(format.format, std::forward<Args>(args)...), format.where);
    }
}

template <class... Args>
void trace(Category category, FormatAt<std::type_identity_t<Args>...> format, Args&&... args) {
    write(Level::Trace, category, format, std::forward<Args>(args)...);
}

template <class... Args>
void debug(Category category, FormatAt<std::type_identity_t<Args>...> format, Args&&... args) {
    write(Level::Debug, category, format, std::forward<Args>(args)...);
}

template <class... Args>
void info(Category category, FormatAt<std::type_identity_t<Args>...> format, Args&&... args) {
    write(Level::Info, category, format, std::forward<Args>(args)...);
}

template <class... Args>
void warning(Category category, FormatAt<std::type_identity_t<Args>...> format, Args&&... args) {
    write(Level::Warning, category, format, std::forward<Args>(args)...);
}

template <class... Args>
void error(Category category, FormatAt<std::type_identity_t<Args>...> format, Args&&... args) {
    write(Level::Error, category, format, std::forward<Args>(args)...);
}

/// Lets through at most one event per interval, for logging from code that runs every frame. It is
/// state of the system that logs, a member like any other (no static, no global):
///
///     if (m_budgetWarnings.allow()) {
///         nx::log::warning(kNetLog, "budget exceeded ({} suppressed)", m_budgetWarnings.takeSuppressed());
///     }
///
/// Thread-safe: concurrent callers never both pass within the same interval.
class RateLimit {
public:
    using Clock = std::chrono::steady_clock;

    inline explicit RateLimit(Clock::duration interval) noexcept : m_interval{interval} {}

    /// Whether the event at `now` may pass: the first one, and then one per interval. A rejected
    /// event is counted as suppressed.
    [[nodiscard]] auto allow(Clock::time_point now = Clock::now()) noexcept -> bool;

    /// Events rejected since the last call; resets the count.
    [[nodiscard]] auto takeSuppressed() noexcept -> std::uint64_t;

private:
    static constexpr Clock::rep kNever = std::numeric_limits<Clock::rep>::min();

    Clock::duration m_interval;
    std::atomic<Clock::rep> m_lastAllowed{kNever};
    std::atomic<std::uint64_t> m_suppressed{0};
};

} // namespace nx::log
