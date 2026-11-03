// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.platform;
import cs.shared;

namespace cs {

namespace {

constexpr float kSecondsAMinute = 60.0F;
/// The key that buys again, by the layout's character: the menu's, whatever it is bound to.
constexpr char32_t kBuyAgainKey = U'r';
/// The ammo's rows, in their order.
constexpr std::array<std::pair<std::uint32_t, WeaponSlot>, 2> kAmmoRows{
    std::pair{kPrimaryAmmoKey, WeaponSlot::Primary}, std::pair{kPistolAmmoKey, WeaponSlot::Secondary}};

/// The player of `view` as the rules weigh what it can afford: where and when it may buy, as it may while the
/// menu is open, which is the game's to weigh as it buys.
[[nodiscard]] auto affordingBuyerOf(const BuyerView& view) noexcept -> Buyer {
    return Buyer{.team = view.team,
                 .money = view.status.money,
                 .isAlive = true,
                 .isAtBuyingPlace = true,
                 .mayTeamBuy = true};
}

/// How an item stands by what the rules said of it; none when it is not for the player at all.
[[nodiscard]] auto itemStateOf(const nx::Result<nx::Money, BuyRefusal>& weighed) noexcept
    -> std::optional<BuyItemState> {
    if (weighed) {
        return BuyItemState::ForSale;
    }
    const BuyRefusal refusal = weighed.error().code;
    if (refusal == BuyRefusal::NotEnoughMoney) {
        return BuyItemState::TooDear;
    }
    if (refusal == BuyRefusal::AlreadyOwned || refusal == BuyRefusal::ArmorWhole ||
        refusal == BuyRefusal::AmmoFull) {
        return BuyItemState::Owned;
    }
    return std::nullopt;
}

/// The menu's place of `weapon`, for sale on it: none for one that is not, a replaced one among them.
[[nodiscard]] auto menuPlaceOf(WeaponIndex index, const WeaponCatalog& weapons) noexcept
    -> const WeaponMenuPlace* {
    const std::optional<WeaponPurchase>& purchase = weapons.weapon(index).purchase;
    if (!purchase || !purchase->menu || !weapons.isComplete(index) || weapons.madeFor(index) != index) {
        return nullptr;
    }
    return &*purchase->menu;
}

/// Appends to `entries` the weapons of `tab` for the player of `view`.
void appendWeapons(std::vector<BuyEntry>& entries, BuyTab tab, const BuyMenuContent& content,
                   const BuyerView& view) {
    const WeaponCatalog& weapons = *content.weapons;
    const Buyer buyer = affordingBuyerOf(view);
    for (std::uint32_t value = 0; value < weapons.weapons().size(); ++value) {
        const WeaponIndex index{value};
        const WeaponMenuPlace* const place = menuPlaceOf(index, weapons);
        if (place == nullptr || place->tab != tab) {
            continue;
        }
        const Weapon& weapon = weapons.weapon(index);
        const std::optional<BuyItemState> state =
            itemStateOf(weaponPriceFor(weapon, buyer, view.inventory->has(index)));
        if (state) {
            entries.push_back({.item = index,
                               .key = place->key,
                               .name = weapon.name,
                               .picture = place->picture,
                               .price = weapon.purchase->price,
                               .state = *state});
        }
    }
}

/// Appends to `entries` the armor the equipment tab sells the player of `view`.
void appendArmor(std::vector<BuyEntry>& entries, const BuyMenuContent& content, const BuyerView& view) {
    const Buyer buyer = affordingBuyerOf(view);
    for (const BuyMenuArmor& armor : content.look->armor) {
        const std::optional<BuyItemState> state =
            itemStateOf(armorPriceFor(*content.armorPrices, buyer, view.status, armor.kind));
        if (state) {
            entries.push_back(
                {.item = armor.kind,
                 .key = armor.key,
                 .name = armor.name,
                 .picture = armor.picture,
                 .price = armorCostOf(*content.armorPrices, view.status, armor.kind).value_or(nx::Money{}),
                 .state = *state});
        }
    }
}

/// The newest of the player's weapons of `slot`; none.
[[nodiscard]] auto newestIn(WeaponSlot slot, const BuyerView& view) -> std::optional<WeaponIndex> {
    const std::vector<WeaponIndex> weapons = view.inventory->weaponsIn(slot);
    return weapons.empty() ? std::nullopt : std::optional{weapons.front()};
}

/// The ammo `weapon` fires; none for one that fires none.
[[nodiscard]] auto ammoOf(const Weapon& weapon, const WeaponCatalog& weapons) noexcept -> const AmmoType* {
    return weapon.magazine ? weapons.findAmmo(weapon.magazine->ammo) : nullptr;
}

[[nodiscard]] auto ammoRowOf(std::uint32_t key, WeaponSlot slot, const BuyMenuContent& content,
                             const BuyerView& view) -> BuyAmmoRow {
    BuyAmmoRow row{.key = key, .slot = slot};
    const std::optional<WeaponIndex> gun = newestIn(slot, view);
    const AmmoType* const ammo = gun ? ammoOf(content.weapons->weapon(*gun), *content.weapons) : nullptr;
    if (ammo == nullptr) {
        return row;
    }
    const std::uint32_t carried = view.inventory->reserve(ammo->id);
    row.ammo = BuyAmmoFacts{.name = ammo->name,
                            .carried = carried,
                            .most = ammo->maxCarried,
                            .boxRounds = ammo->boxRounds,
                            .boxPrice = ammo->boxPrice};
    row.state =
        itemStateOf(ammoPriceFor(*ammo, affordingBuyerOf(view), carried)).value_or(BuyItemState::Owned);
    return row;
}

[[nodiscard]] auto carriedGunOf(WeaponSlot slot, const BuyMenuContent& content, const BuyerView& view)
    -> std::optional<BuyCarriedGun> {
    const std::optional<WeaponIndex> gun = newestIn(slot, view);
    if (!gun) {
        return std::nullopt;
    }
    const Weapon& weapon = content.weapons->weapon(*gun);
    const std::span<const OwnedWeapon> owned = view.inventory->weapons();
    const auto held = std::ranges::find(owned, *gun, &OwnedWeapon::weapon);
    const AmmoType* const ammo = ammoOf(weapon, *content.weapons);
    const bool hasPlace = weapon.purchase && weapon.purchase->menu;
    return BuyCarriedGun{.picture =
                             hasPlace ? std::string_view{weapon.purchase->menu->picture} : std::string_view{},
                         .clip = held != owned.end() ? held->clip : 0,
                         .carried = ammo != nullptr ? view.inventory->reserve(ammo->id) : 0};
}

[[nodiscard]] auto carriedOf(const BuyMenuContent& content, const BuyerView& view) -> BuyCarried {
    const std::span<const BuyMenuArmor> armor = content.look->armor;
    const auto worn = std::ranges::find(armor, view.status.armorKind, &BuyMenuArmor::kind);
    return BuyCarried{.primary = carriedGunOf(WeaponSlot::Primary, content, view),
                      .pistol = carriedGunOf(WeaponSlot::Secondary, content, view),
                      .armorKind = view.status.armorKind,
                      .armor = view.status.armor,
                      .armorPicture =
                          worn != armor.end() ? std::string_view{worn->picture} : std::string_view{}};
}

/// What the menu tells of `entry`, an item of `tab` for the player of `view`.
[[nodiscard]] auto detailOf(const BuyEntry& entry, BuyTab tab, const BuyMenuContent& content,
                            const BuyerView& view) -> BuyDetail {
    BuyDetail detail{.entry = entry, .tab = tab};
    if (const auto* const kind = std::get_if<ArmorKind>(&entry.item)) {
        const std::span<const BuyMenuArmor> armor = content.look->armor;
        const auto found = std::ranges::find(armor, *kind, &BuyMenuArmor::kind);
        detail.about = found != armor.end() ? std::string_view{found->about} : std::string_view{};
    } else if (const auto* const index = std::get_if<WeaponIndex>(&entry.item)) {
        const Weapon& weapon = content.weapons->weapon(*index);
        detail.buyers = weapon.purchase ? std::optional{weapon.purchase->buyers} : std::nullopt;
        detail.gun = buyGunFactsOf(weapon, content);
        const std::optional<WeaponIndex> held = newestIn(weapon.slot, view);
        if (held && *held != *index) {
            detail.replaces = content.weapons->weapon(*held).name;
        }
    }
    return detail;
}

/// What a list of what is bought again calls ammo.
constexpr std::string_view kAmmoItem = "Ammo";

/// The seconds of `time`, whole ones up.
[[nodiscard]] auto wholeSecondsOf(nx::Seconds time) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::ceil(std::max(time.count(), 0.0F)));
}

} // namespace

