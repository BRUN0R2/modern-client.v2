// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Buying (docs/10-counter-strike-gameplay.md, section 8): who may buy, and what a weapon, armor and a box
/// of ammo cost a buyer, data/cstrike/buying.toml with what a weapon's file and ammo.toml say, by
/// ReGameDLL_CS's rules (CBasePlayer::CanPlayerBuy, CanBuyThis, BuyWeaponByWeaponID, BuyItem, BuyGunAmmo and
/// BuyAmmo; GPL, consulted: docs/20 section 3). Pure rules: the game weighs them and acts (cs::Game).
///
/// Today: the weapons, the armor and the ammo; the grenades, the equipment, autobuy, rebuy and the menus
/// come with them (TODO.md).
export module cs.shared:buying;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :player;
import :sounds;
import :weapons;
import :inventory;

export namespace cs {

/// Why the data of buying could not be read.
enum class BuyingError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// Why a player could not buy what it asked for, in the order the rules weigh it.
enum class BuyRefusal : std::uint8_t {
    NoMap,           ///< No map is played: a client's own, which the game never says.
    NoMenu,          ///< The client cannot show the buy menu: its own too.
    NotSold,         ///< What it asked for is not sold at all (the knife).
    NotForItsTeam,   ///< Its team is not sold it (#Alias_Not_Avail, #Cannot_Buy_This).
    Dead,            ///< It is dead (CanPlayerBuy's deadflag).
    OutsideBuyZones, ///< It is not where its team buys (SIGNAL_BUY).
    TimeOver,        ///< The round started longer ago than mp_buytime (#Cant_buy).
    TeamMayNotBuy,   ///< The map lets its team buy nothing (#CT_cant_buy, #Terrorist_cant_buy).
    AlreadyOwned,    ///< It has that weapon (#Cstrike_Already_Own_Weapon).
    ArmorWhole,      ///< It wears what it asked for, whole (#Already_Have_Kevlar, _Helmet).
    NoGun,           ///< It has no weapon of that slot that fires ammo.
    AmmoFull,        ///< It carries all the rounds of that ammo it can (BuyGunAmmo).
    NotEnoughMoney,  ///< It has less money than what it asked for costs (#Not_Enough_Money).
    Count,
};

/// How many boxes of ammo a purchase asks for: one (buyammo1, buyammo2), or as many as the player can
/// carry and pay for (primammo, secammo).
enum class AmmoPurchase : std::uint8_t {
    OneBox,
    AsManyBoxes,
};

/// What armor costs (BuyItem).
struct ArmorPrices {
    nx::Money kevlar{};          ///< A vest, or a vest under a helmet worn (KEVLAR_PRICE).
    nx::Money kevlarAndHelmet{}; ///< Both (ASSAULTSUIT_PRICE).
    nx::Money helmet{};          ///< A helmet alone, over a whole vest (HELMET_PRICE).
};

/// What a player hears as it buys.
struct BuyingSounds {
    GameSound weapon{};
    GameSound armor{};
    GameSound ammo{};
};

/// data/cstrike/buying.toml.
struct BuyingRules {
    /// How far from a spawn point of its team a player buys on a map without buy zones (OLD_CheckBuyZone).
    nx::HammerUnits spawnReach{};
    ArmorPrices armor{};
    BuyingSounds sounds{};

    /// Every file its sounds play, each once, in the order the file names them.
    [[nodiscard]] auto soundFiles() const -> std::vector<std::string_view>;
};

/// Reads `text`, the file `sourceName`: each sound with a file. Errors: DataInvalid.
[[nodiscard]] auto readBuyingRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BuyingRules, BuyingError>;

/// The least time a round's players buy for (MIN_BUY_TIME).
inline constexpr nx::Seconds kLeastBuyTime{15.0F};

/// mp_buytime as the game reads it, and what it sets it to where it holds it.
struct BuyTime {
    nx::Seconds length{};
    std::optional<float> setToMinutes{}; ///< What mp_buytime is set to where it was held.
};

/// mp_buytime's `minutes` read as CanPlayerBuy reads them: times 60 in float, cut to whole seconds as the
/// original's processors cut them (nx::truncateAsX86), held to kLeastBuyTime at least, mp_buytime then set
/// to it.
[[nodiscard]] auto readBuyTime(float minutes) noexcept -> BuyTime;

/// A player as it asks to buy, as the rules weigh it: its team and its money, whether it lives, whether it
/// is where its team buys (SIGNAL_BUY), whether its map lets its team buy, and how long ago its round
/// started of how long its players buy for.
struct Buyer {
    Team team{};
    nx::Money money{};
    bool isAlive = false;
    bool isAtBuyingPlace = false;
    bool mayTeamBuy = false;
    nx::Seconds sinceRoundStart{};
    nx::Seconds buyTime{};
};

/// Whether `buyer` may buy at all, as CanPlayerBuy weighs it. Errors: Dead, OutsideBuyZones, TimeOver,
/// TeamMayNotBuy.
[[nodiscard]] auto checkMayBuy(const Buyer& buyer) -> nx::Result<void, BuyRefusal>;

/// What `weapon` costs `buyer`, who has it when `isOwned` (HandleBuyAliasCommands, BuyWeaponByWeaponID,
/// CanBuyThis): whether its team is sold it is weighed before all else. Errors: NotSold, NotForItsTeam,
/// those of checkMayBuy, AlreadyOwned, NotEnoughMoney.
[[nodiscard]] auto weaponPriceFor(const Weapon& weapon, const Buyer& buyer, bool isOwned)
    -> nx::Result<nx::Money, BuyRefusal>;

/// What armor of `asked`, which is some, costs a player who wears what `status` says (BuyItem): a vest, of
/// armor not whole; a vest and a helmet, by what of them it wears; none when it wears all of that, whole.
[[nodiscard]] auto armorCostOf(const ArmorPrices& prices, const PlayerStatus& status,
                               ArmorKind asked) noexcept -> std::optional<nx::Money>;

/// What armor of `asked`, which is some, costs `buyer`, who wears what `status` says (armorCostOf). Errors:
/// those of checkMayBuy, ArmorWhole, NotEnoughMoney.
[[nodiscard]] auto armorPriceFor(const ArmorPrices& prices, const Buyer& buyer, const PlayerStatus& status,
                                 ArmorKind asked) -> nx::Result<nx::Money, BuyRefusal>;

/// What a box of `ammo` costs `buyer`, who carries `carried` rounds of it (BuyGunAmmo). Errors: those of
/// checkMayBuy, AmmoFull, NotEnoughMoney.
[[nodiscard]] auto ammoPriceFor(const AmmoType& ammo, const Buyer& buyer, std::uint32_t carried)
    -> nx::Result<nx::Money, BuyRefusal>;

/// What a player had after its last purchase, which it can buy again (m_rebuyStruct, BuildRebuyStruct): the
/// newest weapons of its primary and pistol slots, the rounds of their ammo it carried, and its armor.
struct LastPurchase {
    std::optional<WeaponIndex> primary{};
    std::uint32_t primaryRounds = 0;
    std::optional<WeaponIndex> pistol{};
    std::uint32_t pistolRounds = 0;
    ArmorKind armor = ArmorKind::None;

