// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.meta;
import nx.data;

namespace nx::test {

enum class Side : std::uint8_t {
    Terrorists,
    CounterTerrorists,
    Count,
};

struct Clip {
    std::string ammo;
    std::uint32_t size = 0;
};

struct Gun {
    std::string name;
    bool isAutomatic = false;
    float spread = 0.0F;
    Side side = Side::Terrorists;
    nx::Money price{};
    nx::Seconds cycle{};
    std::vector<std::uint32_t> sequences;
    std::optional<Clip> clip;
    Clip spare;
    std::optional<std::array<float, 3>> tint;
};

} // namespace nx::test

template <>
struct nx::meta::Description<nx::test::Clip> {
    static constexpr std::string_view kName = "nx::test::Clip";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::test::Clip::ammo>("ammo"),
        nx::meta::field<&nx::test::Clip::size>("size", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<nx::test::Gun> {
    static constexpr std::string_view kName = "nx::test::Gun";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::test::Gun::name>("name"),
        nx::meta::field<&nx::test::Gun::isAutomatic>("isAutomatic"),
        nx::meta::field<&nx::test::Gun::spread>("spread", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&nx::test::Gun::side>("side"),
        nx::meta::field<&nx::test::Gun::price>("price"),
        nx::meta::field<&nx::test::Gun::cycle>("cycle"),
        nx::meta::field<&nx::test::Gun::sequences>("sequences"),
        nx::meta::field<&nx::test::Gun::clip>("clip"),
        nx::meta::field<&nx::test::Gun::spare>("spare"),
        nx::meta::field<&nx::test::Gun::tint>("tint", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
    };
};

namespace {

using nx::data::DataError;
using nx::data::FieldText;
using nx::test::Gun;

constexpr std::string_view kConsole = "console";

auto gun() -> Gun {
    return Gun{.name = "AK-47", .spread = 0.5F, .spare = {.ammo = "762Nato", .size = 30}};
}

auto set(Gun& value, std::string_view keyPath, std::string_view text) -> nx::Result<void, DataError> {
    return nx::data::assignField(value, keyPath, text, kConsole);
}

auto get(const Gun& value, std::string_view keyPath) -> std::optional<std::string> {
    auto written = nx::data::formatField(value, keyPath, kConsole);
    EXPECT_TRUE(written.has_value()) << nx::describe(written.error());
    return written ? *written : std::nullopt;
}

TEST(Fields, SetEveryKindOfValueAsADataFileReadsIt) {
    Gun value = gun();
    ASSERT_TRUE(set(value, "name", "Drakar III")) << "text as it is, spaces and all";
    ASSERT_TRUE(set(value, "isAutomatic", "true"));
    ASSERT_TRUE(set(value, "spread", "0.25"));
    ASSERT_TRUE(set(value, "side", "counterTerrorists")) << "an enumerator by its data spelling";
    ASSERT_TRUE(set(value, "price", "2500"));
    ASSERT_TRUE(set(value, "cycle", "0.08"));
    ASSERT_TRUE(set(value, "sequences", "[1, 2]"));
    ASSERT_TRUE(set(value, "spare.size", "45"));
    ASSERT_TRUE(set(value, "tint", "[0.25, 0.5, 1.0]")) << "an array of a fixed length";

    EXPECT_EQ(value.name, "Drakar III");
    EXPECT_TRUE(value.isAutomatic);
    EXPECT_EQ(value.spread, 0.25F);
    EXPECT_EQ(value.side, nx::test::Side::CounterTerrorists);
    EXPECT_EQ(value.price, nx::Money{2500});
    EXPECT_EQ(value.cycle, nx::Seconds{0.08F});
    EXPECT_EQ(value.sequences, (std::vector<std::uint32_t>{1, 2}));
    EXPECT_EQ(value.spare.size, 45U);
    EXPECT_EQ(value.spare.ammo, "762Nato");
    EXPECT_EQ(value.tint, (std::array{0.25F, 0.5F, 1.0F}));
}

TEST(Fields, AnAbsentTableStartsFromItsDefaults) {
    Gun value = gun();
    ASSERT_TRUE(set(value, "clip.size", "20"));
    ASSERT_TRUE(value.clip.has_value());
    EXPECT_EQ(value.clip->size, 20U);
    EXPECT_EQ(value.clip->ammo, "");

    ASSERT_TRUE(set(value, "clip", R"({ ammo = "9mm", size = 13 })")) << "a table set whole";
    EXPECT_EQ(value.clip->ammo, "9mm");
    EXPECT_EQ(value.clip->size, 13U);
}

struct Case {
    std::string_view description;
    std::string_view keyPath;
    std::string_view text;
    DataError code;
    std::string_view detail;
};

TEST(Fields, AFailureIsTypedAndLeavesTheValueAsItWas) {
    const std::vector<Case> cases{
        {"unknown key", "spred", "0.1", DataError::UnknownKey, R"(console: unknown key "spred")"},
        {"unknown nested key", "spare.ammunition", "9mm", DataError::UnknownKey,
         R"(console: spare: unknown key "ammunition")"},
        {"past a value", "name.first", "AK", DataError::UnknownKey, R"(console: "name" holds no keys)"},
        {"range", "spread", "2", DataError::OutOfRange,
         "console: spread: expected a number in [0, 1], got 2"},
        {"wrong type", "price", "true", DataError::WrongType,
         "console: price: expected a whole number, got a boolean"},
        {"syntax", "isAutomatic", "yes", DataError::Syntax, "console:1:"},
        {"enumerator", "side", "Terrorists", DataError::UnknownName,
         R"(console: side: expected one of "terrorists", "counterTerrorists", got "Terrorists")"},
        {"table without a key", "clip", R"({ ammo = "9mm" })", DataError::MissingKey,
         R"(console: clip: missing key "size")"},
        {"absent table, bad value", "clip.size", "0", DataError::OutOfRange,
         "console: clip.size: expected a whole number of at least 1, got 0"},
        {"text past the value", "price", "1\nother = 2", DataError::UnknownKey,
         "console: price: more than one value"},
        {"fixed length", "tint", "[0.25, 0.5]", DataError::WrongLength,
         "console: tint: expected 3 elements, got 2"},
        {"fixed array element", "tint", "[0.25, 0.5, 2]", DataError::OutOfRange,
         "console: tint[2]: expected a number in [0, 1], got 2"},
    };
    for (const Case& problem : cases) {
        SCOPED_TRACE(problem.description);
        Gun value = gun();
        const auto set = nx::data::assignField(value, problem.keyPath, problem.text, kConsole);
        ASSERT_FALSE(set.has_value());
        EXPECT_EQ(set.error().code, problem.code);
        EXPECT_TRUE(set.error().detail.starts_with(problem.detail)) << set.error().detail;
        EXPECT_EQ(nx::data::listFields(value), nx::data::listFields(gun()));
    }
}

TEST(Fields, WriteAFieldAsADataFileWritesIt) {
    Gun value = gun();
    value.name = R"(The "Drakar" \ III)";
    value.sequences = {3, 4};
    value.tint = {0.25F, 0.5F, 1.0F};
    EXPECT_EQ(get(value, "name"), R"("The \"Drakar\" \\ III")");
    EXPECT_EQ(get(value, "spread"), "0.5");
    EXPECT_EQ(get(value, "side"), R"("terrorists")");
    EXPECT_EQ(get(value, "sequences"), "[3, 4]");
    EXPECT_EQ(get(value, "tint"), "[0.25, 0.5, 1]");
    EXPECT_EQ(get(value, "spare"), R"({ ammo = "762Nato", size = 30 })");
    EXPECT_EQ(get(value, "spare.size"), "30");
    EXPECT_EQ(get(value, "clip"), std::nullopt) << "an absent table";
    EXPECT_EQ(get(value, "clip.size"), std::nullopt) << "within an absent table";

    const auto unknown = nx::data::formatField(value, "spare.color", kConsole);
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error().code, DataError::UnknownKey);
}

TEST(Fields, WhatIsWrittenReadsBackTheSame) {
    Gun value = gun();
    value.name = "Line\nbreak\t\"quoted\"";
    value.clip = nx::test::Clip{.ammo = "9mm", .size = 17};
    value.cycle = nx::Seconds{0.0955F};
    value.tint = {0.1F, 0.2F, 0.3F};
    Gun copy{};
    for (const FieldText& field : nx::data::listFields(value)) {
        ASSERT_TRUE(set(copy, field.keyPath, field.value)) << field.keyPath << " = " << field.value;
    }
    EXPECT_EQ(copy.name, value.name);
    EXPECT_EQ(copy.side, value.side);
    EXPECT_EQ(copy.cycle, value.cycle);
    EXPECT_EQ(copy.tint, value.tint);
    EXPECT_EQ(nx::data::listFields(copy), nx::data::listFields(value));
}

TEST(Fields, ListEveryFieldThatHoldsAValueByItsKeyPath) {
    const std::vector<FieldText> expected{
        {.keyPath = "name", .value = R"("AK-47")"}, {.keyPath = "isAutomatic", .value = "false"},
        {.keyPath = "spread", .value = "0.5"},      {.keyPath = "side", .value = R"("terrorists")"},
        {.keyPath = "price", .value = "0"},         {.keyPath = "cycle", .value = "0"},
        {.keyPath = "sequences", .value = "[]"},    {.keyPath = "spare.ammo", .value = R"("762Nato")"},
        {.keyPath = "spare.size", .value = "30"},
    };
    EXPECT_EQ(nx::data::listFields(gun()), expected) << "the absent clip left out";
}

} // namespace