auto buyEntriesOf(BuyTab tab, const BuyMenuContent& content, const BuyerView& view) -> std::vector<BuyEntry> {
    std::vector<BuyEntry> entries;
    appendWeapons(entries, tab, content, view);
    if (tab == BuyTab::Equipment) {
        appendArmor(entries, content, view);
    }
    std::ranges::stable_sort(entries, {}, &BuyEntry::key);
    return entries;
}

auto buyTabsOf(const BuyMenuContent& content, const BuyerView& view) -> std::vector<BuyTab> {
    std::vector<BuyTab> tabs;
    for (const BuyTab tab : nx::EnumArray<BuyTab, std::uint32_t>::keys()) {
        if (!buyEntriesOf(tab, content, view).empty()) {
            tabs.push_back(tab);
        }
    }
    return tabs;
}

auto buyGunFactsOf(const Weapon& weapon, const BuyMenuContent& content) -> std::optional<BuyGunFacts> {
    const AmmoType* const ammo = ammoOf(weapon, *content.weapons);
    if (ammo == nullptr || !weapon.firing) {
        return std::nullopt;
    }
    return BuyGunFacts{.damage = nx::narrow<std::int32_t>(std::lround(weapon.firing->damage.value())),
                       .roundsAMinute = nx::narrow<std::int32_t>(
                           std::lround(kSecondsAMinute / weapon.firing->cycle.count())),
                       .isSemiAutomatic = !weapon.firing->isAutomatic,
                       .throughArmor = content.armorRatio * static_cast<float>(weapon.harm.armorRatio),
                       .moveSpeed = weapon.maxSpeed,
                       .clip = weapon.magazine->clipSize,
                       .carried = ammo->maxCarried,
                       .reload = weapon.reload ? weapon.reload->time : nx::Seconds{},
                       .ammoName = ammo->name,
                       .boxRounds = ammo->boxRounds,
                       .boxPrice = ammo->boxPrice};
}

