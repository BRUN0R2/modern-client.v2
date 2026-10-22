// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The HUD: its pictures among hud.txt's entries, the numbers it shows of the player as the game makes
// them, and where it draws them, as the original draws them at 640x480, 1280x720 and 1920x1080
// (docs/12-ui-hud-and-console.md, section 3.2); and numbers of any size, none cut short (section 3.4).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.rhi;
import cs.shared;
import cs.client;

namespace {

using cs::HudMark;
using cs::HudPicture;

/// The HUD's entries of Counter-Strike 1.6's hud.txt, one for the small screens first.
constexpr std::string_view kHudList = R"(15
number_0     320 320hud2 0   0  12 16
number_0     640 640hud7 0   0  20 25
number_1     640 640hud7 23  0  20 25
number_2     640 640hud7 47  0  20 25
number_3     640 640hud7 70  0  20 25
number_4     640 640hud7 95  0  20 25
number_5     640 640hud7 119 0  20 25
number_6     640 640hud7 144 0  20 25
number_7     640 640hud7 169 0  20 25
number_8     640 640hud7 192 0  20 25
number_9     640 640hud7 216 0  20 25
cross        640 640hud7 48  25 24 24
suit_empty   640 640hud7 24  25 24 24
suit_full    640 640hud7 0   25 24 24
dollar       640 640hud7 192 25 18 25
)";

[[nodiscard]] auto sheet() -> cs::HudSheet {
    const auto entries = nx::assets::parseSpriteList(kHudList);
    EXPECT_TRUE(entries.has_value());
    auto made = cs::hudSheetOf(entries.value_or(std::vector<nx::assets::SpriteListEntry>{}));
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return made.value_or(cs::HudSheet{});
}

[[nodiscard]] auto digit(char value, std::int32_t x, std::int32_t y) -> HudMark {
    return {.picture = static_cast<HudPicture>(std::to_underlying(HudPicture::Number0) + (value - '0')),
            .x = x,
            .y = y};
}

/// The number the digits the HUD draws at `y`, from `x` on, spell, left to right.
[[nodiscard]] auto numberAt(std::span<const HudMark> marks, std::int32_t y, std::int32_t x = 0)
    -> std::string {
    std::vector<HudMark> digits;
    for (const HudMark& mark : marks) {
        if (mark.y == y && mark.x >= x && mark.picture <= HudPicture::Number9) {
            digits.push_back(mark);
        }
    }
    std::ranges::sort(digits, {}, &HudMark::x);
    return digits | std::views::transform([](const HudMark& mark) {
               return static_cast<char>('0' + std::to_underlying(mark.picture));
           }) |
           std::ranges::to<std::string>();
}

[[nodiscard]] auto find(std::span<const HudMark> marks, HudPicture picture) -> std::optional<HudMark> {
    const auto found = std::ranges::find(marks, picture, &HudMark::picture);
    return found == marks.end() ? std::nullopt : std::optional{*found};
}

TEST(HudTest, ThePicturesAreTheEntriesForScreens640Wide) {
    const cs::HudSheet pictures = sheet();
    EXPECT_EQ(pictures[HudPicture::Number0].sprite, "640hud7");
    EXPECT_EQ(pictures[HudPicture::Number0].rect,
              (nx::assets::SpriteRect{.x = 0, .y = 0, .width = 20, .height = 25}));
    EXPECT_EQ(pictures[HudPicture::Number9].rect.x, 216U);
    EXPECT_EQ(pictures[HudPicture::Dollar].rect,
              (nx::assets::SpriteRect{.x = 192, .y = 25, .width = 18, .height = 25}));

    const auto withoutDollar = nx::assets::parseSpriteList(kHudList.substr(0, kHudList.rfind("dollar")));
    ASSERT_TRUE(withoutDollar.has_value());
    const auto missing = cs::hudSheetOf(*withoutDollar);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, cs::HudError::PictureMissing);
    EXPECT_EQ(missing.error().detail, "dollar");
}

TEST(HudTest, HealthIsTheWholeNumberTheGameSends) {
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{100.0F}), "100");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{99.9F}), "99");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{1.5F}), "1");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{0.25F}), "1") << "alive never shows 0";
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{1.0F}), "1");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{0.0F}), "0");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{-20.0F}), "0");

    EXPECT_EQ(cs::armorDigits(nx::ArmorPoints{0.75F}), "0");
    EXPECT_EQ(cs::armorDigits(nx::ArmorPoints{100.0F}), "100");
    EXPECT_EQ(cs::armorDigits(nx::ArmorPoints{-1.0F}), "0");
    EXPECT_EQ(cs::moneyDigits(nx::Money{800}), "800");
    EXPECT_EQ(cs::moneyDigits(nx::Money{-5}), "0");
}

TEST(HudTest, NoNumberIsCutShort) {
    // docs/12 section 3.4: health 5000, 100000 and the type's maximum, armor above 100 and above 255,
    // money above 16000, all of their digits.
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{5000.0F}), "5000");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{100000.0F}), "100000");
    EXPECT_EQ(cs::healthDigits(nx::HitPoints{std::numeric_limits<float>::max()}),
              "340282346638528859811704183484516925440");
    EXPECT_EQ(cs::armorDigits(nx::ArmorPoints{300.0F}), "300");
    EXPECT_EQ(cs::armorDigits(nx::ArmorPoints{70000.0F}), "70000");
    EXPECT_EQ(cs::moneyDigits(nx::Money{16001}), "16001");
    EXPECT_EQ(cs::moneyDigits(nx::Money{std::numeric_limits<std::int32_t>::max()}), "2147483647");
}

