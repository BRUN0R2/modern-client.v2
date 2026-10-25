// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The HUD: its pictures among hud.txt's entries, the numbers it shows of the player as the game makes
// them, and where it draws them, as the original draws them at 640x480, 1280x720 and 1920x1080
// (docs/12-ui-hud-and-console.md, section 3.2); numbers of any size, none cut short (section 3.4);
// and the crosshair, its cvars and where it is, as the original draws it from 640x480 to 1920x1080;
// the ammo of the weapon in hand, as the original draws a Glock's.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
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

/// A console's output that keeps nothing.
class SilentOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view /*line*/) override {}
    void reportFailure(const nx::Failure<nx::console::ConsoleError>& /*failure*/) override {}
};

/// No files.
class NoFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
    }
};

/// What the crosshair's left line says: how long it is and how far its end is from the middle.
struct Measured {
    std::uint32_t length = 0;
    std::uint32_t gap = 0;
};

[[nodiscard]] auto measured(cs::CrosshairSize size, std::uint32_t distance, std::uint32_t width,
                            std::uint32_t height) -> Measured {
    const std::array lines = cs::crosshairRects(size, distance, {.width = width, .height = height});
    const auto middle = static_cast<float>(width / 2);
    return {.length = static_cast<std::uint32_t>(lines[0].width),
            .gap = static_cast<std::uint32_t>(middle - (lines[0].x + lines[0].width - 1.0F))};
}

void expectRect(const nx::render::ScreenRect& line, const nx::render::ScreenRect& wanted,
                std::string_view which) {
    EXPECT_EQ(line.x, wanted.x) << which;
    EXPECT_EQ(line.y, wanted.y) << which;
    EXPECT_EQ(line.width, wanted.width) << which;
    EXPECT_EQ(line.height, wanted.height) << which;
}

TEST(AmmoHudTest, AGlocksAmmoIsWhereTheOriginalDrawsIt) {
    // Measured at 1280x720 (docs/12 section 3.2): the 9mm picture at W - 42, H - 40; "40" ending at it,
    // 37 from the bottom; the bar, 2 by 25, at W - 114; "20" ending ten before it.
    const cs::AmmoLayout layout =
        cs::layoutAmmo({.clip = 20, .reserve = 40}, sheet(), {.width = 1280, .height = 720});
    EXPECT_EQ(layout.pictureX, 1238);
    EXPECT_EQ(layout.pictureY, 680);
    const std::vector<HudMark> expected{digit('2', 1116, 683), digit('0', 1136, 683), digit('4', 1198, 683),
                                        digit('0', 1218, 683)};
    EXPECT_EQ(layout.digits, expected);
    EXPECT_EQ(layout.bar.x, 1166.0F);
    EXPECT_EQ(layout.bar.y, 683.0F);
    EXPECT_EQ(layout.bar.width, 2.0F);
    EXPECT_EQ(layout.bar.height, 25.0F);

    const cs::AmmoLayout small =
        cs::layoutAmmo({.clip = 20, .reserve = 40}, sheet(), {.width = 640, .height = 480});
    EXPECT_EQ(small.pictureX, 598);
    EXPECT_EQ(small.bar.x, 526.0F);
    EXPECT_EQ(numberAt(small.digits, 443), "2040");
}

TEST(AmmoHudTest, MoreDigitsWidenTheirRoomToTheLeft) {
    // docs/12 section 3.4: no ammo is cut short. A reserve of five digits pushes the bar and the clip
    // left; the picture stays at the right edge.
    const cs::AmmoLayout layout =
        cs::layoutAmmo({.clip = 1000, .reserve = 12345}, sheet(), {.width = 1280, .height = 720});
    EXPECT_EQ(layout.pictureX, 1238);
    EXPECT_EQ(numberAt(layout.digits, 683, 1138), "12345");
    EXPECT_EQ(layout.bar.x, 1138.0F - 12.0F);
    EXPECT_EQ(numberAt(layout.digits, 683), "100012345");
    EXPECT_EQ(layout.digits.front().x, 1126 - 10 - 80);
}

