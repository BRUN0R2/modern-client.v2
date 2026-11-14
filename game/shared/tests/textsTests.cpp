// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The game's texts: every language of the repository writes every one of them, a file that does not is
// refused, and a language is one of those the data has.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

#include "temporaryDirectory.hpp"

namespace {

using cs::TextsError;

/// A language's file that writes every text the game asks for, the main menu's `quit` as `quit` says.
[[nodiscard]] auto fileWith(std::string_view quit) -> std::string {
    return std::format("[mainMenu]\n"
                       "resumeGame = \"a\"\ndisconnect = \"b\"\nnewGame = \"c\"\nquit = \"{}\"\nmap = \"e\"\n"
                       "noMaps = \"f\"\nstart = \"g\"\nback = \"h\"\nloading = \"i\"\nkeyUpDown = \"j\"\n"
                       "keyEnter = \"k\"\nkeyEscape = \"l\"\nselect = \"m\"\nchoose = \"n\"\nresume = \"o\"\n"
                       "play = \"p\"\ngoBack = \"q\"\noptions = \"r\"\nkeyLeftRight = \"s\"\nchange = "
                       "\"t\"\nkeyDelete = \"u\"\nclear = \"v\"\n"
                       "[options]\n"
                       "mouse = \"a\"\naudio = \"b\"\nvideo = \"c\"\ngame = \"d\"\nsensitivity = \"e\"\n"
                       "reverseMouse = \"f\"\nvolume = \"g\"\nplayUnfocused = \"h\"\nmaxFps = \"i\"\n"
                       "showFps = \"j\"\ndrawHud = \"k\"\nautoSwitch = \"l\"\nrightHand = \"m\"\non = \"n\"\n"
                       "off = \"o\"\nkeyboard = \"p\"\npressAKey = \"q\"\nnoKey = \"r\"\n"
                       "vsync = \"x\"\ngamma = \"x\"\nbrightness = \"x\"\nfidelity = \"x\"\n"
                       "fidelityLegacy = \"x\"\nfidelityEnhanced = \"x\"\nlanguage = \"x\"\n"
                       "moveForward = \"x\"\n"
                       "moveBack = \"x\"\n"
                       "moveLeft = \"x\"\n"
                       "moveRight = \"x\"\n"
                       "jump = \"x\"\n"
                       "duck = \"x\"\n"
                       "walk = \"x\"\n"
                       "use = \"x\"\n"
                       "fire = \"x\"\n"
                       "secondaryFire = \"x\"\n"
                       "reload = \"x\"\n"
                       "lastWeapon = \"x\"\n"
                       "buyMenu = \"x\"\n"
                       "buyEquipment = \"x\"\n"
                       "buyPrimaryAmmo = \"x\"\n"
                       "buyPistolAmmo = \"x\"\n"
                       "rebuy = \"x\"\n"
                       "showScores = \"x\"\n"
                       "toggleConsole = \"x\"\n"
                       "noclip = \"x\"\n",
                       quit);
}

[[nodiscard]] auto errorOf(std::string_view text) -> std::optional<TextsError> {
    const auto texts = cs::readTexts(text, "test");
    return texts ? std::nullopt : std::optional{texts.error().code};
}

TEST(TextsTest, EveryLanguageOfTheRepositoryWritesEveryText) {
    const std::filesystem::path data{NX_DATA_DIRECTORY};
    const auto languages = cs::languagesOf(data);
    ASSERT_TRUE(languages.has_value()) << (languages ? "" : nx::describe(languages.error()));
    EXPECT_EQ(*languages, (std::vector<std::string>{"en", "pt-BR"}));
    for (const std::string& language : *languages) {
        const auto texts = cs::openTexts(data, language);
        EXPECT_TRUE(texts.has_value()) << language << ": " << (texts ? "" : nx::describe(texts.error()));
    }
}

TEST(TextsTest, ALanguageWritesItsOwnTexts) {
    const std::filesystem::path data{NX_DATA_DIRECTORY};
    const auto english = cs::openTexts(data, "en");
    const auto portuguese = cs::openTexts(data, "pt-BR");
    ASSERT_TRUE(english.has_value());
    ASSERT_TRUE(portuguese.has_value());
    EXPECT_EQ(english->mainMenu.newGame, "NEW GAME");
    EXPECT_EQ(portuguese->mainMenu.newGame, "NOVO JOGO");
    EXPECT_EQ(english->options.on, "ON");
    EXPECT_EQ(portuguese->options.on, "LIGADO");
    EXPECT_EQ(portuguese->mainMenu.noMaps, "O jogo não tem mapas.") << "UTF-8, as the file is";
}

TEST(TextsTest, AFileThatDoesNotWriteEveryTextIsRefused) {
    EXPECT_EQ(errorOf(fileWith("d")), std::nullopt);
    EXPECT_EQ(errorOf(fileWith("")), TextsError::DataInvalid) << "a text written empty";
    EXPECT_EQ(errorOf(fileWith("d") + "extra = \"r\"\n"), TextsError::DataInvalid)
        << "a text the game does not ask for";
    std::string lacking = fileWith("d");
    lacking.erase(lacking.find("goBack"));
    EXPECT_EQ(errorOf(lacking), TextsError::DataInvalid) << "a text left out";
    std::string emptyInAnotherPart = fileWith("d");
    emptyInAnotherPart.replace(emptyInAnotherPart.find("off = \"o\""), std::string_view{"off = \"o\""}.size(),
                               "off = \"\"");
    EXPECT_EQ(errorOf(emptyInAnotherPart), TextsError::DataInvalid) << "every part is read through";
    EXPECT_EQ(errorOf(""), TextsError::DataInvalid) << "no part at all";
}

TEST(TextsTest, ALanguageIsATagAFileCanBeNamedAfter) {
    for (const std::string_view tag : {"en", "pt-BR", "zh-Hans-CN", "es-419"}) {
        EXPECT_TRUE(cs::isLanguageTag(tag)) << tag;
    }
    for (const std::string_view text :
         {"", "-", "en-", "-en", "pt--BR", "../en", "en/x", "pt_BR", "en.toml", "e n", "pt-BR\\x"}) {
        EXPECT_FALSE(cs::isLanguageTag(text)) << text;
    }
}

TEST(TextsTest, OnlyALanguageTheDataHasOpens) {
    const nx::test::TemporaryDirectory data;
    data.write("cstrike/locale/en.toml", fileWith("QUIT"));
    data.write("cstrike/locale/broken.toml", "[mainMenu]\nquit = \"QUIT\"\n");
    data.write("cstrike/locale/notes.txt", "not a language");
    data.write("cstrike/locale/old/de.toml", fileWith("ENDE"));
    const auto languages = cs::languagesOf(data.path());
    ASSERT_TRUE(languages.has_value());
    EXPECT_EQ(*languages, (std::vector<std::string>{"broken", "en"})) << "the files of the folder itself";

    const auto english = cs::openTexts(data.path(), "en");
    ASSERT_TRUE(english.has_value()) << (english ? "" : nx::describe(english.error()));
    EXPECT_EQ(english->mainMenu.quit, "QUIT");
    const auto codeOf = [&](std::string_view language) -> std::optional<TextsError> {
        const auto texts = cs::openTexts(data.path(), language);
        return texts ? std::nullopt : std::optional{texts.error().code};
    };
    EXPECT_EQ(codeOf("de"), TextsError::UnknownLanguage);
    EXPECT_EQ(codeOf("EN"), TextsError::UnknownLanguage) << "by its tag as the file spells it";
    EXPECT_EQ(codeOf("../locale/en"), TextsError::UnknownLanguage);
    EXPECT_EQ(codeOf("broken"), TextsError::DataInvalid);

    const nx::test::TemporaryDirectory empty;
    const auto none = cs::openTexts(empty.path(), "en");
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().code, TextsError::UnknownLanguage) << "no folder of languages";
}