TEST(HudTest, ThePlayerAsItSpawnsIsWhereTheOriginalDrawsIt) {
    // Measured at 1280x720 (docs/12 section 3.2): the cross at 10, 680 and "100" in room for three from
    // 36; the empty suit at 256, 683 and "0" at the right of its room; the dollar at 1146, 645 and
    // "800" at the right of room for five that ends 16 from the right edge.
    const std::vector<HudMark> marks =
        cs::layoutHud({.health = cs::kSpawnHealth, .armor = nx::ArmorPoints{}, .money = cs::kStartMoney},
                      sheet(), {.width = 1280, .height = 720});
    const std::vector<HudMark> expected{{.picture = HudPicture::Cross, .x = 10, .y = 680},
                                        digit('1', 36, 680),
                                        digit('0', 56, 680),
                                        digit('0', 76, 680),
                                        {.picture = HudPicture::SuitEmpty, .x = 256, .y = 683},
                                        digit('0', 320, 683),
                                        {.picture = HudPicture::Dollar, .x = 1146, .y = 645},
                                        digit('8', 1204, 645),
                                        digit('0', 1224, 645),
                                        digit('0', 1244, 645)};
    EXPECT_EQ(marks, expected);
}

TEST(HudTest, ArmorFillsTheSuitAsFarUpAsItReaches) {
    const cs::HudSheet pictures = sheet();
    const auto at = [&pictures](float armor) {
        return cs::layoutHud(
            {.health = cs::kSpawnHealth, .armor = nx::ArmorPoints{armor}, .money = cs::kStartMoney}, pictures,
            {.width = 1280, .height = 720});
    };
    // Measured with a vest: "100" fills the room after the suit, suit_full over suit_empty.
    const std::vector<HudMark> full = at(100.0F);
    EXPECT_EQ(find(full, HudPicture::SuitFull),
              (HudMark{.picture = HudPicture::SuitFull, .x = 256, .y = 683}));
    EXPECT_EQ(numberAt(full, 683), "100");
    EXPECT_TRUE(std::ranges::contains(full, digit('1', 280, 683)));

    EXPECT_EQ(find(at(50.0F), HudPicture::SuitFull),
              (HudMark{.picture = HudPicture::SuitFull, .x = 256, .y = 683 + 12, .cutTop = 12}));
    EXPECT_EQ(find(at(250.0F), HudPicture::SuitFull), find(full, HudPicture::SuitFull)) << "full is full";
    EXPECT_FALSE(find(at(0.5F), HudPicture::SuitFull).has_value()) << "less than a row";
}

TEST(HudTest, AtEveryResolutionTheHudKeepsToTheEdges) {
    const cs::HudSheet pictures = sheet();
    const cs::HudView spawned{
        .health = cs::kSpawnHealth, .armor = nx::ArmorPoints{}, .money = cs::kStartMoney};
    for (const auto& [width, height] : {std::pair{640, 480}, std::pair{1920, 1080}}) {
        const std::vector<HudMark> marks = cs::layoutHud(
            spawned, pictures,
            {.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height)});
        EXPECT_EQ(find(marks, HudPicture::Cross),
                  (HudMark{.picture = HudPicture::Cross, .x = 10, .y = height - 40}));
        EXPECT_EQ(numberAt(marks, height - 40), "100");
        EXPECT_EQ(find(marks, HudPicture::SuitEmpty),
                  (HudMark{.picture = HudPicture::SuitEmpty, .x = width / 5, .y = height - 37}));
        EXPECT_EQ(find(marks, HudPicture::Dollar),
                  (HudMark{.picture = HudPicture::Dollar, .x = width - 134, .y = height - 75}));
        EXPECT_EQ(numberAt(marks, height - 75), "800");
    }
}

TEST(HudTest, LongerNumbersWidenTheirRoomAndPushWhatIsBeside) {
    const cs::HudSheet pictures = sheet();
    const auto at = [&pictures](float health, float armor, std::int32_t money, std::uint32_t width) {
        return cs::layoutHud(
            {.health = nx::HitPoints{health}, .armor = nx::ArmorPoints{armor}, .money = nx::Money{money}},
            pictures, {.width = width, .height = 480});
    };
    // 5000 at 640 wide: its room grows to the right, and the suit keeps 32 pixels from it.
    const std::vector<HudMark> fiveThousand = at(5000.0F, 0.0F, 800, 640);
    EXPECT_EQ(numberAt(fiveThousand, 440), "5000");
    EXPECT_EQ(fiveThousand[1], digit('5', 36, 440));
    EXPECT_EQ(find(fiveThousand, HudPicture::SuitEmpty)->x, 36 + 80 + 32);

    const std::vector<HudMark> highest = at(std::numeric_limits<float>::max(), 0.0F, 800, 640);
    EXPECT_EQ(numberAt(highest, 440), "340282346638528859811704183484516925440");
    EXPECT_EQ(find(highest, HudPicture::SuitEmpty)->x, 36 + (39 * 20) + 32);
    EXPECT_EQ(numberAt(at(100000.0F, 0.0F, 800, 1280), 440), "100000");

    // Armor grows to the right of its room.
    const std::vector<HudMark> armor = at(100.0F, 70000.0F, 800, 1280);
    EXPECT_EQ(numberAt(armor, 443), "70000");
    EXPECT_TRUE(std::ranges::contains(armor, digit('7', 256 + 24, 443)));

    // Money grows to the left of its room, the dollar before it.
    const std::vector<HudMark> rich = at(100.0F, 0.0F, std::numeric_limits<std::int32_t>::max(), 1280);
    EXPECT_EQ(numberAt(rich, 405), "2147483647");
    EXPECT_EQ(find(rich, HudPicture::Dollar)->x, 1264 - 200 - 18);
    EXPECT_TRUE(std::ranges::contains(rich, digit('7', 1244, 405))) << "ends where it did";
}

} // namespace
