// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the main menu lists and writes in (docs/12-ui-hud-and-console.md, section 4):
/// data/cstrike/mainMenu.toml, the project's own, as the original's resource/GameMenu.res lists its menu's.
export module cs.shared:mainMenuLook;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :menuFonts;

export namespace cs {

/// Why the data of the main menu could not be read.
enum class MainMenuLookError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// What an item of the main menu does.
enum class MainMenuAction : std::uint8_t {
    ResumeGame, ///< Closes the menu over the map played (the original's `ResumeGame`).
    Disconnect, ///< Leaves the map played (`Disconnect`).
    NewGame,    ///< Plays a map of the game's, chosen on its page (`OpenCreateMultiplayerGameDialog`).
    Quit,       ///< Ends the client (`Quit`).
    Count,
};

/// When the main menu lists an item.
enum class MainMenuListing : std::uint8_t {
    Always,
    InGame, ///< Over a map played alone (the original's `OnlyInGame`).
    Count,
};

/// An item of the main menu: what it does, and when the menu lists it.
struct MainMenuItem {
    MainMenuAction action{};
    MainMenuListing listed{};

    [[nodiscard]] inline auto operator==(const MainMenuItem&) const noexcept -> bool = default;
};

/// data/cstrike/mainMenu.toml: the families the menu writes in, the layout of the picture behind it among
/// the game's files (the original's resource/BackgroundLayout.txt), and its items from the top.
struct MainMenuLook {
    MenuFontFamilies font{};
    std::string background{};
    std::vector<MainMenuItem> items{};
};

/// Reads `text`, the file `sourceName`: both families, a background, and items of which none does what
/// another does and one at least is always listed, so that the menu is never empty. Errors: DataInvalid.
[[nodiscard]] auto readMainMenuLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<MainMenuLook, MainMenuLookError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::MainMenuItem> {
    static constexpr std::string_view kName = "cs::MainMenuItem";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::MainMenuItem::action>("action"),
        nx::meta::field<&cs::MainMenuItem::listed>("listed"),
    };
};

template <>
struct nx::meta::Description<cs::MainMenuLook> {
    static constexpr std::string_view kName = "cs::MainMenuLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::MainMenuLook::font>("font"),
        nx::meta::field<&cs::MainMenuLook::background>("background"),
        nx::meta::field<&cs::MainMenuLook::items>("items"),
    };
};