TEST(AmmoHudTest, TheAmmoShownIsTheHeldWeaponsWhenItFiresAny) {
    const cs::AmmoTypes ammo{.ammo = {cs::AmmoType{.id = "9mm", .maxCarried = 120, .boxRounds = 30}}};
    const cs::WeaponView view{.drawSequences = {0},
                              .idles = {cs::WeaponIdle{.wait = nx::Seconds{20.0F}, .share = 1.0F}},
                              .idlesWhenEmpty = true};
    const cs::WeaponModels models{
        .view = "models/v_gun.mdl", .player = "models/p_gun.mdl", .playerAnimation = "onehanded"};
    auto catalog = cs::WeaponCatalog::make(
        ammo, {cs::Weapon{.id = "knife",
                          .classname = "weapon_knife",
                          .name = "Knife",
                          .slot = cs::WeaponSlot::Knife,
                          .models = models,
                          .view = view},
               cs::Weapon{.id = "glock18",
                          .classname = "weapon_glock18",
                          .name = "9x19mm Sidearm",
                          .slot = cs::WeaponSlot::Secondary,
                          .weight = 5,
                          .magazine = cs::WeaponMagazine{.ammo = "9mm", .clipSize = 20},
                          .models = models,
                          .view = view}});
    ASSERT_TRUE(catalog.has_value()) << nx::describe(catalog.error());
    cs::Inventory inventory{*catalog};
    EXPECT_EQ(cs::hudAmmoOf(inventory, *catalog), std::nullopt);
    nx::Random random{0, "test"};
    std::ignore = inventory.give(cs::WeaponIndex{0}, cs::AutoSwitch::ToHeavier, {}, random);
    EXPECT_EQ(cs::hudAmmoOf(inventory, *catalog), std::nullopt) << "the knife fires none";
    std::ignore = inventory.give(cs::WeaponIndex{1}, cs::AutoSwitch::ToHeavier, {}, random);
    std::ignore = inventory.giveAmmo("9mm", 40);
    const std::optional<cs::HudAmmo> shown = cs::hudAmmoOf(inventory, *catalog);
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->ammo, "9mm");
    EXPECT_EQ(shown->clip, 20U);
    EXPECT_EQ(shown->reserve, 40U);
}

TEST(CrosshairTest, TheLinesAreWhereTheOriginalDrawsThem) {
    // A Glock at 1280x720: left 615..624 and right 656..665 on row 360, top 335..344 and bottom
    // 376..385 on column 640, a pixel thick.
    const std::array lines = cs::crosshairRects(cs::CrosshairSize::Auto, 8, {.width = 1280, .height = 720});
    expectRect(lines[0], {.x = 615.0F, .y = 360.0F, .width = 10.0F, .height = 1.0F}, "left");
    expectRect(lines[1], {.x = 656.0F, .y = 360.0F, .width = 10.0F, .height = 1.0F}, "right");
    expectRect(lines[2], {.x = 640.0F, .y = 335.0F, .width = 1.0F, .height = 10.0F}, "top");
    expectRect(lines[3], {.x = 640.0F, .y = 376.0F, .width = 1.0F, .height = 10.0F}, "bottom");
}

TEST(CrosshairTest, ItsSizeScalesWithTheScreensWidthAsTheOriginals) {
    using cs::CrosshairSize;
    // Measured on the original (docs/12 section 3.2): length and gap, a Glock (8) standing.
    struct Case {
        CrosshairSize size;
        std::uint32_t width;
        std::uint32_t height;
        Measured expected;
    };
    const std::array cases{
        Case{CrosshairSize::Small, 640, 480, {3, 5}},    Case{CrosshairSize::Small, 800, 600, {3, 6}},
        Case{CrosshairSize::Small, 1024, 768, {5, 8}},   Case{CrosshairSize::Small, 1280, 720, {6, 10}},
        Case{CrosshairSize::Small, 1600, 900, {7, 12}},  Case{CrosshairSize::Medium, 640, 480, {4, 6}},
        Case{CrosshairSize::Medium, 800, 600, {5, 8}},   Case{CrosshairSize::Medium, 1024, 768, {6, 10}},
        Case{CrosshairSize::Medium, 1280, 720, {8, 12}}, Case{CrosshairSize::Medium, 1600, 900, {10, 16}},
        Case{CrosshairSize::Large, 640, 480, {5, 8}},    Case{CrosshairSize::Large, 800, 600, {6, 10}},
        Case{CrosshairSize::Large, 1024, 768, {8, 12}},  Case{CrosshairSize::Large, 1280, 960, {10, 16}},
        Case{CrosshairSize::Large, 1600, 900, {12, 20}}, Case{CrosshairSize::Large, 1920, 1080, {15, 24}},
        Case{CrosshairSize::Auto, 640, 480, {3, 5}},     Case{CrosshairSize::Auto, 800, 600, {5, 8}},
        Case{CrosshairSize::Auto, 1024, 768, {8, 12}},   Case{CrosshairSize::Auto, 1600, 900, {12, 20}}};
    for (const Case& test : cases) {
        const Measured got = measured(test.size, 8, test.width, test.height);
        EXPECT_EQ(got.length, test.expected.length) << test.width << "x" << test.height;
        EXPECT_EQ(got.gap, test.expected.gap) << test.width << "x" << test.height;
    }
    // The knife (7), auto: 3 and 4 at 640x480, 5 and 7 at 800x600, 8 and 11 at 1024x768, 10 and 14
    // at 1280x720, 12 and 17 at 1600x900.
    for (const auto& [width, height, length, gap] :
         {std::tuple{640U, 480U, 3U, 4U}, std::tuple{800U, 600U, 5U, 7U}, std::tuple{1024U, 768U, 8U, 11U},
          std::tuple{1280U, 720U, 10U, 14U}, std::tuple{1600U, 900U, 12U, 17U}}) {
        const Measured got = measured(CrosshairSize::Auto, cs::kKnifeCrosshairDistance, width, height);
        EXPECT_EQ(got.length, length) << width << "x" << height;
        EXPECT_EQ(got.gap, gap) << width << "x" << height;
    }
}

