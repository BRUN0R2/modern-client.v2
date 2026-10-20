// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/// Structured logging (docs/02-padroes-de-codigo.md, section 10).
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

enum class Level : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Count,
};

struct Category {
    std::string_view name;
};

struct Record {
    Level level = Level::Info;
    std::string_view category;
    std::string_view message;
    std::source_location where;
    std::chrono::system_clock::time_point time;
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

    virtual void write(const Record& record) noexcept = 0;
    virtual void flush() noexcept {}
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
    explicit Logger(std::vector<std::unique_ptr<Sink>> sinks);

    void addSink(std::unique_ptr<Sink> sink);

    /// Threshold for categories without their own level. Default: Level::Info.
    void setMinimumLevel(Level level);

    /// Threshold for one category, overriding the minimum level.
    void setCategoryLevel(Category category, Level level);

    [[nodiscard]] auto isEnabled(Category category, Level level) const -> bool;

    void write(Level level, Category category, std::string_view message, std::source_location where) noexcept;
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

/// Installs `logger` (not owned; must outlive its installation) and returns the previous one.
/// `nullptr` restores the built-in console logger.
auto setDefaultLogger(Logger* logger) noexcept -> Logger*;

/// Installs a logger for the lifetime of this object and restores the previous one afterwards.
class DefaultLoggerScope {
public:
    explicit DefaultLoggerScope(Logger& logger) noexcept : m_previous{setDefaultLogger(&logger)} {}
    ~DefaultLoggerScope() { setDefaultLogger(m_previous); }

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

/// Lets through at most one event per interval, for logging from code that runs every frame:
///
///     static nx::log::RateLimit budgetWarnings{std::chrono::seconds{1}};
///     if (budgetWarnings.allow()) { nx::log::warning(kNetLog, "..."); }
///
/// Thread-safe: concurrent callers never both pass within the same interval.
class RateLimit {
public:
    using Clock = std::chrono::steady_clock;

    explicit RateLimit(Clock::duration interval) noexcept : m_interval{interval} {}

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
