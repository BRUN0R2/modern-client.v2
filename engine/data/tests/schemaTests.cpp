// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.meta;
import nx.data;

namespace nx_test {

enum class SourceKind : std::uint8_t {
    Engine,
    DevContent,
    GoldsrcInstall,
    Count,
};

struct Mount {
    std::string directory;
    std::int32_t priority = 0;
};

struct Settings {
    std::string name;
    bool redistributable = false;
    std::uint8_t maxPlayers = 0;
    float volume = 0.0F;
    double gravity = 0.0;
    SourceKind kind = SourceKind::Engine;
    nx::Money startMoney{};
    nx::Seconds freezeTime{};
    std::optional<std::string> comment;
    std::vector<std::string> tags;
    Mount primary;
    std::vector<Mount> mounts;
};

struct Counter {
    std::int64_t value = 0;
};

} // namespace nx_test

template <>
struct nx::meta::Description<nx_test::Counter> {
    static constexpr std::string_view kName = "nx_test::Counter";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx_test::Counter::value>("value",
                                                  nx::data::Range{.maximum = 9'007'199'254'740'992.0}),
    };
};

template <>
struct nx::meta::Description<nx_test::Mount> {
    static constexpr std::string_view kName = "nx_test::Mount";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx_test::Mount::directory>("directory"),
        nx::meta::field<&nx_test::Mount::priority>("priority", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<nx_test::Settings> {
    static constexpr std::string_view kName = "nx_test::Settings";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx_test::Settings::name>("name"),
        nx::meta::field<&nx_test::Settings::redistributable>("redistributable"),
        nx::meta::field<&nx_test::Settings::maxPlayers>("maxPlayers",
                                                        nx::data::Range{.minimum = 1.0, .maximum = 64.0}),
        nx::meta::field<&nx_test::Settings::volume>("volume",
                                                    nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&nx_test::Settings::gravity>("gravity"),
        nx::meta::field<&nx_test::Settings::kind>("kind"),
        nx::meta::field<&nx_test::Settings::startMoney>("startMoney"),
        nx::meta::field<&nx_test::Settings::freezeTime>("freezeTime"),
        nx::meta::field<&nx_test::Settings::comment>("comment"),
        nx::meta::field<&nx_test::Settings::tags>("tags"),
        nx::meta::field<&nx_test::Settings::primary>("primary"),
        nx::meta::field<&nx_test::Settings::mounts>("mounts"),
    };
};

namespace {

using nx::data::DataError;
using nx_test::Settings;

constexpr std::string_view kValid = R"(name = "Modern server"
redistributable = true
maxPlayers = 32
volume = 0.75
gravity = 800
kind = "goldsrcInstall"
startMoney = 800
freezeTime = 6.5
tags = ["classic", "lan"]
primary = { directory = "cstrike", priority = 2 }

[[mounts]]
directory = "cstrike"
priority = 1

[[mounts]]
directory = "valve"
priority = 0
)";

/// kValid with the line that starts with `key` replaced by `line` (or appended when absent).
auto withLine(std::string_view key, std::string_view line) -> std::string {
    std::string text;
    bool replaced = false;
    for (const auto part : std::views::split(kValid, '\n')) {
        const std::string_view current{part.begin(), part.end()};
        if (!replaced && current.starts_with(key)) {
            text += line;
            replaced = true;
        } else {
            text += current;
        }
        text += '\n';
    }
    return replaced ? text : std::string{line} + "\n" + text;
}

auto parse(std::string_view text) -> nx::Result<Settings, DataError> {
    return nx::data::parseToml<Settings>(text, "server.toml");
}

TEST(Schema, ReadsEveryKindOfField) {
    const auto settings = parse(kValid);
    ASSERT_TRUE(settings.has_value()) << nx::describe(settings.error());

    EXPECT_EQ(settings->name, "Modern server");
    EXPECT_TRUE(settings->redistributable);
    EXPECT_EQ(settings->maxPlayers, 32);
    EXPECT_EQ(settings->volume, 0.75F);
    EXPECT_EQ(settings->gravity, 800.0) << "a whole number is a valid floating-point value";
    EXPECT_EQ(settings->kind, nx_test::SourceKind::GoldsrcInstall);
    EXPECT_EQ(settings->startMoney, nx::Money{800});
    EXPECT_EQ(settings->freezeTime, nx::Seconds{6.5F});
    EXPECT_FALSE(settings->comment.has_value());
    EXPECT_EQ(settings->tags, (std::vector<std::string>{"classic", "lan"}));
    EXPECT_EQ(settings->primary.directory, "cstrike");
    ASSERT_EQ(settings->mounts.size(), 2U);
    EXPECT_EQ(settings->mounts[1].directory, "valve");
}

TEST(Schema, OptionalFieldsReadTheirKeyWhenPresent) {
    const auto settings = parse(withLine("comment", R"(comment = "no bots")"));
    ASSERT_TRUE(settings.has_value()) << nx::describe(settings.error());
    EXPECT_EQ(settings->comment, std::optional<std::string>{"no bots"});
}

struct Case {
    std::string_view description;
    std::string text;
    DataError code;
    std::string_view detail;
};

TEST(Schema, EveryProblemIsATypedFailureWithItsPlace) {
    const std::vector<Case> cases{
        {"syntax", withLine("name", "name = "), DataError::Syntax, "server.toml:1:"},
        {"unknown key", withLine("gravity", "gravty = 800"), DataError::UnknownKey,
         R"(server.toml:5:1: unknown key "gravty"; expected one of "name", "redistributable", )"},
        {"missing key", withLine("name", ""), DataError::MissingKey,
         R"(server.toml:1:1: missing key "name")"},
        {"wrong type", withLine("maxPlayers", R"(maxPlayers = "32")"), DataError::WrongType,
         "server.toml:3:14: maxPlayers: expected a whole number, got text"},
        {"fraction in an integer", withLine("maxPlayers", "maxPlayers = 3.5"), DataError::WrongType,
         "server.toml:3:14: maxPlayers: expected a whole number, got a number with a fraction"},
        {"type limit", withLine("maxPlayers", "maxPlayers = 300"), DataError::OutOfRange,
         "server.toml:3:14: maxPlayers: expected a whole number in [0, 255], got 300"},
        {"range annotation", withLine("maxPlayers", "maxPlayers = 65"), DataError::OutOfRange,
         "server.toml:3:14: maxPlayers: expected a whole number in [1, 64], got 65"},
        {"float range", withLine("volume", "volume = 1.5"), DataError::OutOfRange,
         "server.toml:4:10: volume: expected a number in [0, 1], got 1.5"},
        {"not finite", withLine("gravity", "gravity = inf"), DataError::OutOfRange,
         "server.toml:5:11: gravity: inf is not a finite number of the field's precision"},
        {"unknown enumerator", withLine("kind", R"(kind = "GoldsrcInstall")"), DataError::UnknownName,
         R"(server.toml:6:8: kind: expected one of "engine", "devContent", "goldsrcInstall", got "GoldsrcInstall")"},
        {"nested table", withLine("primary", R"(primary = { directory = "cstrike", priority = -1 })"),
         DataError::OutOfRange,
         "server.toml:10:47: primary.priority: expected a whole number of at least 0, got -1"},
        {"array element", withLine("tags", R"(tags = ["classic", 3])"), DataError::WrongType,
         "server.toml:9:20: tags[1]: expected text, got a whole number"},
        {"array of tables", withLine("directory = \"valve\"", "directory = 7"), DataError::WrongType,
         "server.toml:17:13: mounts[1].directory: expected text, got a whole number"},
    };

    for (const Case& problem : cases) {
        SCOPED_TRACE(problem.description);
        const auto settings = parse(problem.text);
        ASSERT_FALSE(settings.has_value());
        EXPECT_EQ(settings.error().code, problem.code);
        EXPECT_TRUE(settings.error().detail.starts_with(problem.detail)) << settings.error().detail;
    }
}

TEST(Schema, ATypeLimitFailureKeepsTheConversionAsItsCause) {
    const auto settings = parse(withLine("maxPlayers", "maxPlayers = 300"));
    ASSERT_FALSE(settings.has_value());
    ASSERT_EQ(settings.error().cause.size(), 1U);
    EXPECT_EQ(settings.error().cause.records().front().domain, "ConversionError");
}

TEST(Schema, WholeNumberRangesCompareExactly) {
    // 2^53 + 1 is no double: compared as doubles, it would equal the bound 2^53 and pass.
    const auto exact = nx::data::parseToml<nx_test::Counter>("value = 9007199254740993", "counter.toml");
    ASSERT_FALSE(exact.has_value());
    EXPECT_EQ(exact.error().code, DataError::OutOfRange);
    EXPECT_TRUE(
        nx::data::parseToml<nx_test::Counter>("value = 9007199254740992", "counter.toml").has_value());
}

} // namespace
