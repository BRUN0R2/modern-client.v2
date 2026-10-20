// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import std;
import nx.core;

namespace {

constexpr nx::log::Category kTestLog{"test"};
constexpr nx::log::Category kNoisyLog{"noisy"};

struct CapturedRecord {
    nx::log::Level level;
    std::string category;
    std::string message;
    std::uint_least32_t line;
};

/// Keeps copies of the records it receives; the record views die when write() returns.
class RecordingSink final : public nx::log::Sink {
public:
    explicit RecordingSink(std::vector<CapturedRecord>& records) noexcept : m_records{&records} {}

    void write(const nx::log::Record& record) noexcept override {
        m_records->push_back(CapturedRecord{
            .level = record.level,
            .category = std::string{record.category},
            .message = std::string{record.message},
            .line = record.where.line(),
        });
    }

private:
    std::vector<CapturedRecord>* m_records;
};

/// Logger with a recording sink, installed as the default for the lifetime of the fixture.
class LogTest : public testing::Test {
protected:
    LogTest() : m_scope{m_logger} { m_logger.addSink(std::make_unique<RecordingSink>(records)); }

    std::vector<CapturedRecord> records;
    nx::log::Logger m_logger;

private:
    nx::log::DefaultLoggerScope m_scope;
};

/// Counts how many times it is formatted.
struct FormatCounter {
    int* formatted;
};

} // namespace

template <>
struct std::formatter<FormatCounter> : std::formatter<int> {
    auto format(FormatCounter counter, std::format_context& context) const {
        return std::formatter<int>::format(++*counter.formatted, context);
    }
};

TEST_F(LogTest, FormatsTheMessageAndKeepsLevelCategoryAndCallSite) {
    const auto expectedLine = std::source_location::current().line() + 1;
    nx::log::info(kTestLog, "loaded {} in {} ms", "de_dust2", 12);

    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].level, nx::log::Level::Info);
    EXPECT_EQ(records[0].category, "test");
    EXPECT_EQ(records[0].message, "loaded de_dust2 in 12 ms");
    EXPECT_EQ(records[0].line, expectedLine);
}

TEST_F(LogTest, DropsRecordsBelowTheMinimumLevel) {
    nx::log::debug(kTestLog, "hidden");
    nx::log::warning(kTestLog, "shown");

    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].message, "shown");
}

TEST_F(LogTest, CategoryLevelsOverrideTheMinimum) {
    m_logger.setCategoryLevel(kNoisyLog, nx::log::Level::Error);
    m_logger.setCategoryLevel(kTestLog, nx::log::Level::Trace);

    nx::log::warning(kNoisyLog, "suppressed");
    nx::log::trace(kTestLog, "detail");

    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].message, "detail");
    EXPECT_TRUE(m_logger.isEnabled(kNoisyLog, nx::log::Level::Error));
}

TEST_F(LogTest, DoesNotFormatDisabledRecords) {
    int formatted = 0;

    nx::log::debug(kTestLog, "{}", FormatCounter{&formatted});
    EXPECT_EQ(formatted, 0);

    nx::log::info(kTestLog, "{}", FormatCounter{&formatted});
    EXPECT_EQ(formatted, 1);
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].message, "1");
}

TEST(Log, LinesCarryTimeLevelCategoryMessageAndLocation) {
    const nx::log::Record record{
        .level = nx::log::Level::Warning,
        .category = "net",
        .message = "client 3 exceeded its budget",
        .where = std::source_location::current(),
        .time = std::chrono::sys_days{std::chrono::year{2026} / 10 / 20} + std::chrono::hours{21} +
                std::chrono::minutes{3} + std::chrono::seconds{11} + std::chrono::milliseconds{123},
    };

    const auto line = nx::log::formatLine(record);

    EXPECT_TRUE(line.starts_with(
        "2026-10-20T21:03:11.123Z Warning [net] client 3 exceeded its budget (logTests.cpp:"))
        << line;
}

TEST(Log, DefaultLoggerScopeRestoresThePreviousLogger) {
    auto& before = nx::log::defaultLogger();
    {
        nx::log::Logger replacement;
        const nx::log::DefaultLoggerScope scope{replacement};
        EXPECT_EQ(&nx::log::defaultLogger(), &replacement);
    }
    EXPECT_EQ(&nx::log::defaultLogger(), &before);
}

TEST(RateLimit, AllowsOneEventPerInterval) {
    using namespace std::chrono_literals;
    nx::log::RateLimit limit{1s};
    const auto start = nx::log::RateLimit::Clock::time_point{} + 100s;

    EXPECT_TRUE(limit.allow(start));
    EXPECT_FALSE(limit.allow(start + 500ms));
    EXPECT_FALSE(limit.allow(start + 999ms));
    EXPECT_TRUE(limit.allow(start + 1s));
    EXPECT_EQ(limit.takeSuppressed(), 2U);
    EXPECT_EQ(limit.takeSuppressed(), 0U);
}