auto buyAgainOf(const BuyMenuContent& content, const BuyerView& view) -> std::optional<BuyAgain> {
    const std::vector<RebuyStep> steps =
        rebuyStepsOf(view.lastPurchase, *view.inventory, view.status, *content.weapons);
    if (steps.empty()) {
        return std::nullopt;
    }
    BuyAgain again{
        .cost = rebuyCostOf(steps, *view.inventory, view.status, *content.weapons, *content.armorPrices)};
    for (const RebuyStep& step : steps) {
        if (const auto* const weapon = std::get_if<WeaponIndex>(&step)) {
            again.items.emplace_back(content.weapons->weapon(*weapon).name);
        } else if (const auto* const kind = std::get_if<ArmorKind>(&step)) {
            const std::span<const BuyMenuArmor> armor = content.look->armor;
            const auto found = std::ranges::find(armor, *kind, &BuyMenuArmor::kind);
            if (found != armor.end()) {
                again.items.emplace_back(found->name);
            }
        } else if (!std::ranges::contains(again.items, kAmmoItem)) {
            again.items.push_back(kAmmoItem);
        }
    }
    return again;
}

BuyMenu::BuyMenu(const BuyMenuContent& sold, BuyActions buying) noexcept
    : content{sold}, actions{std::move(buying)} {}

auto BuyMenu::open(const BuyerView& view) -> nx::Result<void, BuyRefusal> {
    if (view.buying.refusal) {
        return nx::fail(*view.buying.refusal);
    }
    isShown = true;
    const std::vector<BuyTab> tabs = buyTabsOf(content, view);
    const bool isShownTabListed = std::ranges::contains(tabs, shownTab);
    showTab(isShownTabListed || tabs.empty() ? shownTab : tabs.front(), view, false);
    return {};
}

