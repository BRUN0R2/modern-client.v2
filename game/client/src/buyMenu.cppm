// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The buy menu (docs/12-ui-hud-and-console.md, section 4), the project's own look: tabs of what is sold,
/// the items of the tab shown, and what the item selected is, by the original's keys: a first key opens a
/// tab, the next buys on it, so that `b` `4` `2` buys what it always did. What it sells comes from the
/// weapons' files and data/cstrike/buyMenu.toml, a weapon made as the game runs too, each weighed by the
/// rules that weigh a purchase (cs.shared:buying). Here is its state as the keys and the mouse change it, and
/// what it shows (BuyMenuFrame); drawing it is apart (cs.client:buyMenuPanel).
export module cs.client:buyMenu;

import std;
import nx.core;
import nx.rhi;
import nx.platform;
import cs.shared;
import :buyCommands;
import :buyMenuLayout;

export namespace cs {

/// The key that opens each tab, as the original's first menu numbers it.
inline constexpr nx::EnumArray<BuyTab, std::uint32_t> kBuyTabKeys{
    nx::EnumArray<BuyTab, std::uint32_t>::Storage{1, 2, 3, 4, 5, 8}};
/// The keys that buy a box of ammo for the primary weapon and the pistol at once, as in the original's first
/// menu.
inline constexpr std::uint32_t kPrimaryAmmoKey = 6;
inline constexpr std::uint32_t kPistolAmmoKey = 7;
/// The key that goes back a level: `slot10`, on the key 0.
inline constexpr std::uint32_t kBuyMenuBackKey = 10;

/// What the menu sells, of the game's content, which must outlive it.
struct BuyMenuContent {
    const WeaponCatalog* weapons = nullptr;
    const ArmorPrices* armorPrices = nullptr;
    const BuyMenuLook* look = nullptr;
    float armorRatio = 0.0F; ///< What of a hit armor lets through, before its weapon's ratio (ArmorShares).
};

/// The player the menu shows, as its server told last.
struct BuyerView {
    Team team{};
    PlayerStatus status{};
    const Inventory* inventory = nullptr;
    BuyingState buying{};
};

/// What the menu sells: a weapon of the catalog, or armor.
using BuyItem = std::variant<WeaponIndex, ArmorKind>;

/// How what the menu sells stands for the player.
enum class BuyItemState : std::uint8_t {
    ForSale, ///< It can buy it.
    TooDear, ///< It has less money than it costs.
    Owned,   ///< It has it, or wears it whole, or carries all it can of it.
};

/// An item on a tab.
struct BuyEntry {
    BuyItem item{};
    std::uint32_t key = 0; ///< What buys it once its tab is open; of two at one key, the first.
    std::string_view name{};
    std::string_view picture{};
    nx::Money price{};
    BuyItemState state{};

    [[nodiscard]] inline auto operator==(const BuyEntry&) const noexcept -> bool = default;
};

/// The items of `tab` for the player of `view`, from the lowest key: those sold to its team, of weapons
/// complete and not replaced by another. A weapon of no place on the menu is bought by its commands alone.
[[nodiscard]] auto buyEntriesOf(BuyTab tab, const BuyMenuContent& content, const BuyerView& view)
    -> std::vector<BuyEntry>;

/// The tabs that have items for the player of `view`, in their order.
[[nodiscard]] auto buyTabsOf(const BuyMenuContent& content, const BuyerView& view) -> std::vector<BuyTab>;

/// A kind of ammo as a row of the menu tells of it.
struct BuyAmmoFacts {
    std::string_view name{};
    std::uint32_t carried = 0;
    std::uint32_t most = 0;
    std::uint32_t boxRounds = 0;
    nx::Money boxPrice{};

    [[nodiscard]] inline auto operator==(const BuyAmmoFacts&) const noexcept -> bool = default;
};

/// A row of ammo: the key that buys a box of it and the slot of the weapon that fires it.
struct BuyAmmoRow {
    std::uint32_t key = 0;
    WeaponSlot slot{};
    std::optional<BuyAmmoFacts> ammo{}; ///< None: the player has no weapon of that slot that fires ammo.
    BuyItemState state{};

    [[nodiscard]] inline auto operator==(const BuyAmmoRow&) const noexcept -> bool = default;
};

/// What the menu tells of a gun: what a round deals, how many it fires a minute (its firing's cycle), whether
/// each press fires once, the share of a hit armor lets through to its health, how fast it lets its holder
/// run, its clip, the rounds of its ammo a player carries at most, how long its reload takes, and its ammo.
struct BuyGunFacts {
    std::int32_t damage = 0;
    std::int32_t roundsAMinute = 0;
    bool isSemiAutomatic = false;
    float throughArmor = 0.0F;
    nx::UnitsPerSecond moveSpeed{};
    std::uint32_t clip = 0;
    std::uint32_t carried = 0;
    nx::Seconds reload{};
    std::string_view ammoName{};
    std::uint32_t boxRounds = 0;
    nx::Money boxPrice{};

    [[nodiscard]] inline auto operator==(const BuyGunFacts&) const noexcept -> bool = default;
};

/// What the menu tells of `weapon`, of `content`'s; none for one that does not fire ammo.
[[nodiscard]] auto buyGunFactsOf(const Weapon& weapon, const BuyMenuContent& content)
    -> std::optional<BuyGunFacts>;

/// What the menu tells of the item selected: its tab and who it is sold to (a weapon's), what it does
/// (armor's) or its facts (a gun's), and the weapon it takes the place of, by its name; none: it takes no
/// other's.
struct BuyDetail {
    BuyEntry entry{};
    BuyTab tab{};
    std::optional<Buyers> buyers{};
    std::string_view about{};
    std::optional<BuyGunFacts> gun{};
    std::string_view replaces{};