TEST(PlayerTextsTest, AnotherLanguageIsReadInPlaceOfTheOneBeforeAndOneTheDataLacksChangesNothing) {
    const nx::test::TemporaryDirectory data;
    data.write("cstrike/locale/en.toml", fileWith("QUIT"));
    data.write("cstrike/locale/pt-BR.toml", fileWith("SAIR"));
    auto texts = cs::PlayerTexts::open(data.path(), "en");
    ASSERT_TRUE(texts.has_value()) << (texts ? "" : nx::describe(texts.error()));
    EXPECT_EQ(std::vector<std::string>(texts->languages().begin(), texts->languages().end()),
              (std::vector<std::string>{"en", "pt-BR"}));
    const cs::Texts* const place = &texts->written();
    EXPECT_EQ(place->mainMenu.quit, "QUIT");

    const auto askedFor = [&](std::string_view language) -> std::optional<cs::LanguageAsked> {
        const auto asked = texts->ask(language);
        return asked ? std::optional{*asked} : std::nullopt;
    };
    EXPECT_EQ(askedFor("en"), cs::LanguageAsked::Same) << "the language it has";
    EXPECT_EQ(askedFor("pt-BR"), cs::LanguageAsked::Read);
    EXPECT_EQ(texts->written().mainMenu.quit, "SAIR");
    EXPECT_EQ(&texts->written(), place) << "in place of the texts before";

    const auto unknown = texts->ask("de");
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error().code, TextsError::UnknownLanguage);
    EXPECT_EQ(texts->written().mainMenu.quit, "SAIR") << "the texts it had stay";
    EXPECT_EQ(askedFor("de"), cs::LanguageAsked::Same) << "asked for again, it is not read again";
    EXPECT_EQ(askedFor("en"), cs::LanguageAsked::Read) << "another is";
    EXPECT_EQ(texts->written().mainMenu.quit, "QUIT");

    const auto none = cs::PlayerTexts::open(data.path(), "de");
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().code, TextsError::UnknownLanguage) << "as it opens, a language the data lacks";
}

} // namespace