auto BuyMenu::toggle(const BuyerView& view) -> nx::Result<void, BuyRefusal> {
    if (isShown) {
        close();
        return {};
    }
    return open(view);
}

auto BuyMenu::openEquipment(const BuyerView& view) -> nx::Result<void, BuyRefusal> {
    if (!isShown) {
        if (auto opened = open(view); !opened) {
            return opened;
        }
    }
    showTab(BuyTab::Equipment, view, true);
    return {};
}

void BuyMenu::close() noexcept {
    isShown = false;
    isTabArmed = false;
}

void BuyMenu::back() noexcept {
    if (isTabArmed) {
        isTabArmed = false;
    } else {
        close();
    }
}

void BuyMenu::update(const BuyerView& view) {
    if (!isShown || !view.buying.refusal) {
        return;
    }
    close();
    if (*view.buying.refusal != BuyRefusal::Dead) {
        actions.refused(*view.buying.refusal);
    }
}

void BuyMenu::pressSlot(std::uint32_t slot, const BuyerView& view) {
    if (!isShown) {
        return;
    }
    if (slot == kBuyMenuBackKey) {
        back();
        return;
    }
    if (isTabArmed) {
        const std::vector<BuyEntry> entries = buyEntriesOf(shownTab, content, view);
        const auto entry = std::ranges::find(entries, slot, &BuyEntry::key);
        if (entry != entries.end()) {
            selection = entry->item;
            buy(entry->item);
        }
        return;
    }
    const auto ammoRow = std::ranges::find(kAmmoRows, slot, &std::pair<std::uint32_t, WeaponSlot>::first);
    if (ammoRow != kAmmoRows.end()) {
        buyAmmo(ammoRow->second);
        return;
    }
    for (const BuyTab tab : nx::EnumArray<BuyTab, std::uint32_t>::keys()) {
        if (kBuyTabKeys[tab] == slot && !buyEntriesOf(tab, content, view).empty()) {
            showTab(tab, view, true);
        }
    }
}

void BuyMenu::buySelected(const BuyerView& view) {
    if (isShown && selection) {
        const std::vector<BuyEntry> entries = buyEntriesOf(shownTab, content, view);
        if (std::ranges::contains(entries, *selection, &BuyEntry::item)) {
            buy(*selection);
        }
    }
}

auto BuyMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                     nx::rhi::Extent2d size, const BuyerView& view) -> bool {
    if (std::holds_alternative<nx::platform::MouseMoved>(event)) {
        hover(cursor, size, view);
        return true;
    }
    if (std::holds_alternative<nx::platform::MouseWheelMoved>(event)) {
        return true;
    }
    if (const auto* const button = std::get_if<nx::platform::MouseButtonChanged>(&event)) {
        if (button->isDown && button->button == nx::platform::MouseButton::Left) {
            click(cursor, size, view);
        }
        return button->isDown;
    }
    const auto* const key = std::get_if<nx::platform::KeyChanged>(&event);
    if (key == nullptr || !key->isDown) {
        return false;
    }
    if (key->key == nx::platform::Key::Escape) {
        if (!key->isRepeat) {
            back();
        }
        return true;
    }
    if (key->key == nx::platform::Key::Enter || key->key == nx::platform::Key::KeypadEnter) {
        if (!key->isRepeat) {
            buySelected(view);
        }
        return true;
    }
    if (key->character == kBuyAgainKey) {
        if (!key->isRepeat) {
            buyAgain();
        }
        return true;
    }
    return false;
}

