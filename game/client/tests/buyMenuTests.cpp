// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The buy menu: what it sells on each tab and how each item stands, the original's keys, its mouse, when it
// closes, where its parts go on a screen, and what it tells of a gun and writes of money and time.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.game;
import nx.platform;
import nx.rhi;
import nx.ui;
import cs.shared;
import cs.client;

namespace {

using cs::ArmorKind;
using cs::BuyItemState;
using cs::BuyRefusal;
using cs::BuyTab;
using cs::WeaponIndex;
using nx::platform::Key;
using nx::platform::KeyChanged;
using nx::platform::MouseButton;
using nx::platform::MouseButtonChanged;

constexpr WeaponIndex kGlock{1};
constexpr WeaponIndex kAk{2};
constexpr WeaponIndex kM4{3};
constexpr nx::rhi::Extent2d kScreen{.width = 1920, .height = 1080};
constexpr nx::Seconds kBuyTime{90.0F};

/// A weapon of `id` in `slot`, at `position` of it.
[[nodiscard]] auto weaponOf(std::string id, cs::WeaponSlot slot, std::uint32_t position,
                            std::optional<cs::WeaponPurchase> purchase, std::optional<std::string> ammo)
    -> cs::Weapon {
    std::optional<cs::WeaponMagazine> magazine;
    if (ammo) {
        magazine = cs::WeaponMagazine{.ammo = *ammo, .clipSize = 30};
    }
    return cs::Weapon{.id = id,
                      .classname = "weapon_" + id,
                      .name = id,
                      .slot = slot,
                      .position = position,
                      .purchase = std::move(purchase),
                      .magazine = std::move(magazine),
                      .models = cs::WeaponModels{.view = "models/v_gun.mdl",
                                                 .player = "models/p_gun.mdl",
                                                 .playerAnimation = "gun"},
                      .view = cs::WeaponView{
                          .drawSequences = {1},
                          .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                          .idlesWhenEmpty = true}};
}

[[nodiscard]] auto soldAs(cs::Buyers buyers, std::int32_t price, BuyTab tab, std::uint32_t key)
    -> cs::WeaponPurchase {
    return cs::WeaponPurchase{.buyers = buyers,
                              .price = nx::Money{price},
                              .menu =
                                  cs::WeaponMenuPlace{.tab = tab, .key = key, .picture = "gfx/vgui/gun.tga"}};
}

/// A knife, not sold; a pistol for all at 1; a rifle for each team, at 2 and 3 of their tab; and a rifle sold
/// with no place on the menu.
[[nodiscard]] auto catalog() -> cs::WeaponCatalog {
    cs::AmmoTypes ammo{
        .ammo = {
            cs::AmmoType{
                .id = "9mm", .name = "9 mm", .maxCarried = 120, .boxPrice = nx::Money{20}, .boxRounds = 30},
            cs::AmmoType{.id = "762Nato",
                         .name = "7.62 NATO",
                         .maxCarried = 90,
                         .boxPrice = nx::Money{80},
                         .boxRounds = 30}}};
    auto made = cs::WeaponCatalog::make(
        std::move(ammo),
        {weaponOf("knife", cs::WeaponSlot::Knife, 1, std::nullopt, std::nullopt),
         weaponOf("glock", cs::WeaponSlot::Secondary, 1,
                  soldAs(cs::Buyers::Everyone, 400, BuyTab::Pistols, 1), "9mm"),
         weaponOf("ak", cs::WeaponSlot::Primary, 1, soldAs(cs::Buyers::Terrorists, 2500, BuyTab::Rifles, 2),
                  "762Nato"),
         weaponOf("m4", cs::WeaponSlot::Primary, 2,
                  soldAs(cs::Buyers::CounterTerrorists, 3100, BuyTab::Rifles, 3), "762Nato"),
         weaponOf("unlisted", cs::WeaponSlot::Primary, 3,
                  cs::WeaponPurchase{.buyers = cs::Buyers::Everyone, .price = nx::Money{100}}, "762Nato")});
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return std::move(made).value();
}

/// A menu over that catalog and a player, what it asked to buy, and what it told.
class BuyMenuTest : public ::testing::Test {
protected:
    BuyMenuTest()
        : weapons{catalog()}, inventory{weapons},
          menu{content(),
               cs::BuyActions{
                   .weapon = [this](WeaponIndex weapon) -> nx::Result<void, BuyRefusal> {
                       boughtWeapons.push_back(weapon);
                       return answer();
                   },
                   .armor = [this](ArmorKind kind) -> nx::Result<void, BuyRefusal> {
                       boughtArmor.push_back(kind);
                       return answer();
                   },
                   .ammo = [this](cs::WeaponSlot slot, cs::AmmoPurchase) -> nx::Result<void, BuyRefusal> {
                       boughtAmmo.push_back(slot);
                       return answer();
                   },
                   .rebuy = [this]() -> nx::Result<void, BuyRefusal> {
                       ++rebuys;
                       return answer();
                   },
                   .refused = [this](BuyRefusal told) { refusals.push_back(told); }}} {}

