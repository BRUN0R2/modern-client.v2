// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

module;

// stderr and fputs: macros and C stdio are not exported by `import std;`.
#include <cstdio>

module nx.core;

import std;

namespace nx::log {

namespace {

[[nodiscard]] auto consoleSinks() -> std::vector<std::unique_ptr<Sink>> {
    std::vector<std::unique_ptr<Sink>> sinks;
    sinks.push_back(std::make_unique<ConsoleSink>());
    return sinks;
}

[[nodiscard]] auto builtinLogger() -> Logger& {
    static Logger logger{consoleSinks()};
    return logger;
}

// constinit: readable before any dynamic initializer logs.
constinit std::atomic<Logger*> installedLogger{nullptr};

} // namespace

auto formatLine(const Record& record) -> std::string {
    const auto time = std::chrono::floor<std::chrono::milliseconds>(record.time);
    return std::format("{:%FT%T}Z {} [{}] {} ({}:{})", time, enumName(record.level), record.category,
                       record.message, sourceFileName(record.where), record.where.line());
}

void ConsoleSink::write(const Record& record) noexcept {
    const auto line = formatLine(record) + '\n';
    std::fputs(line.c_str(), stderr);
}

void ConsoleSink::flush() noexcept {
    std::fflush(stderr);
}

Logger::Logger(std::vector<std::unique_ptr<Sink>> sinks) : m_sinks{std::move(sinks)} {}

void Logger::addSink(std::unique_ptr<Sink> sink) {
    expects(sink != nullptr, "Logger::addSink: sink must not be null");
    const std::scoped_lock lock{m_mutex};
    m_sinks.push_back(std::move(sink));
}

void Logger::setMinimumLevel(Level level) {
    const std::scoped_lock lock{m_mutex};
    m_minimumLevel = level;
}

void Logger::setCategoryLevel(Category category, Level level) {
    const std::scoped_lock lock{m_mutex};
    const auto existing =
        std::ranges::find(m_categoryLevels, category.name, &std::pair<std::string, Level>::first);
    if (existing != m_categoryLevels.end()) {
        existing->second = level;
    } else {
        m_categoryLevels.emplace_back(std::string{category.name}, level);
    }
}

auto Logger::isEnabled(Category category, Level level) const -> bool {
    const std::scoped_lock lock{m_mutex};
    return level >= thresholdFor(category.name);
}

auto Logger::thresholdFor(std::string_view category) const -> Level {
    const auto existing =
        std::ranges::find(m_categoryLevels, category, &std::pair<std::string, Level>::first);
    return existing != m_categoryLevels.end() ? existing->second : m_minimumLevel;
}

void Logger::write(Level level, Category category, std::string_view message,
                   std::source_location where) noexcept {
    const Record record{
        .level = level,
        .category = category.name,
        .message = message,
        .where = where,
        .time = std::chrono::system_clock::now(),
    };
    const std::scoped_lock lock{m_mutex};
    for (const auto& sink : m_sinks) {
        sink->write(record);
    }
}

void Logger::flush() noexcept {
    const std::scoped_lock lock{m_mutex};
    for (const auto& sink : m_sinks) {
        sink->flush();
    }
}

auto defaultLogger() noexcept -> Logger& {
    // acquire: pairs with the release half of the exchange in setDefaultLogger.
    auto* const installed = installedLogger.load(std::memory_order_acquire);
    return installed != nullptr ? *installed : builtinLogger();
}

auto setDefaultLogger(Logger* logger) noexcept -> Logger* {
    // acq_rel: publishes the new logger and observes the one it replaces.
    return installedLogger.exchange(logger, std::memory_order_acq_rel);
}

auto RateLimit::allow(Clock::time_point now) noexcept -> bool {
    const auto nowTicks = now.time_since_epoch().count();
    // relaxed: the timestamp guards only itself; no other data is published through it.
    auto last = m_lastAllowed.load(std::memory_order_relaxed);
    while (last == kNever || nowTicks - last >= m_interval.count()) {
        if (m_lastAllowed.compare_exchange_weak(last, nowTicks, std::memory_order_relaxed)) {
            return true;
        }
    }
    m_suppressed.fetch_add(1, std::memory_order_relaxed);
    return false;
}

auto RateLimit::takeSuppressed() noexcept -> std::uint64_t {
    return m_suppressed.exchange(0, std::memory_order_relaxed);
}

} // namespace nx::log