auto BuyMenu::frameOf(const BuyerView& view) const -> BuyMenuFrame {
    BuyMenuFrame frame{.team = view.team,
                       .money = view.status.money,
                       .secondsLeft = wholeSecondsOf(view.buying.timeLeft),
                       .secondsInAll = wholeSecondsOf(view.buying.length),
                       .tab = shownTab,
                       .isArmed = isTabArmed,
                       .entries = buyEntriesOf(shownTab, content, view),
                       .ammo = {ammoRowOf(kPrimaryAmmoKey, WeaponSlot::Primary, content, view),
                                ammoRowOf(kPistolAmmoKey, WeaponSlot::Secondary, content, view)},
                       .carried = carriedOf(content, view),
                       .again = buyAgainOf(content, view)};
    for (const BuyTab tab : nx::EnumArray<BuyTab, std::uint32_t>::keys()) {
        const std::vector<BuyEntry> entries = buyEntriesOf(tab, content, view);
        if (!entries.empty()) {
            frame.tabs.push_back({.tab = tab, .picture = entries.front().picture});
        }
    }
    if (selection) {
        const auto entry = std::ranges::find(frame.entries, *selection, &BuyEntry::item);
        if (entry != frame.entries.end()) {
            frame.detail = detailOf(*entry, shownTab, content, view);
        }
    }
    return frame;
}

void BuyMenu::showTab(BuyTab tab, const BuyerView& view, bool arms) {
    shownTab = tab;
    isTabArmed = arms;
    const std::vector<BuyEntry> entries = buyEntriesOf(tab, content, view);
    selection = entries.empty() ? std::nullopt : std::optional{entries.front().item};
}

void BuyMenu::buy(const BuyItem& item) {
    const auto* const weapon = std::get_if<WeaponIndex>(&item);
    const auto* const armor = std::get_if<ArmorKind>(&item);
    const nx::Result<void, BuyRefusal> bought =
        weapon != nullptr ? actions.weapon(*weapon) : actions.armor(*armor);
    if (bought) {
        isTabArmed = false;
    } else {
        actions.refused(bought.error().code);
    }
}

void BuyMenu::buyAmmo(WeaponSlot slot) {
    if (const nx::Result<void, BuyRefusal> bought = actions.ammo(slot, AmmoPurchase::OneBox); !bought) {
        actions.refused(bought.error().code);
    }
}

void BuyMenu::buyAgain() {
    if (const nx::Result<void, BuyRefusal> bought = actions.rebuy(); bought) {
        isTabArmed = false;
    } else {
        actions.refused(bought.error().code);
    }
}

void BuyMenu::hover(nx::platform::MousePosition cursor, nx::rhi::Extent2d size, const BuyerView& view) {
    const BuyMenuLayout layout{size};
    const std::vector<BuyEntry> entries = buyEntriesOf(shownTab, content, view);
    for (const auto& [index, entry] : std::views::enumerate(entries)) {
        if (layout.cell(nx::narrow<std::size_t>(index), entries.size()).holds(cursor.x, cursor.y)) {
            selection = entry.item;
            return;
        }
    }
}

void BuyMenu::click(nx::platform::MousePosition cursor, nx::rhi::Extent2d size, const BuyerView& view) {
    const BuyMenuLayout layout{size};
    const std::vector<BuyTab> tabs = buyTabsOf(content, view);
    for (const auto& [index, tab] : std::views::enumerate(tabs)) {
        if (layout.tab(nx::narrow<std::size_t>(index)).holds(cursor.x, cursor.y)) {
            showTab(tab, view, true);
            return;
        }
    }
    for (const auto& [index, ammo] : std::views::enumerate(kAmmoRows)) {
        if (layout.ammoRow(nx::narrow<std::size_t>(index), tabs.size()).holds(cursor.x, cursor.y)) {
            buyAmmo(ammo.second);
            return;
        }
    }
    const std::vector<BuyEntry> entries = buyEntriesOf(shownTab, content, view);
    for (const auto& [index, entry] : std::views::enumerate(entries)) {
        if (layout.cell(nx::narrow<std::size_t>(index), entries.size()).holds(cursor.x, cursor.y)) {
            selection = entry.item;
            buy(entry.item);
            return;
        }
    }
    if (layout.buyButton().holds(cursor.x, cursor.y)) {
        buySelected(view);
    }
    if (layout.againTile().holds(cursor.x, cursor.y) && buyAgainOf(content, view)) {
        buyAgain();
    }
}

} // namespace cs
