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

/// A language's file that writes every text the main menu asks for, `quit` as `quit` says.
[[nodiscard]] auto fileWith(std::string_view quit) -> std::string {
    return std::format("[mainMenu]\n"
                       "resumeGame = \"a\"\ndisconnect = \"b\"\nnewGame = \"c\"\nquit = \"{}\"\nmap = \"e\"\n"
                       "noMaps = \"f\"\nstart = \"g\"\nback = \"h\"\nloading = \"i\"\nkeyUpDown = \"j\"\n"
                       "keyEnter = \"k\"\nkeyEscape = \"l\"\nselect = \"m\"\nchoose = \"n\"\nresume = \"o\"\n"
                       "play = \"p\"\ngoBack = \"q\"\n",
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

} // namespace