    [[nodiscard]] auto content() const -> cs::BuyMenuContent {
        return {.weapons = &weapons, .armorPrices = &prices, .look = &look, .armorRatio = 0.5F};
    }
    /// The player: a terrorist of `money` that may buy.
    [[nodiscard]] auto view() const -> cs::BuyerView {
        return {.team = team,
                .status = cs::PlayerStatus{.armor = armor, .armorKind = armorKind, .money = money},
                .inventory = &inventory,
                .buying = cs::BuyingState{.refusal = mayNotBuy, .timeLeft = kBuyTime, .length = kBuyTime},
                .lastPurchase = last};
    }
    void give(WeaponIndex weapon) {
        nx::Random random{0, "test"};
        std::ignore = inventory.give(weapon, cs::AutoSwitch::Off, nx::game::TickTime{}, random);
    }
    void openOnRifles() { ASSERT_TRUE(menu.toggle(view()).has_value()); }
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor = {})
        -> bool {
        return menu.handle(event, cursor, kScreen, view());
    }
    /// The middle of `rect`.
    [[nodiscard]] static auto middleOf(const nx::ui::PixelRect& rect) -> nx::platform::MousePosition {
        return {.x = static_cast<float>(rect.x.value()) + (static_cast<float>(rect.wide.value()) / 2.0F),
                .y = static_cast<float>(rect.y.value()) + (static_cast<float>(rect.tall.value()) / 2.0F)};
    }
    void click(const nx::ui::PixelRect& rect) {
        EXPECT_TRUE(handle(MouseButtonChanged{.button = MouseButton::Left, .isDown = true}, middleOf(rect)));
    }

    cs::WeaponCatalog weapons;
    cs::ArmorPrices prices{
        .kevlar = nx::Money{650}, .kevlarAndHelmet = nx::Money{1000}, .helmet = nx::Money{350}};
    cs::BuyMenuLook look{.font = {.regular = "Test", .strong = "Test"},
                         .armor = {cs::BuyMenuArmor{.kind = ArmorKind::Kevlar,
                                                    .key = 1,
                                                    .name = "Vest",
                                                    .picture = "gfx/vgui/kevlar.tga",
                                                    .about = "A vest."},
                                   cs::BuyMenuArmor{.kind = ArmorKind::KevlarAndHelmet,
                                                    .key = 2,
                                                    .name = "Vest and helmet",
                                                    .picture = "gfx/vgui/kevlar_helmet.tga",
                                                    .about = "Both."}}};
    cs::Inventory inventory;
    cs::Team team = cs::Team::Terrorists;
    nx::Money money{16000};
    nx::ArmorPoints armor{};
    ArmorKind armorKind = ArmorKind::None;
    std::optional<BuyRefusal> mayNotBuy;
    std::optional<BuyRefusal> refusal;
    std::vector<WeaponIndex> boughtWeapons;
    std::vector<ArmorKind> boughtArmor;
    std::vector<cs::WeaponSlot> boughtAmmo;
    std::size_t rebuys = 0;
    cs::LastPurchase last{};
    std::vector<BuyRefusal> refusals;
    cs::BuyMenu menu;

private:
    [[nodiscard]] auto answer() const -> nx::Result<void, BuyRefusal> {
        if (refusal) {
            return nx::fail(*refusal);
        }
        return {};
    }
};

TEST_F(BuyMenuTest, ATabSellsItsPlayersTeamWhatHasAPlaceOnIt) {
    const std::vector<cs::BuyEntry> forTerrorists = cs::buyEntriesOf(BuyTab::Rifles, content(), view());
    ASSERT_EQ(forTerrorists.size(), 1U) << "not the other team's, nor one of no place on the menu";
    EXPECT_EQ(forTerrorists.front().item, cs::BuyItem{kAk});
    EXPECT_EQ(forTerrorists.front().key, 2U);
    EXPECT_EQ(forTerrorists.front().price, nx::Money{2500});

    team = cs::Team::CounterTerrorists;
    const std::vector<cs::BuyEntry> forCounterTerrorists =
        cs::buyEntriesOf(BuyTab::Rifles, content(), view());
    ASSERT_EQ(forCounterTerrorists.size(), 1U);
    EXPECT_EQ(forCounterTerrorists.front().item, cs::BuyItem{kM4});
}

TEST_F(BuyMenuTest, TabsWithNothingForThePlayerAreNotListed) {
    EXPECT_EQ(cs::buyTabsOf(content(), view()),
              (std::vector{BuyTab::Pistols, BuyTab::Rifles, BuyTab::Equipment}));
}