    [[nodiscard]] inline auto operator==(const LastPurchase&) const noexcept -> bool = default;
};

/// What a player of `inventory` and `status`, of `weapons`, is left with after a purchase.
[[nodiscard]] auto lastPurchaseOf(const Inventory& inventory, const PlayerStatus& status,
                                  const WeaponCatalog& weapons) -> LastPurchase;

/// A step of buying again for ammo: as much as can be carried and paid for, for `gun`, of `slot`.
struct AmmoRebuy {
    WeaponSlot slot{};
    WeaponIndex gun{};

    [[nodiscard]] inline auto operator==(const AmmoRebuy&) const noexcept -> bool = default;
};

/// A step of buying again: a weapon, ammo, or armor.
using RebuyStep = std::variant<WeaponIndex, AmmoRebuy, ArmorKind>;

/// What a player of `inventory` and `status` buys to have `last` again, in the order of `cl_rebuy`'s default
/// (Rebuy): a primary weapon when it has none (RebuyPrimaryWeapon), the ammo of the one it will have when it
/// carried more (RebuyPrimaryAmmo), the pistol when it has another (RebuySecondaryWeapon), its ammo likewise,
/// and armor better than it wears (RebuyArmor).
[[nodiscard]] auto rebuyStepsOf(const LastPurchase& last, const Inventory& inventory,
                                const PlayerStatus& status, const WeaponCatalog& weapons)
    -> std::vector<RebuyStep>;

/// What `steps` cost a player of `inventory` and `status`: the boxes of their ammo up to all it can carry.
[[nodiscard]] auto rebuyCostOf(std::span<const RebuyStep> steps, const Inventory& inventory,
                               const PlayerStatus& status, const WeaponCatalog& weapons,
                               const ArmorPrices& prices) -> nx::Money;

/// Whether a player may buy now and for how long its round's players still do, as its client is told them
/// (the original's buy zone icon and `BuyTime`).
struct BuyingState {
    std::optional<BuyRefusal> refusal{}; ///< Why it may not, as checkMayBuy weighs it; none: it may.
    nx::Seconds timeLeft{};
    nx::Seconds length{}; ///< How long a round's players buy for (mp_buytime).
};

/// What `buyer` is told: its buy time does not run down while its round `isFrozen`, which starts it again.
[[nodiscard]] auto buyingStateOf(const Buyer& buyer, bool isFrozen) -> BuyingState;

} // namespace cs

template <>
struct nx::meta::Description<cs::ArmorPrices> {
    static constexpr std::string_view kName = "cs::ArmorPrices";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::ArmorPrices::kevlar>("kevlar", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::ArmorPrices::kevlarAndHelmet>("kevlarAndHelmet",
                                                           nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::ArmorPrices::helmet>("helmet", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BuyingSounds> {
    static constexpr std::string_view kName = "cs::BuyingSounds";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BuyingSounds::weapon>("weapon"),
        nx::meta::field<&cs::BuyingSounds::armor>("armor"),
        nx::meta::field<&cs::BuyingSounds::ammo>("ammo"),
    };
};

template <>
struct nx::meta::Description<cs::BuyingRules> {
    static constexpr std::string_view kName = "cs::BuyingRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BuyingRules::spawnReach>("spawnReach", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BuyingRules::armor>("armor"),
        nx::meta::field<&cs::BuyingRules::sounds>("sounds"),
    };
};
