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

TEST(BloodTest, ASpraysSizeIsItsDamagesWholePointsDoubledOverTenFromThreeToSixteen) {
    const cs::BloodSprayRules spray = repositoryRules().spray;
    EXPECT_EQ(spray.color, 247U) << "BLOOD_COLOR_RED";
    for (const auto& [damage, size] :
         {std::pair{0.9F, std::optional<std::uint32_t>{}}, std::pair{1.0F, std::optional<std::uint32_t>{3}},
          std::pair{15.9F, std::optional<std::uint32_t>{3}},
          std::pair{35.0F, std::optional<std::uint32_t>{7}},
          std::pair{60.0F, std::optional<std::uint32_t>{12}},
          std::pair{100.0F, std::optional<std::uint32_t>{16}},
          std::pair{1000.0F, std::optional<std::uint32_t>{16}}}) {
        EXPECT_EQ(cs::spraySizeOf(spray, nx::Damage{damage}), size) << damage;
    }
}

TEST(BloodTest, ASpraysLookIsRBloodSprites) {
    const cs::BloodSprayLook look = repositoryRules().sprayLook;
    EXPECT_EQ(look.spraySprite, "sprites/bloodspray.spr");
    EXPECT_EQ(look.dropSprite, "sprites/blood.spr");
    EXPECT_EQ(look.colorShift, 3U);
    EXPECT_EQ(look.amount, 250U);
    EXPECT_EQ(look.sprayTime, nx::Seconds{0.25F});
    EXPECT_EQ(look.dropGravity, nx::Scale{0.5F}) << "FTENT_SLOWGRAVITY";
    EXPECT_EQ(look.mostDropFrame, 8U);
}

TEST(BloodTest, AHeadsStreamIsItsDamageTimesFiveAndUpToAHundredMoreAByteAtMost) {
    const cs::BloodStreamRules stream = repositoryRules().stream;
    EXPECT_EQ(stream.color, 223U) << "BLOOD_COLOR_DARKRED";
    nx::Random random{1, "stream"};
    // 26.9 times 5 is 134.5, cut to 134; 40 times 5 and 100 more pass 255.
    for (const auto& [damage, slowest, fastest] :
         {std::tuple{0.0F, 0U, 100U}, std::tuple{10.0F, 50U, 150U}, std::tuple{26.9F, 134U, 234U},
          std::tuple{40.0F, 200U, 255U}, std::tuple{144.0F, 255U, 255U}}) {
        std::uint32_t least = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t most = 0;
        for (std::uint32_t draw = 0; draw < kDraws; ++draw) {
            const std::uint32_t speed = cs::streamSpeedOf(stream, nx::Damage{damage}, random);
            least = std::min(least, speed);
            most = std::max(most, speed);
        }
        EXPECT_EQ(least, slowest) << damage;
        EXPECT_EQ(most, fastest) << damage;
    }
}

TEST(BloodTest, AStreamsLookIsRBloodStreams) {
    const cs::BloodStreamLook look = repositoryRules().streamLook;
    EXPECT_EQ(look.colorRange, 10U) << "RandomLong(0, 9)";
    EXPECT_EQ(look.streamParticles, 100U);
    EXPECT_EQ(look.streamGravity, nx::Scale{0.4F}) << "pt_8x_slowgrav";
    EXPECT_EQ(look.splashGravity, nx::Scale{0.2F}) << "pt_4x_slowgrav";
    EXPECT_EQ(look.streamLife, nx::Seconds{2.0F});
    EXPECT_EQ(look.splashLife, nx::Seconds{3.0F});
    EXPECT_EQ(look.splashPerSpeed, 5U);
    EXPECT_EQ(look.splashCompanions, 2U);
}

TEST(BloodTest, TheEnhancedLooksBloodGushesMoreFromAHeadThanFromAnywhereElse) {
    const cs::BloodEnhancedLook look = repositoryRules().enhancedLook;
    EXPECT_EQ(look.mostDroplets, 1024U);
    EXPECT_EQ(look.mostSplats, 192U);
    EXPECT_GT(look.dropletLife, look.dropletFade) << "fading out in the air";
    EXPECT_GT(look.drag, 1.0F) << "the air stops the spray a short way off";
    EXPECT_GT(look.splatLife, look.splatFade);
    EXPECT_GT(look.head.dropletsPerSize, look.body.dropletsPerSize) << "more droplets";
    EXPECT_GT(look.head.mists, look.body.mists) << "more mist";
    EXPECT_GT(look.head.mostSpeed, look.body.mostSpeed) << "faster";
    EXPECT_EQ(look.body.gushPulses, 0U) << "a hit elsewhere does not gush";
    EXPECT_EQ(look.head.gushPulses, 6U);
    EXPECT_EQ(look.head.gushDroplets, 10U);
    EXPECT_GT(look.head.mistSpeed, look.body.mistSpeed) << "its mist flies farther";
    EXPECT_LT(look.head.gushSlowing, 1.0F) << "each pulse slower";
}

TEST(BloodTest, SpreadsOutOfOrderNoDecalsOrNoneForAnyDamageAreRefused) {
    const std::string text = repositoryText();
    for (const auto& [line, changed] :
         {std::pair{"below = 25.0", "below = 5.0"},
          std::pair{"marksOneIn = 3", "marksOneIn = 0"},
          std::pair{"reach = 172.0", "reach = 0.0"},
          std::pair{R"(decals = ["{blood1", "{blood2", "{blood3", "{blood4", "{blood5", "{blood6"])",
                    "decals = []"},
          std::pair{"traces = 4\nnoise = 0.3", "below = 50.0\ntraces = 4\nnoise = 0.3"},
          std::pair{"traces = 1", "traces = 0"},
          std::pair{"color = 247", "color = 254"},
          std::pair{"smallestSize = 3", "smallestSize = 17"},
          std::pair{"amountPerSize = 10", "amountPerSize = 0"},
          std::pair{"leastDropLife = 1.0", "leastDropLife = 4.0"},
          std::pair{"sprayTime = 0.25", "sprayTime = 0.0"},
          std::pair{R"(dropSprite = "sprites/blood.spr")", R"(dropSprite = "")"},
          std::pair{"fastest = 255", "fastest = 256"},
          std::pair{"colorRange = 10", "colorRange = 40"},
          std::pair{"streamLife = 2.0", "streamLife = 0.0"},
          std::pair{"splashPerSpeed = 5", "splashPerSpeed = 0"},
          std::pair{"mostDroplets = 1024", "mostDroplets = 0"},
          std::pair{"splatFade = 4.0", "splatFade = 20.0"},
          std::pair{"leastSplatGrowth = 2.0", "leastSplatGrowth = 4.0"},
          std::pair{"leastRadius = 0.25", "leastRadius = 0.0"},
          std::pair{"mistLife = 0.45", "mistLife = 0.0"},
          std::pair{"backShare = 0.4", "backShare = 1.5"},
          std::pair{"gushInterval = 0.1", "gushInterval = 0.0"},
          std::pair{"dropletFade = 0.35", "dropletFade = 0.9"},
          std::pair{"mistSpeed = 60.0", "mistSpeed = -1.0"},
          std::pair{"gushSlowing = 0.8", "gushSlowing = 1.5"}}) {
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