TEST_F(BuyMenuTest, AWeaponMadeAsTheGameRunsIsListedOnceCompleteAndAReplacedOneNoMore) {
    auto made = weapons.create({.id = "made", .slot = cs::WeaponSlot::Secondary});
    ASSERT_TRUE(made.has_value());
    const std::size_t listed = cs::buyEntriesOf(BuyTab::Pistols, content(), view()).size();
    EXPECT_EQ(listed, 1U) << "incomplete, it is not";

    auto copied = weapons.create({.id = "copied", .slot = cs::WeaponSlot::Secondary, .from = kGlock});
    ASSERT_TRUE(copied.has_value());
    EXPECT_EQ(cs::buyEntriesOf(BuyTab::Pistols, content(), view()).size(), 2U);

    ASSERT_TRUE(weapons.replace(kGlock, *copied).has_value());
    const std::vector<cs::BuyEntry> entries = cs::buyEntriesOf(BuyTab::Pistols, content(), view());
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front().item, cs::BuyItem{*copied});
}

TEST_F(BuyMenuTest, AnItemStandsByTheMoneyAndWhatThePlayerHas) {
    money = nx::Money{2500};
    EXPECT_EQ(cs::buyEntriesOf(BuyTab::Rifles, content(), view()).front().state, BuyItemState::ForSale);
    money = nx::Money{2499};
    EXPECT_EQ(cs::buyEntriesOf(BuyTab::Rifles, content(), view()).front().state, BuyItemState::TooDear);
    give(kAk);
    EXPECT_EQ(cs::buyEntriesOf(BuyTab::Rifles, content(), view()).front().state, BuyItemState::Owned);
}

TEST_F(BuyMenuTest, ArmorCostsWhatIsNotWornAndWhatIsWornWholeIsOwned) {
    const auto entries = [this] {
        return cs::buyEntriesOf(BuyTab::Equipment, content(), view());
    };
    EXPECT_EQ(entries()[1].price, nx::Money{1000});
    armor = cs::kMostArmor;
    armorKind = ArmorKind::Kevlar;
    EXPECT_EQ(entries()[0].state, BuyItemState::Owned);
    EXPECT_EQ(entries()[1].price, nx::Money{350}) << "the helmet alone over a whole vest";
    armorKind = ArmorKind::KevlarAndHelmet;
    EXPECT_EQ(entries()[1].state, BuyItemState::Owned);
}

TEST_F(BuyMenuTest, TheOriginalsKeysBuyAndTheMenuStaysOpen) {
    openOnRifles();
    EXPECT_FALSE(menu.isArmed()) << "a first key opens a tab";
    menu.pressSlot(4, view());
    EXPECT_EQ(menu.tab(), BuyTab::Rifles);
    EXPECT_TRUE(menu.isArmed());
    menu.pressSlot(2, view());
    EXPECT_EQ(boughtWeapons, (std::vector{kAk}));
    EXPECT_TRUE(menu.isOpen());
    EXPECT_FALSE(menu.isArmed()) << "the next key opens a tab again";

    menu.pressSlot(8, view());
    EXPECT_EQ(menu.tab(), BuyTab::Equipment);
    menu.pressSlot(2, view());
    EXPECT_EQ(boughtArmor, (std::vector{ArmorKind::KevlarAndHelmet}));
}

TEST_F(BuyMenuTest, TheAmmosKeysBuyABoxAtOnce) {
    openOnRifles();
    menu.pressSlot(cs::kPrimaryAmmoKey, view());
    menu.pressSlot(cs::kPistolAmmoKey, view());
    EXPECT_EQ(boughtAmmo, (std::vector{cs::WeaponSlot::Primary, cs::WeaponSlot::Secondary}));
    EXPECT_FALSE(menu.isArmed());
}

TEST_F(BuyMenuTest, AKeyOfNothingDoesNothing) {
    openOnRifles();
    menu.pressSlot(2, view()); // shotguns, which have nothing for sale
    EXPECT_EQ(menu.tab(), BuyTab::Rifles);
    EXPECT_FALSE(menu.isArmed());
    menu.pressSlot(4, view());
    menu.pressSlot(9, view());
    EXPECT_TRUE(boughtWeapons.empty());
    EXPECT_TRUE(menu.isArmed());
}

TEST_F(BuyMenuTest, ClosedItsKeysDoNothing) {
    menu.pressSlot(4, view());
    menu.pressSlot(2, view());
    EXPECT_TRUE(boughtWeapons.empty());
    EXPECT_FALSE(menu.isOpen());
}

