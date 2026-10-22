// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The weapons of Counter-Strike as data (docs/10-counter-strike-gameplay.md, section 7.2): what each
/// is, the slot it takes, who buys it and for how much, the ammo it fires and how much of it its clip
/// holds, how fast its holder runs, and the models it is seen by. The game's own are in
/// data/cstrike/weapons/<weapon>.toml and data/cstrike/ammo.toml, with ReGameDLL_CS's values (GPL,
/// consulted: docs/20 section 3).
///
/// Today: what a weapon is and how it is held; its animations, how it fires and reloads, its spread
/// and its recoil come with the weapons' state machine (TODO.md).
export module cs.shared:weapons;

import std;
import nx.core;
import nx.meta;
import nx.data;

export namespace cs {

/// What can be wrong with the weapons' data.
enum class WeaponError : std::uint8_t {
    DataInvalid, ///< A file breaks its schema or the catalog's rules (cause: the DataError or the
                 ///< VfsError, when there is one); the detail names the file, the weapon or the ammo.
};

/// The slots of a player's weapons, in the order the HUD's weapon menu numbers them, 1 to 5.
enum class WeaponSlot : std::uint8_t {
    Primary,   ///< Rifles, shotguns, submachine guns, sniper rifles and the machine gun.
    Secondary, ///< Pistols.
    Knife,
    Grenade,
    Bomb,
    Count,
};

/// Who may buy a weapon.
enum class Buyers : std::uint8_t {
    Terrorists,
    CounterTerrorists,
    Everyone,
    Count,
};

/// A kind of ammo: how much of it a player carries besides what the clips of its weapons hold, and
/// how buying sells it.
struct AmmoType {
    std::string id{};           ///< The original's name for it: "9mm", "762Nato".
    std::uint32_t maxCarried{}; ///< A player carries this many rounds of it at most.
    nx::Money boxPrice{};       ///< A box of it costs this much,
    std::uint32_t boxRounds{};  ///< and holds this many rounds.
};

/// data/cstrike/ammo.toml: the kinds of ammo.
struct AmmoTypes {
    std::vector<AmmoType> ammo{};
};

/// How a weapon is sold.
struct WeaponPurchase {
    Buyers buyers{};
    nx::Money price{};
};

/// The ammo a weapon fires, and how much of it its clip holds.
struct WeaponMagazine {
    std::string ammo{}; ///< The id of an AmmoType.
    std::uint32_t clipSize{};
};

/// The models a weapon is seen by.
struct WeaponModels {
    std::string view{};   ///< In its holder's own view: models/v_<weapon>.mdl.
    std::string player{}; ///< In its holder's hands, as the others see it: models/p_<weapon>.mdl.
    /// What its holder's model plays as it holds it: the end of the labels of its sequences ("ak47"
    /// of "ref_aim_ak47").
    std::string playerAnimation{};
};

/// A weapon: data/cstrike/weapons/<id>.toml.
struct Weapon {
    std::string id{};        ///< Its name, as the buy commands and its file call it: "ak47".
    std::string classname{}; ///< The class of the entity the game makes of it: "weapon_ak47".
    WeaponSlot slot{};
    std::uint32_t position{}; ///< The HUD's weapon menu lists a slot's weapons by it.
    /// When the game chooses a weapon for a player, as one runs out or is dropped, it chooses the
    /// heaviest of the others.
    std::uint32_t weight{};
    nx::UnitsPerSecond maxSpeed{};            ///< Its holder runs this fast at most.
    std::optional<WeaponPurchase> purchase{}; ///< None: it is not sold (the knife).
    std::optional<WeaponMagazine> magazine{}; ///< None: it fires no ammo (the knife).
    WeaponModels models{};
};

/// Reads `text`, the file `sourceName`: the kinds of ammo. Errors: DataInvalid.
[[nodiscard]] auto readAmmoTypes(std::string_view text, std::string_view sourceName)
    -> nx::Result<AmmoTypes, WeaponError>;

/// Reads `text`, the file `sourceName`: a weapon, its id and classname not empty, its models paths
/// of the game's files. Errors: DataInvalid.
[[nodiscard]] auto readWeapon(std::string_view text, std::string_view sourceName)
    -> nx::Result<Weapon, WeaponError>;

/// The game's weapons and kinds of ammo, each checked against the others.
class WeaponCatalog {
public:
    /// The catalog of `ammo` and `weapons`, in their order. Errors: DataInvalid: an id twice (of a
    /// kind of ammo, or of a weapon), a classname twice, two weapons at one position of a slot, or a
    /// weapon firing a kind of ammo `ammo` lacks.
    [[nodiscard]] static auto make(AmmoTypes ammo, std::vector<Weapon> weapons)
        -> nx::Result<WeaponCatalog, WeaponError>;

    [[nodiscard]] inline auto ammo() const noexcept -> std::span<const AmmoType> { return m_ammo; }
    [[nodiscard]] inline auto weapons() const noexcept -> std::span<const Weapon> { return m_weapons; }

    /// The kind of ammo `id` names; none when the catalog has none of that id.
    [[nodiscard]] auto findAmmo(std::string_view id) const noexcept -> const AmmoType*;
    /// The weapon `id` names; none when the catalog has none of that id.
    [[nodiscard]] auto findWeapon(std::string_view id) const noexcept -> const Weapon*;

private:
    WeaponCatalog(std::vector<AmmoType> ammo, std::vector<Weapon> weapons) noexcept;

    std::vector<AmmoType> m_ammo;
    std::vector<Weapon> m_weapons;
};

} // namespace cs

template <>
struct nx::meta::Description<cs::AmmoType> {
    static constexpr std::string_view kName = "cs::AmmoType";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::AmmoType::id>("id"),
        nx::meta::field<&cs::AmmoType::maxCarried>("maxCarried"),
        nx::meta::field<&cs::AmmoType::boxPrice>("boxPrice"),
        nx::meta::field<&cs::AmmoType::boxRounds>("boxRounds", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::AmmoTypes> {
    static constexpr std::string_view kName = "cs::AmmoTypes";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::AmmoTypes::ammo>("ammo"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponPurchase> {
    static constexpr std::string_view kName = "cs::WeaponPurchase";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponPurchase::buyers>("buyers"),
        nx::meta::field<&cs::WeaponPurchase::price>("price", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponMagazine> {
    static constexpr std::string_view kName = "cs::WeaponMagazine";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponMagazine::ammo>("ammo"),
        nx::meta::field<&cs::WeaponMagazine::clipSize>("clipSize", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponModels> {
    static constexpr std::string_view kName = "cs::WeaponModels";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponModels::view>("view"),
        nx::meta::field<&cs::WeaponModels::player>("player"),
        nx::meta::field<&cs::WeaponModels::playerAnimation>("playerAnimation"),
    };
};

template <>
struct nx::meta::Description<cs::Weapon> {
    static constexpr std::string_view kName = "cs::Weapon";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Weapon::id>("id"),
        nx::meta::field<&cs::Weapon::classname>("classname"),
        nx::meta::field<&cs::Weapon::slot>("slot"),
        nx::meta::field<&cs::Weapon::position>("position"),
        nx::meta::field<&cs::Weapon::weight>("weight"),
        nx::meta::field<&cs::Weapon::maxSpeed>("maxSpeed", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::Weapon::purchase>("purchase"),
        nx::meta::field<&cs::Weapon::magazine>("magazine"),
        nx::meta::field<&cs::Weapon::models>("models"),
    };
};
