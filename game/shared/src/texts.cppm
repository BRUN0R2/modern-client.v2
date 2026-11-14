// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the game writes to the player, in the player's language (docs/12-ui-hud-and-console.md, section
/// 7; ADR-0035): a field for each text, so that a text the code asks for and no language has does not
/// build, and a language's file, data/cstrike/locale/<language>.toml, that must write every one of them.
export module cs.shared:texts;

import std;
import nx.core;
import nx.meta;
import nx.data;

export namespace cs {

/// Why the texts of a language could not be read.
enum class TextsError : std::uint8_t {
    UnknownLanguage, ///< The language is no tag, or has no file among the game's data (cause).
    DataInvalid, ///< Its file lacks a text, writes one empty or has one the game does not ask for (cause).
};

/// What the main menu writes: its items, the page of a new game, the loading screen, and the keys its foot
/// tells with what they do.
struct MainMenuTexts {
    std::string resumeGame{};
    std::string disconnect{};
    std::string newGame{};
    std::string options{};
    std::string quit{};
    std::string map{};
    std::string noMaps{};
    std::string start{};
    std::string back{};
    std::string loading{};
    std::string keyUpDown{};
    std::string keyEnter{};
    std::string keyEscape{};
    std::string select{};
    std::string choose{};
    std::string resume{};
    std::string play{};
    std::string goBack{};
    std::string keyLeftRight{};
    std::string change{};
    std::string keyDelete{};
    std::string clear{};
};

/// What the options write: their tabs, their settings, how a switch stands, and the actions the keys run,
/// with what an action that waits for its key and one that has none say.
struct OptionsTexts {
    std::string mouse{};
    std::string keyboard{};
    std::string audio{};
    std::string video{};
    std::string game{};
    std::string sensitivity{};
    std::string reverseMouse{};
    std::string volume{};
    std::string playUnfocused{};
    std::string maxFps{};
    std::string vsync{};
    std::string gamma{};
    std::string brightness{};
    std::string fidelity{};
    std::string fidelityLegacy{};
    std::string fidelityEnhanced{};
    std::string language{};
    std::string onNextStart{};
    std::string onNextMap{};
    std::string showFps{};
    std::string drawHud{};
    std::string autoSwitch{};
    std::string rightHand{};
    std::string on{};
    std::string off{};
    std::string pressAKey{};
    std::string noKey{};
    std::string moveForward{};
    std::string moveBack{};
    std::string moveLeft{};
    std::string moveRight{};
    std::string jump{};
    std::string duck{};
    std::string walk{};
    std::string use{};
    std::string fire{};
    std::string secondaryFire{};
    std::string reload{};
    std::string lastWeapon{};
    std::string buyMenu{};
    std::string buyEquipment{};
    std::string buyPrimaryAmmo{};
    std::string buyPistolAmmo{};
    std::string rebuy{};
    std::string showScores{};
    std::string toggleConsole{};
    std::string noclip{};
};

/// Every text of the game, by the part that writes it.
struct Texts {
    MainMenuTexts mainMenu{};
    OptionsTexts options{};
};

/// Whether `language` is a language tag a file can be named after: letters, digits and `-` between them
/// ("en", "pt-BR"), so that a name from a cvar never reaches outside the folder of the languages.
[[nodiscard]] auto isLanguageTag(std::string_view language) noexcept -> bool;

/// Reads `text`, the file `sourceName` of a language: every text, none empty, and none the game does not
/// ask for. Errors: DataInvalid.
[[nodiscard]] auto readTexts(std::string_view text, std::string_view sourceName)
    -> nx::Result<Texts, TextsError>;

/// The languages `data`, the project's data/ folder, has the game's texts in: the names of its
/// cstrike/locale/<language>.toml files, in the order of the alphabet. Errors: UnknownLanguage (the folder
/// cannot be read).
[[nodiscard]] auto languagesOf(const std::filesystem::path& data)
    -> nx::Result<std::vector<std::string>, TextsError>;

/// The game's texts in `language`, from `data`, the project's data/ folder. Errors: UnknownLanguage, its
/// detail the languages there are; DataInvalid.
[[nodiscard]] auto openTexts(const std::filesystem::path& data, std::string_view language)
    -> nx::Result<Texts, TextsError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::MainMenuTexts> {
    static constexpr std::string_view kName = "cs::MainMenuTexts";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::MainMenuTexts::resumeGame>("resumeGame"),
        nx::meta::field<&cs::MainMenuTexts::disconnect>("disconnect"),
        nx::meta::field<&cs::MainMenuTexts::newGame>("newGame"),
        nx::meta::field<&cs::MainMenuTexts::options>("options"),
        nx::meta::field<&cs::MainMenuTexts::quit>("quit"),
        nx::meta::field<&cs::MainMenuTexts::map>("map"),
        nx::meta::field<&cs::MainMenuTexts::noMaps>("noMaps"),
        nx::meta::field<&cs::MainMenuTexts::start>("start"),
        nx::meta::field<&cs::MainMenuTexts::back>("back"),
        nx::meta::field<&cs::MainMenuTexts::loading>("loading"),
        nx::meta::field<&cs::MainMenuTexts::keyUpDown>("keyUpDown"),
        nx::meta::field<&cs::MainMenuTexts::keyEnter>("keyEnter"),
        nx::meta::field<&cs::MainMenuTexts::keyEscape>("keyEscape"),
        nx::meta::field<&cs::MainMenuTexts::select>("select"),
        nx::meta::field<&cs::MainMenuTexts::choose>("choose"),
        nx::meta::field<&cs::MainMenuTexts::resume>("resume"),
        nx::meta::field<&cs::MainMenuTexts::play>("play"),
        nx::meta::field<&cs::MainMenuTexts::goBack>("goBack"),
        nx::meta::field<&cs::MainMenuTexts::keyLeftRight>("keyLeftRight"),
        nx::meta::field<&cs::MainMenuTexts::change>("change"),
        nx::meta::field<&cs::MainMenuTexts::keyDelete>("keyDelete"),
        nx::meta::field<&cs::MainMenuTexts::clear>("clear"),
    };
};