TEST_F(BuyMenuTest, TheBackKeyGoesBackALevelThenCloses) {
    openOnRifles();
    menu.pressSlot(4, view());
    menu.pressSlot(cs::kBuyMenuBackKey, view());
    EXPECT_FALSE(menu.isArmed());
    EXPECT_TRUE(menu.isOpen());
    menu.pressSlot(cs::kBuyMenuBackKey, view());
    EXPECT_FALSE(menu.isOpen());
}

TEST_F(BuyMenuTest, ItOpensOnTheTabLastShownAndOnEquipmentWhenAskedArmed) {
    openOnRifles();
    menu.pressSlot(1, view());
    ASSERT_TRUE(menu.toggle(view()).has_value());
    EXPECT_FALSE(menu.isOpen());
    ASSERT_TRUE(menu.toggle(view()).has_value());
    EXPECT_EQ(menu.tab(), BuyTab::Pistols);
    EXPECT_FALSE(menu.isArmed());
    menu.close();

    ASSERT_TRUE(menu.openEquipment(view()).has_value());
    EXPECT_EQ(menu.tab(), BuyTab::Equipment);
    EXPECT_TRUE(menu.isArmed());
}

TEST_F(BuyMenuTest, WhereThePlayerMayNotBuyItDoesNotOpen) {
    mayNotBuy = BuyRefusal::OutsideBuyZones;
    const auto opened = menu.toggle(view());
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().code, BuyRefusal::OutsideBuyZones);
    EXPECT_FALSE(menu.isOpen());
    EXPECT_FALSE(menu.openEquipment(view()).has_value());
    EXPECT_FALSE(menu.isOpen());
}

TEST_F(BuyMenuTest, ItClosesOnceThePlayerMayNotBuyTellingWhyButOfItsDeath) {
    openOnRifles();
    menu.update(view());
    EXPECT_TRUE(menu.isOpen());
    mayNotBuy = BuyRefusal::TimeOver;
    menu.update(view());
    EXPECT_FALSE(menu.isOpen());
    EXPECT_EQ(refusals, (std::vector{BuyRefusal::TimeOver}));

    mayNotBuy.reset();
    openOnRifles();
    mayNotBuy = BuyRefusal::Dead;
    menu.update(view());
    EXPECT_FALSE(menu.isOpen());
    EXPECT_EQ(refusals.size(), 1U);
}

TEST_F(BuyMenuTest, WhatTheGameRefusesIsToldAndTheTabStaysArmed) {
    openOnRifles();
    menu.pressSlot(4, view());
    refusal = BuyRefusal::NotEnoughMoney;
    menu.pressSlot(2, view());
    EXPECT_EQ(refusals, (std::vector{BuyRefusal::NotEnoughMoney}));
    EXPECT_TRUE(menu.isArmed());
}

TEST_F(BuyMenuTest, TheMouseSelectsWhatItIsOverAndBuysWhatItClicks) {
    openOnRifles();
    menu.pressSlot(1, view()); // the pistol, its first item selected
    menu.back();
    const cs::BuyMenuLayout layout{kScreen};
    const nx::ui::PixelRect cell = layout.cell(0, 1);
    EXPECT_TRUE(handle(nx::platform::MouseMoved{.dx = 1.0F}, middleOf(cell)));
    EXPECT_EQ(menu.selected(), std::optional{cs::BuyItem{kGlock}});
    click(cell);
    EXPECT_EQ(boughtWeapons, (std::vector{kGlock}));

    click(layout.buyButton());
    EXPECT_EQ(boughtWeapons, (std::vector{kGlock, kGlock}));
}

TEST_F(BuyMenuTest, TheMouseOpensATabAndBuysAmmoAndOverNothingDoesNothing) {
    openOnRifles();
    const cs::BuyMenuLayout layout{kScreen};
    click(layout.tab(0));
    EXPECT_EQ(menu.tab(), BuyTab::Pistols);
    EXPECT_TRUE(menu.isArmed());

    click(layout.ammoRow(1, 3));
    EXPECT_EQ(boughtAmmo, (std::vector{cs::WeaponSlot::Secondary}));

    click(layout.carried());
    EXPECT_TRUE(boughtWeapons.empty());
    EXPECT_TRUE(menu.isOpen());
}

TEST_F(BuyMenuTest, TheKeysItHasNoUseForAndWhatComesUpGoToTheBinds) {
    openOnRifles();
    EXPECT_FALSE(handle(KeyChanged{.key = Key::W, .isDown = true})) << "the player walks with it open";
    EXPECT_FALSE(handle(KeyChanged{.key = Key::Digit4, .isDown = true})) << "bound to slot4";
    EXPECT_FALSE(handle(KeyChanged{.key = Key::Enter, .isDown = false}));
    EXPECT_FALSE(handle(MouseButtonChanged{.button = MouseButton::Left, .isDown = false}));
    EXPECT_TRUE(handle(MouseButtonChanged{.button = MouseButton::Right, .isDown = true})) << "no attack";
    EXPECT_TRUE(handle(nx::platform::MouseWheelMoved{.dy = 1.0F}));
    EXPECT_FALSE(handle(nx::platform::FocusChanged{.isFocused = false}));
}