    [[nodiscard]] inline auto operator==(const BuyDetail&) const noexcept -> bool = default;
};

/// A gun the player carries: its picture, the rounds in its clip and those of its ammo besides.
struct BuyCarriedGun {
    std::string_view picture{};
    std::uint32_t clip = 0;
    std::uint32_t carried = 0;

    [[nodiscard]] inline auto operator==(const BuyCarriedGun&) const noexcept -> bool = default;
};

/// What the player carries: its primary weapon, its pistol and its armor, with the picture of what it wears.
struct BuyCarried {
    std::optional<BuyCarriedGun> primary{};
    std::optional<BuyCarriedGun> pistol{};
    ArmorKind armorKind = ArmorKind::None;
    nx::ArmorPoints armor{};
    std::string_view armorPicture{};

    [[nodiscard]] inline auto operator==(const BuyCarried&) const noexcept -> bool = default;
};

/// A tab the menu lists, its picture its first item's.
struct BuyMenuTab {
    BuyTab tab{};
    std::string_view picture{};

    [[nodiscard]] inline auto operator==(const BuyMenuTab&) const noexcept -> bool = default;
};

/// All the menu shows: its player's team, money and time left, in whole seconds; its tabs, the one shown and
/// whether the next key buys on it; its items, what the one selected is, the ammo and what the player
/// carries.
struct BuyMenuFrame {
    Team team{};
    nx::Money money{};
    std::uint32_t secondsLeft = 0;
    std::uint32_t secondsInAll = 0;
    std::vector<BuyMenuTab> tabs{};
    BuyTab tab{};
    bool isArmed = false;
    std::vector<BuyEntry> entries{};
    std::optional<BuyDetail> detail{};
    std::array<BuyAmmoRow, 2> ammo{};
    BuyCarried carried{};

    [[nodiscard]] inline auto operator==(const BuyMenuFrame&) const noexcept -> bool = default;
};

/// The menu's state, as the keys and the mouse change it: whether it is open, the tab it shows, whether the
/// next key buys on it (armed), and the item selected. It stays open as the player buys; the player goes on
/// walking with it open, the keys it does not take going to their binds.
class BuyMenu {
public:
    /// The menu of `sold`, buying through `buying`, which also tells of what the game refuses.
    BuyMenu(const BuyMenuContent& sold, BuyActions buying) noexcept;

    [[nodiscard]] inline auto isOpen() const noexcept -> bool { return isShown; }
    [[nodiscard]] inline auto tab() const noexcept -> BuyTab { return shownTab; }
    [[nodiscard]] inline auto isArmed() const noexcept -> bool { return isTabArmed; }
    [[nodiscard]] inline auto selected() const noexcept -> const std::optional<BuyItem>& { return selection; }

    /// `buy`: opens on the tab it last showed, or closes. Errors: why the player of `view` may not buy, the
    /// menu staying closed.
    [[nodiscard]] auto toggle(const BuyerView& view) -> nx::Result<void, BuyRefusal>;
    /// `buyequip`: opens on the equipment tab, armed. Errors: as toggle's.
    [[nodiscard]] auto openEquipment(const BuyerView& view) -> nx::Result<void, BuyRefusal>;
    void close() noexcept;
    /// Back a level: from a tab armed to the keys of the tabs, from those to closed.
    void back() noexcept;
    /// Closes once the player of `view` may not buy, telling why but when it is dead.
    void update(const BuyerView& view);

    /// `slot1` to `slot10` (slot), open: a first key opens a tab or buys a box of ammo, the next buys on the
    /// tab; kBuyMenuBackKey goes back. A key of nothing does nothing.
    void pressSlot(std::uint32_t slot, const BuyerView& view);
    /// Buys the item selected.
    void buySelected(const BuyerView& view);

    /// What `event` does to the menu, open, on a screen of `size` with the mouse's cursor at `cursor`: the
    /// mouse moved selects what it is over, its left button buys it or opens a tab, Escape goes back, Enter
    /// buys. Returns whether it took the event: not what has to reach the binds, as a key or a button coming
    /// up, nor the keys it has no use for.
    auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                nx::rhi::Extent2d size, const BuyerView& view) -> bool;

    /// What it shows of the player of `view`.
    [[nodiscard]] auto frameOf(const BuyerView& view) const -> BuyMenuFrame;

private:
    /// Shows `tab`, its first item selected, armed as `arms` says.
    void showTab(BuyTab tab, const BuyerView& view, bool arms);
    /// Opens, on a tab with items for the player of `view`. Errors: as toggle's.
    [[nodiscard]] auto open(const BuyerView& view) -> nx::Result<void, BuyRefusal>;
    void buy(const BuyItem& item);
    void buyAmmo(WeaponSlot slot);
    /// The item under `cursor` selected, when one is.
    void hover(nx::platform::MousePosition cursor, nx::rhi::Extent2d size, const BuyerView& view);
    /// What is under `cursor` opened or bought.
    void click(nx::platform::MousePosition cursor, nx::rhi::Extent2d size, const BuyerView& view);

    BuyMenuContent content;
    BuyActions actions;
    bool isShown = false;
    bool isTabArmed = false;
    BuyTab shownTab = BuyTab::Rifles;
    std::optional<BuyItem> selection{};
};

} // namespace cs
