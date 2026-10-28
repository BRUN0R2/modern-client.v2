// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The blood a hit sheds behind it (docs/10, section 7.4), as the original's TraceBleed and
// UTIL_BloodDecalTrace shed it: the repository's data/cstrike/blood.toml and what its reader refuses,
// how many ways a hit traces by its damage and how far off its way, and one in three of them marked.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import cs.shared;

namespace {

/// Draws of the tests that count the marks: enough to see one in three.
constexpr std::uint32_t kDraws = 3000;

[[nodiscard]] auto repositoryText() -> std::string {
    return nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "blood.toml")
        .value_or(std::string{});
}

[[nodiscard]] auto repositoryRules() -> cs::BloodRules {
    auto rules = cs::readBloodRules(repositoryText(), "blood.toml");
    EXPECT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    return std::move(rules).value_or(cs::BloodRules{});
}

TEST(BloodTest, TheRepositorysBloodIsTraceBleeds) {
    const cs::BloodRules rules = repositoryRules();
    EXPECT_EQ(rules.decals,
              (std::vector<std::string>{"{blood1", "{blood2", "{blood3", "{blood4", "{blood5", "{blood6"}))
        << "DECAL_BLOOD1 to DECAL_BLOOD6";
    EXPECT_EQ(rules.reach, nx::HammerUnits{172.0F});
    EXPECT_EQ(rules.marksOneIn, 3U) << "RANDOM_LONG(0, 2)";
    ASSERT_EQ(rules.spreads.size(), 3U);
    EXPECT_EQ(rules.spreads.back().traces, 4U);
}

TEST(BloodTest, AHitTracesMoreWaysTheMoreItsDamageEachOffItsWayByItsNoise) {
    const cs::BloodRules rules = repositoryRules();
    nx::Random random{1, "blood"};
    const nx::Vec3 way{.x = 1.0F};
    for (const auto& [damage, ways, noise] :
         {std::tuple{9.9F, 1U, 0.1F}, std::tuple{10.0F, 2U, 0.2F}, std::tuple{24.9F, 2U, 0.2F},
          std::tuple{25.0F, 4U, 0.3F}, std::tuple{500.0F, 4U, 0.3F}}) {
        const std::vector<nx::Vec3> traced = cs::bleedingWaysOf(rules, nx::Damage{damage}, way, random);
        EXPECT_EQ(traced.size(), ways) << damage;
        for (const nx::Vec3 each : traced) {
            const nx::Vec3 off = each - way;
            EXPECT_LE(std::max({std::abs(off.x), std::abs(off.y), std::abs(off.z)}), noise) << damage;
        }
    }
}

TEST(BloodTest, OneInThreeWaysThatMeetWhatIsSolidLeavesADecalEachAsLikely) {
    const cs::BloodRules rules = repositoryRules();
    nx::Random random{2, "blood"};
    std::vector<std::uint32_t> counts(rules.decals.size());
    std::uint32_t marks = 0;
    for (std::uint32_t draw = 0; draw < kDraws; ++draw) {
        if (const std::optional<std::uint32_t> decal = cs::bloodDecalOf(rules, random)) {
            ASSERT_LT(*decal, rules.decals.size());
            ++counts[*decal];
            ++marks;
        }
    }
    EXPECT_NEAR(static_cast<double>(marks) / kDraws, 1.0 / 3.0, 0.03);
    EXPECT_TRUE(std::ranges::none_of(counts, [](std::uint32_t count) { return count == 0; })) << "each drawn";
}

TEST(BloodTest, SpreadsOutOfOrderNoDecalsOrNoneForAnyDamageAreRefused) {
    const std::string text = repositoryText();
    for (const auto& [line, changed] :
         {std::pair{"below = 25.0", "below = 5.0"}, std::pair{"marksOneIn = 3", "marksOneIn = 0"},
          std::pair{"reach = 172.0", "reach = 0.0"},
          std::pair{R"(decals = ["{blood1", "{blood2", "{blood3", "{blood4", "{blood5", "{blood6"])",
                    "decals = []"},
          std::pair{"traces = 4\nnoise = 0.3", "below = 50.0\ntraces = 4\nnoise = 0.3"},
          std::pair{"traces = 1", "traces = 0"}}) {
        std::string broken = text;
        const std::size_t at = broken.find(line);
        ASSERT_NE(at, std::string::npos) << line;
        broken.replace(at, std::string_view{line}.size(), changed);
        const auto rules = cs::readBloodRules(broken, "blood.toml");
        ASSERT_FALSE(rules.has_value()) << changed;
        EXPECT_EQ(rules.error().code, cs::BloodError::DataInvalid);
    }
}

} // namespace