TEST_F(BuyMenuTest, EscapeGoesBackAndEnterBuysOncePerPress) {
    openOnRifles();
    menu.pressSlot(4, view());
    EXPECT_TRUE(handle(KeyChanged{.key = Key::Enter, .isDown = true}));
    EXPECT_TRUE(handle(KeyChanged{.key = Key::Enter, .isDown = true, .isRepeat = true}));
    EXPECT_EQ(boughtWeapons, (std::vector{kAk}));

    menu.pressSlot(4, view());
    EXPECT_TRUE(handle(KeyChanged{.key = Key::Escape, .isDown = true}));
    EXPECT_FALSE(menu.isArmed());
    EXPECT_TRUE(handle(KeyChanged{.key = Key::Escape, .isDown = true}));
    EXPECT_FALSE(menu.isOpen());
}

TEST_F(BuyMenuTest, ItsKeyAndItsTileBuyAgainAndWhatTheGameRefusesIsTold) {
    openOnRifles();
    last = cs::LastPurchase{.primary = kAk, .armor = ArmorKind::Kevlar};
    EXPECT_TRUE(handle(KeyChanged{.key = Key::R, .isDown = true, .character = U'r'})) << "it does not reload";
    EXPECT_EQ(rebuys, 1U);
    EXPECT_TRUE(handle(KeyChanged{.key = Key::R, .isDown = true, .isRepeat = true, .character = U'r'}));
    EXPECT_EQ(rebuys, 1U) << "once a press";
    EXPECT_FALSE(handle(KeyChanged{.key = Key::R, .isDown = false, .character = U'r'}));
    click(cs::BuyMenuLayout{kScreen}.againTile());
    EXPECT_EQ(rebuys, 2U);

    refusal = BuyRefusal::NotEnoughMoney;
    EXPECT_TRUE(handle(KeyChanged{.key = Key::R, .isDown = true, .character = U'r'}));
    EXPECT_EQ(refusals, (std::vector{BuyRefusal::NotEnoughMoney}));
}

TEST_F(BuyMenuTest, ItsTileBuysNothingWithNothingToBuyAgain) {
    openOnRifles();
    click(cs::BuyMenuLayout{kScreen}.againTile());
    EXPECT_EQ(rebuys, 0U);
    EXPECT_FALSE(menu.frameOf(view()).again.has_value());
}

TEST_F(BuyMenuTest, AFrameTellsWhatItWouldBuyAgainAndItsCost) {
    give(kGlock);
    last = cs::LastPurchase{.primary = kAk, .primaryRounds = 90, .armor = ArmorKind::KevlarAndHelmet};
    const std::optional<cs::BuyAgain> again = cs::buyAgainOf(content(), view());
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->items, (std::vector<std::string_view>{"ak", "Ammo", "Vest and helmet"}));
    EXPECT_EQ(again->cost, nx::Money{2500 + (3 * 80) + 1000});
}

TEST_F(BuyMenuTest, AFrameTellsWhatTheMenuShows) {
    give(kGlock);
    give(kM4);
    openOnRifles();
    const cs::BuyMenuFrame frame = menu.frameOf(view());
    EXPECT_EQ(frame.money, nx::Money{16000});
    EXPECT_EQ(frame.secondsLeft, 90U);
    EXPECT_EQ(frame.tab, BuyTab::Rifles);
    ASSERT_EQ(frame.tabs.size(), 3U);
    EXPECT_EQ(frame.tabs[1], (cs::BuyMenuTab{.tab = BuyTab::Rifles, .picture = "gfx/vgui/gun.tga"}));
    ASSERT_TRUE(frame.detail.has_value());
    EXPECT_EQ(frame.detail->entry.item, cs::BuyItem{kAk});
    EXPECT_EQ(frame.detail->buyers, cs::Buyers::Terrorists);
    EXPECT_EQ(frame.detail->replaces, "m4") << "of its slot";

    const cs::BuyAmmoRow& primary = frame.ammo[0];
    EXPECT_EQ(primary.key, cs::kPrimaryAmmoKey);
    ASSERT_TRUE(primary.ammo.has_value());
    EXPECT_EQ(primary.ammo->name, "7.62 NATO");
    EXPECT_EQ(primary.ammo->most, 90U);
    ASSERT_TRUE(frame.carried.primary.has_value());
    EXPECT_EQ(frame.carried.primary->clip, 30U);
    ASSERT_TRUE(frame.carried.pistol.has_value());
    EXPECT_EQ(frame.carried.armorKind, ArmorKind::None);

    EXPECT_EQ(menu.frameOf(view()), frame) << "laid out again only when it changes";
    money = nx::Money{15000};
    EXPECT_NE(menu.frameOf(view()), frame);
}