TEST(CrosshairTest, ItsCvarsAreTheOriginalsAndRefuseWhatItCannotDraw) {
    SilentOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    const auto cvars = cs::HudCvars::registerIn(console);
    ASSERT_TRUE(cvars.has_value()) << nx::describe(cvars.error());
    const cs::CrosshairStyle style = cvars->crosshair();
    EXPECT_EQ(style.color, (nx::render::ScreenColor{.red = 50, .green = 250, .blue = 50}));
    EXPECT_EQ(style.size, cs::CrosshairSize::Auto);
    EXPECT_TRUE(style.isTranslucent);
    const std::string archived = console.archivedCvars();
    for (const std::string_view line : {R"(cl_crosshair_color "50 250 50")", R"(cl_crosshair_size "auto")",
                                        R"(cl_crosshair_translucent "1")"}) {
        EXPECT_NE(archived.find(line), std::string::npos) << line;
    }

    EXPECT_TRUE(console.setCvar("cl_crosshair_color", "255 255 255").has_value());
    EXPECT_TRUE(console.setCvar("cl_crosshair_size", "small").has_value());
    EXPECT_TRUE(console.setCvar("cl_crosshair_translucent", "0").has_value());
    EXPECT_FALSE(console.setCvar("cl_crosshair_color", "300 0 0").has_value());
    EXPECT_FALSE(console.setCvar("cl_crosshair_color", "50 250").has_value());
    EXPECT_FALSE(console.setCvar("cl_crosshair_size", "huge").has_value());
    const cs::CrosshairStyle changed = cvars->crosshair();
    EXPECT_EQ(changed.color, (nx::render::ScreenColor{.red = 255, .green = 255, .blue = 255}));
    EXPECT_EQ(changed.size, cs::CrosshairSize::Small);
    EXPECT_FALSE(changed.isTranslucent);
}

TEST(CrosshairTest, ColorsAndSizesAreReadAsTheOriginalWritesThem) {
    EXPECT_EQ(cs::crosshairColorOf("50 250 50"),
              (nx::render::ScreenColor{.red = 50, .green = 250, .blue = 50}));
    EXPECT_EQ(cs::crosshairColorOf(" 0  0 255 "),
              (nx::render::ScreenColor{.red = 0, .green = 0, .blue = 255}));
    EXPECT_FALSE(cs::crosshairColorOf("").has_value());
    EXPECT_FALSE(cs::crosshairColorOf("1 2 3 4").has_value());
    EXPECT_FALSE(cs::crosshairColorOf("1 2 -3").has_value());
    EXPECT_FALSE(cs::crosshairColorOf("1 2 3.5").has_value());
    EXPECT_EQ(cs::crosshairSizeNamed("medium"), cs::CrosshairSize::Medium);
    EXPECT_FALSE(cs::crosshairSizeNamed("Medium").has_value()) << "as the original spells them";
}

} // namespace