template <>
struct nx::meta::Description<cs::OptionsTexts> {
    static constexpr std::string_view kName = "cs::OptionsTexts";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::OptionsTexts::mouse>("mouse"),
        nx::meta::field<&cs::OptionsTexts::keyboard>("keyboard"),
        nx::meta::field<&cs::OptionsTexts::audio>("audio"),
        nx::meta::field<&cs::OptionsTexts::video>("video"),
        nx::meta::field<&cs::OptionsTexts::game>("game"),
        nx::meta::field<&cs::OptionsTexts::sensitivity>("sensitivity"),
        nx::meta::field<&cs::OptionsTexts::reverseMouse>("reverseMouse"),
        nx::meta::field<&cs::OptionsTexts::volume>("volume"),
        nx::meta::field<&cs::OptionsTexts::playUnfocused>("playUnfocused"),
        nx::meta::field<&cs::OptionsTexts::maxFps>("maxFps"),
        nx::meta::field<&cs::OptionsTexts::vsync>("vsync"),
        nx::meta::field<&cs::OptionsTexts::gamma>("gamma"),
        nx::meta::field<&cs::OptionsTexts::brightness>("brightness"),
        nx::meta::field<&cs::OptionsTexts::fidelity>("fidelity"),
        nx::meta::field<&cs::OptionsTexts::fidelityLegacy>("fidelityLegacy"),
        nx::meta::field<&cs::OptionsTexts::fidelityEnhanced>("fidelityEnhanced"),
        nx::meta::field<&cs::OptionsTexts::language>("language"),
        nx::meta::field<&cs::OptionsTexts::onNextStart>("onNextStart"),
        nx::meta::field<&cs::OptionsTexts::onNextMap>("onNextMap"),
        nx::meta::field<&cs::OptionsTexts::showFps>("showFps"),
        nx::meta::field<&cs::OptionsTexts::drawHud>("drawHud"),
        nx::meta::field<&cs::OptionsTexts::autoSwitch>("autoSwitch"),
        nx::meta::field<&cs::OptionsTexts::rightHand>("rightHand"),
        nx::meta::field<&cs::OptionsTexts::on>("on"),
        nx::meta::field<&cs::OptionsTexts::off>("off"),
        nx::meta::field<&cs::OptionsTexts::pressAKey>("pressAKey"),
        nx::meta::field<&cs::OptionsTexts::noKey>("noKey"),
        nx::meta::field<&cs::OptionsTexts::moveForward>("moveForward"),
        nx::meta::field<&cs::OptionsTexts::moveBack>("moveBack"),
        nx::meta::field<&cs::OptionsTexts::moveLeft>("moveLeft"),
        nx::meta::field<&cs::OptionsTexts::moveRight>("moveRight"),
        nx::meta::field<&cs::OptionsTexts::jump>("jump"),
        nx::meta::field<&cs::OptionsTexts::duck>("duck"),
        nx::meta::field<&cs::OptionsTexts::walk>("walk"),
        nx::meta::field<&cs::OptionsTexts::use>("use"),
        nx::meta::field<&cs::OptionsTexts::fire>("fire"),
        nx::meta::field<&cs::OptionsTexts::secondaryFire>("secondaryFire"),
        nx::meta::field<&cs::OptionsTexts::reload>("reload"),
        nx::meta::field<&cs::OptionsTexts::lastWeapon>("lastWeapon"),
        nx::meta::field<&cs::OptionsTexts::buyMenu>("buyMenu"),
        nx::meta::field<&cs::OptionsTexts::buyEquipment>("buyEquipment"),
        nx::meta::field<&cs::OptionsTexts::buyPrimaryAmmo>("buyPrimaryAmmo"),
        nx::meta::field<&cs::OptionsTexts::buyPistolAmmo>("buyPistolAmmo"),
        nx::meta::field<&cs::OptionsTexts::rebuy>("rebuy"),
        nx::meta::field<&cs::OptionsTexts::showScores>("showScores"),
        nx::meta::field<&cs::OptionsTexts::toggleConsole>("toggleConsole"),
        nx::meta::field<&cs::OptionsTexts::noclip>("noclip"),
    };
};

template <>
struct nx::meta::Description<cs::Texts> {
    static constexpr std::string_view kName = "cs::Texts";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Texts::mainMenu>("mainMenu"),
        nx::meta::field<&cs::Texts::options>("options"),
    };
};