TEST_F(BuyMenuTest, ItIsLaidOutAgainAsItsClockChangesAndNotAsItsLineMoves) {
    const auto withTimeLeft = [this](float seconds) {
        cs::BuyerView shown = view();
        shown.buying.timeLeft = nx::Seconds{seconds};
        return shown;
    };
    openOnRifles();
    EXPECT_EQ(menu.frameOf(withTimeLeft(75.75F)), menu.frameOf(withTimeLeft(75.25F)));
    EXPECT_NE(menu.frameOf(withTimeLeft(75.25F)), menu.frameOf(withTimeLeft(74.75F)));
    EXPECT_GT(cs::buyTimeShareOf(withTimeLeft(75.75F).buying),
              cs::buyTimeShareOf(withTimeLeft(75.25F).buying));
}

TEST(BuyMenuLayoutTest, TheTimesShareIsWithinNoneAndAllOfIt) {
    const nx::Seconds length{90.0F};
    EXPECT_FLOAT_EQ(cs::buyTimeShareOf({.timeLeft = nx::Seconds{45.0F}, .length = length}), 0.5F);
    EXPECT_EQ(cs::buyTimeShareOf({.timeLeft = nx::Seconds{100.0F}, .length = length}), 1.0F);
    EXPECT_EQ(cs::buyTimeShareOf({.timeLeft = {}, .length = length}), 0.0F);
    EXPECT_EQ(cs::buyTimeShareOf({}), 0.0F) << "no length, no line";
}

TEST(BuyMenuLayoutTest, ABarsFillIsItsWholePixelsAndAShareOfTheNext) {
    constexpr nx::Pixels kWide{100};
    EXPECT_EQ(cs::barFillOf(kWide, 0.0F), (cs::BuyBarFill{}));
    EXPECT_EQ(cs::barFillOf(kWide, 1.0F), (cs::BuyBarFill{.whole = nx::Pixels{100}}));
    EXPECT_EQ(cs::barFillOf(kWide, 0.5F), (cs::BuyBarFill{.whole = nx::Pixels{50}}));
    const cs::BuyBarFill between = cs::barFillOf(kWide, 0.505F);
    EXPECT_EQ(between.whole, nx::Pixels{50});
    EXPECT_NEAR(between.nextPixel, 0.5F, 0.01F);
    EXPECT_EQ(cs::barFillOf(kWide, 7.0F).whole, kWide) << "never past its end";
}

TEST(BuyMenuLayoutTest, TheTimeBarRunsDownTickByTickWithoutAJump) {
    constexpr std::int32_t kTicks = 9000; // 90 seconds of 100 ticks
    const nx::ui::PixelRect bar = cs::BuyMenuLayout{kScreen}.timeBar();
    const auto reached = [&bar](std::int32_t tick) {
        const float share = 1.0F - (static_cast<float>(tick) / static_cast<float>(kTicks));
        const cs::BuyBarFill fill = cs::barFillOf(bar.wide, share);
        return static_cast<float>(fill.whole.value()) + fill.nextPixel;
    };
    // A second a time, the line jumped by this many pixels; a tick's is a hundredth of it, within a float's
    // error.
    const float aTick = static_cast<float>(bar.wide.value()) / static_cast<float>(kTicks);
    for (std::int32_t tick = 1; tick <= kTicks; ++tick) {
        const float step = reached(tick - 1) - reached(tick);
        ASSERT_LT(step, aTick * 2.0F) << "at tick " << tick;
        ASSERT_GT(step, -aTick) << "never back, at tick " << tick;
    }
}

