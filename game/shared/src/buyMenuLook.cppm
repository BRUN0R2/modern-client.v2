// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the buy menu shows besides what each weapon's file says of its place on it
/// (docs/12-ui-hud-and-console.md, section 4): data/cstrike/buyMenu.toml, the project's own.
export module cs.shared:buyMenuLook;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :player;

export namespace cs {

/// Why the data of the buy menu could not be read.
enum class BuyMenuLookError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// Armor as the buy menu's equipment tab shows it: which, the key that buys it once the tab is open, what it
/// is called, its picture (a TGA among the game's files) and what it does.
struct BuyMenuArmor {
    ArmorKind kind{};
    std::uint32_t key{};
    std::string name{};
    std::string picture{};
    std::string about{};
};

/// The font families the buy menu writes in, of the project's own fonts (data/fonts/): for its text, and for
/// what stands out in it.
struct BuyMenuFontFamilies {
    std::string regular{};
    std::string strong{};
};

/// data/cstrike/buyMenu.toml: the families the menu writes in, and its armor.
struct BuyMenuLook {
    BuyMenuFontFamilies font{};
    std::vector<BuyMenuArmor> armor{};
};

/// Reads `text`, the file `sourceName`: both families, each item of armor of some kind, named and with a
/// picture, no two of one kind or at one key. Errors: DataInvalid.
[[nodiscard]] auto readBuyMenuLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<BuyMenuLook, BuyMenuLookError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::BuyMenuArmor> {
    static constexpr std::string_view kName = "cs::BuyMenuArmor";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BuyMenuArmor::kind>("kind"),
        nx::meta::field<&cs::BuyMenuArmor::key>("key", nx::data::Range{.minimum = 1.0, .maximum = 9.0}),
        nx::meta::field<&cs::BuyMenuArmor::name>("name"),
        nx::meta::field<&cs::BuyMenuArmor::picture>("picture"),
        nx::meta::field<&cs::BuyMenuArmor::about>("about"),
    };
};

template <>
struct nx::meta::Description<cs::BuyMenuFontFamilies> {
    static constexpr std::string_view kName = "cs::BuyMenuFontFamilies";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BuyMenuFontFamilies::regular>("regular"),
        nx::meta::field<&cs::BuyMenuFontFamilies::strong>("strong"),
    };
};

template <>
struct nx::meta::Description<cs::BuyMenuLook> {
    static constexpr std::string_view kName = "cs::BuyMenuLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BuyMenuLook::font>("font"),
        nx::meta::field<&cs::BuyMenuLook::armor>("armor"),
    };
};