TEST(BuyMenuLayoutTest, EveryPartIsOnTheScreenWhateverItsSize) {
    for (const nx::rhi::Extent2d size :
         {nx::rhi::Extent2d{.width = 640, .height = 480}, nx::rhi::Extent2d{.width = 1280, .height = 720},
          nx::rhi::Extent2d{.width = 1920, .height = 1080}, nx::rhi::Extent2d{.width = 2560, .height = 1080},
          nx::rhi::Extent2d{.width = 3840, .height = 2160}}) {
        const cs::BuyMenuLayout layout{size};
        const auto isOnScreen = [size](const nx::ui::PixelRect& rect) {
            return rect.x >= nx::Pixels{} && rect.y >= nx::Pixels{} && rect.wide > nx::Pixels{} &&
                   rect.tall > nx::Pixels{} &&
                   rect.x + rect.wide <= nx::Pixels{nx::narrow<std::int32_t>(size.width)} &&
                   rect.y + rect.tall <= nx::Pixels{nx::narrow<std::int32_t>(size.height)};
        };
        EXPECT_TRUE(isOnScreen(layout.header()));
        EXPECT_TRUE(isOnScreen(layout.timeBar()));
        EXPECT_TRUE(isOnScreen(layout.rail()));
        EXPECT_TRUE(isOnScreen(layout.grid()));
        EXPECT_TRUE(isOnScreen(layout.detail()));
        EXPECT_TRUE(isOnScreen(layout.carried()));
        EXPECT_TRUE(isOnScreen(layout.buyButton()));
        EXPECT_LE(layout.ammoRow(1, 6).y + layout.ammoRow(1, 6).tall, layout.rail().y + layout.rail().tall)
            << "all the tabs and the ammo fit on the rail";
        EXPECT_TRUE(isOnScreen(layout.carriedTile(0)));
        EXPECT_TRUE(isOnScreen(layout.againTile()));
        EXPECT_LE(layout.carriedTile(2).x + layout.carriedTile(2).wide, layout.againTile().x);
        EXPECT_LT(layout.againTile().x + layout.againTile().wide, layout.carried().x + layout.carried().wide)
            << "the keys have the rest of the row";
    }
}

TEST(BuyMenuLayoutTest, HoweverManyItemsATabHasTheyAllFitApart) {
    const cs::BuyMenuLayout layout{kScreen};
    for (const std::size_t count :
         {std::size_t{1}, std::size_t{6}, std::size_t{9}, std::size_t{16}, std::size_t{64}}) {
        for (std::size_t index = 0; index < count; ++index) {
            const nx::ui::PixelRect cell = layout.cell(index, count);
            EXPECT_GE(cell.x, layout.grid().x);
            EXPECT_LE(cell.x + cell.wide, layout.grid().x + layout.grid().wide);
            EXPECT_LE(cell.y + cell.tall, layout.grid().y + layout.grid().tall);
            if (index > 0) {
                const nx::ui::PixelRect before = layout.cell(index - 1, count);
                EXPECT_TRUE(before.y < cell.y || before.x + before.wide <= cell.x)
                    << count << " items, " << index;
            }
        }
    }
}

TEST(BuyMenuLayoutTest, AnItemsPriceTakesARowOfItsOwnWhenItDoesNotFitBesideItsName) {
    using nx::Pixels;
    EXPECT_FALSE(cs::isPriceApart(1, Pixels{100}, Pixels{40}, Pixels{10}, Pixels{150}));
    EXPECT_TRUE(cs::isPriceApart(1, Pixels{100}, Pixels{40}, Pixels{10}, Pixels{149}))
        << "a narrow screen's cell";
    EXPECT_TRUE(cs::isPriceApart(2, Pixels{50}, Pixels{40}, Pixels{10}, Pixels{150})) << "its name wrapped";
}

TEST(BuyMenuTextTest, MoneyIsWrittenWithItsThousands) {
    EXPECT_EQ(cs::dollarsText(nx::Money{0}), "$0");
    EXPECT_EQ(cs::dollarsText(nx::Money{800}), "$800");
    EXPECT_EQ(cs::dollarsText(nx::Money{2500}), "$2,500");
    EXPECT_EQ(cs::dollarsText(nx::Money{16000}), "$16,000");
    EXPECT_EQ(cs::dollarsText(nx::Money{1234567}), "$1,234,567") << "no value is cut short";
}

TEST(BuyMenuTextTest, TimeAndSharesAreWrittenWhole) {
    EXPECT_EQ(cs::clockText(90), "1:30");
    EXPECT_EQ(cs::clockText(65), "1:05");
    EXPECT_EQ(cs::clockText(0), "0:00");
    EXPECT_EQ(cs::shareText(0.775F), "77.5%");
    EXPECT_EQ(cs::shareText(0.7F), "70%");
}

/// The fonts the menu writes in are the project's: there with its data on any system, each of its weight and
/// with every character the menu's own text writes.
TEST(BuyMenuDataTest, ItsFontsAreTheProjectsOwnAndWriteAllItSays) {
    auto vfs = nx::assets::Vfs::create(std::span<const std::filesystem::path>{});
    ASSERT_TRUE(vfs.has_value());
    auto game = cs::readGameContent(NX_DATA_DIRECTORY, std::move(vfs).value());
    ASSERT_TRUE(game.has_value()) << (game ? "" : nx::describe(game.error()));

    nx::ui::FontCatalog fonts;
    const auto added = fonts.addFolder(std::filesystem::path{NX_DATA_DIRECTORY} / "fonts");
    ASSERT_TRUE(added.has_value());
    EXPECT_EQ(added->faces, 2U);
    EXPECT_TRUE(added->leftOut.empty());
    constexpr std::array kWritten{U'$', U',', U':', U'/', U'%', U'|',     U'+',
                                  U'.', U'0', U'9', U'A', U'z', U'\u00B7'};
    for (const auto& [family, weight] : {std::pair{std::string_view{game->buyMenu.font.regular}, 400},
                                         std::pair{std::string_view{game->buyMenu.font.strong}, 600}}) {
        const std::optional<nx::ui::FontChoice> choice = fonts.choose({.family = family, .weight = weight});
        ASSERT_TRUE(choice.has_value()) << family;
        EXPECT_EQ(choice->face->weight, weight) << family;
        EXPECT_FALSE(choice->isEmboldened) << "no bold simulated on " << family;
        auto font = fonts.open(*choice, {.cellHeight = 21, .smoothing = nx::ui::FontSmoothing::Antialiased});
        ASSERT_TRUE(font.has_value()) << (font ? "" : nx::describe(font.error()));
        auto atlas = nx::ui::GlyphAtlas::build(*font, cs::consoleCharacters());
        ASSERT_TRUE(atlas.has_value()) << (atlas ? "" : nx::describe(atlas.error()));
        for (const char32_t character : kWritten) {
            EXPECT_NE(atlas->glyph(character), nullptr)
                << family << " lacks " << static_cast<std::uint32_t>(character);
        }
    }
}

/// The game's own data: what the menu tells of a gun comes from its file.
TEST(BuyMenuDataTest, AGunTellsWhatItsFileSays) {
    auto vfs = nx::assets::Vfs::create(std::span<const std::filesystem::path>{});
    ASSERT_TRUE(vfs.has_value());
    auto game = cs::readGameContent(NX_DATA_DIRECTORY, std::move(vfs).value());
    ASSERT_TRUE(game.has_value()) << (game ? "" : nx::describe(game.error()));
    const cs::BuyMenuContent content{.weapons = &game->weapons,
                                     .armorPrices = &game->buying.armor,
                                     .look = &game->buyMenu,
                                     .armorRatio = game->playerHarm.armor.ratio};

    const cs::Weapon* const rifle = game->weapons.findWeapon("ak47");
    ASSERT_NE(rifle, nullptr);
    const std::optional<cs::BuyGunFacts> facts = cs::buyGunFactsOf(*rifle, content);
    ASSERT_TRUE(facts.has_value());
    EXPECT_EQ(facts->damage, 36);
    EXPECT_EQ(facts->roundsAMinute, 628);
    EXPECT_FALSE(facts->isSemiAutomatic);
    EXPECT_FLOAT_EQ(facts->throughArmor, 0.775F);
    EXPECT_EQ(facts->moveSpeed, nx::UnitsPerSecond{221.0F});
    EXPECT_EQ(facts->clip, 30U);
    EXPECT_EQ(facts->carried, 90U);
    EXPECT_EQ(facts->reload, nx::Seconds{2.45F});
    EXPECT_EQ(facts->ammoName, "7.62 NATO");
    EXPECT_EQ(facts->boxRounds, 30U);
    EXPECT_EQ(facts->boxPrice, nx::Money{80});

    const cs::Weapon* const pistol = game->weapons.findWeapon("glock18");
    ASSERT_NE(pistol, nullptr);
    EXPECT_TRUE(cs::buyGunFactsOf(*pistol, content)->isSemiAutomatic);
    EXPECT_FALSE(cs::buyGunFactsOf(*game->weapons.findWeapon("knife"), content).has_value());

    // Each key where the original's menus have it, whatever else comes to be sold.
    const cs::Inventory inventory{game->weapons};
    const cs::BuyerView terrorist{.team = cs::Team::Terrorists, .inventory = &inventory};
    const cs::BuyerView counterTerrorist{.team = cs::Team::CounterTerrorists, .inventory = &inventory};
    const auto keyOf = [&](const cs::BuyerView& view, BuyTab tab, std::string_view name) -> std::uint32_t {
        for (const cs::BuyEntry& entry : cs::buyEntriesOf(tab, content, view)) {
            if (entry.name == name) {
                return entry.key;
            }
        }
        return 0;
    };
    EXPECT_EQ(keyOf(terrorist, BuyTab::Rifles, "CV-47"), 2U);
    EXPECT_EQ(keyOf(counterTerrorist, BuyTab::Rifles, "Maverick M4A1 Carbine"), 3U);
    EXPECT_EQ(keyOf(terrorist, BuyTab::Pistols, "9x19mm Sidearm"), 1U);
    EXPECT_EQ(keyOf(terrorist, BuyTab::Pistols, "K&M .45 Tactical"), 2U);
    EXPECT_EQ(keyOf(terrorist, BuyTab::Equipment, "Kevlar Vest"), 1U);
    EXPECT_EQ(keyOf(terrorist, BuyTab::Equipment, "Kevlar + Helmet"), 2U);
}

} // namespace
