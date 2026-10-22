// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// KeyValues: VGUI2's schemes, layouts and localized strings, read as VGUI2 reads them, from texts
// shaped like the installation's own.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::KeyValuesEntry;
using nx::assets::KeyValuesError;
using nx::assets::KeyValuesEscapes;
using nx::assets::parseKeyValues;

[[nodiscard]] auto errorOf(std::string_view text) -> std::optional<KeyValuesError> {
    const auto entries = parseKeyValues(text);
    return entries ? std::nullopt : std::optional{entries.error().code};
}

[[nodiscard]] auto bytesOf(std::initializer_list<int> values) -> std::vector<std::byte> {
    return values | std::views::transform([](int value) { return static_cast<std::byte>(value); }) |
           std::ranges::to<std::vector<std::byte>>();
}

TEST(KeyValuesTest, ReadsASchemeAsVgui2Does) {
    const auto entries = parseKeyValues("//\n"
                                        "// TRACKER SCHEME RESOURCE FILE\n"
                                        "//\n"
                                        "Scheme\n"
                                        "{\n"
                                        "\tColors\n"
                                        "\t{\n"
                                        "\t\t\"BaseText\"\t\t\"216 222 211 255\"\t// used in text windows\r\n"
                                        "\t\t\"WindowBG\"\t\t\"62 70 55 255\"\n"
                                        "\t}\n"
                                        "\tBorders\n"
                                        "\t{\n"
                                        "\t\tBaseBorder\t\t\"InsetBorder\"\n"
                                        "\t\tInsetBorder { \"inset\" \"0 0 1 1\" }\n"
                                        "\t}\n"
                                        "}\n");
    ASSERT_TRUE(entries.has_value()) << nx::describe(entries.error());
    ASSERT_EQ(entries->size(), 1U);
    const KeyValuesEntry& scheme = entries->front();
    EXPECT_EQ(scheme.key, "Scheme");
    EXPECT_TRUE(scheme.isBlock);
    const KeyValuesEntry* colors = scheme.find("colors");
    ASSERT_NE(colors, nullptr) << "keys are found in any case";
    EXPECT_EQ(colors->findWord("BaseText"), "216 222 211 255");
    EXPECT_EQ(colors->findWord("windowbg"), "62 70 55 255");
    EXPECT_EQ(colors->findWord("Missing"), std::nullopt);
    const KeyValuesEntry* borders = scheme.find("Borders");
    ASSERT_NE(borders, nullptr);
    EXPECT_EQ(borders->findWord("BaseBorder"), "InsetBorder") << "a bare key with a quoted value";
    EXPECT_EQ(borders->findWord("InsetBorder"), std::nullopt) << "a block is no word";
    ASSERT_NE(borders->find("InsetBorder"), nullptr);
    EXPECT_EQ(borders->find("InsetBorder")->findWord("inset"), "0 0 1 1");
}

TEST(KeyValuesTest, KeepsALayoutsEntriesInTheirOrder) {
    const auto entries = parseKeyValues(
        "\"Resource/OptionsSubMultiplayer.res\"\n"
        "{\n"
        "\t\"Cancel\" { \"ControlName\" \"Button\" \"xpos\" \"378\" \"labelText\" \"#GameUI_Cancel\" }\n"
        "\t\"ok\" { \"ControlName\" \"Button\" \"xpos\" \"308\" }\n"
        "\t\"ok\" { \"xpos\" \"1\" }\n"
        "}\n");
    ASSERT_TRUE(entries.has_value()) << nx::describe(entries.error());
    const KeyValuesEntry& layout = entries->front();
    EXPECT_EQ(layout.key, "Resource/OptionsSubMultiplayer.res");
    ASSERT_EQ(layout.entries.size(), 3U) << "a repeated key is kept";
    EXPECT_EQ(layout.entries[0].key, "Cancel");
    EXPECT_EQ(layout.entries[0].findWord("labelText"), "#GameUI_Cancel");
    EXPECT_EQ(layout.find("OK")->findWord("xpos"), "308") << "the first of a repeated key";
}

TEST(KeyValuesTest, ReadsWordsBareAndQuoted) {
    const auto entries = parseKeyValues("a b\"c\"d\n"
                                        "\"two\nlines\" \"has // no comment\"\n"
                                        "\"empty\" \"\"\n"
                                        "url http://example\n");
    ASSERT_TRUE(entries.has_value()) << nx::describe(entries.error());
    ASSERT_EQ(entries->size(), 5U);
    EXPECT_EQ((*entries)[0].key, "a");
    EXPECT_EQ((*entries)[0].word, "b") << "a bare word ends at a quote";
    EXPECT_EQ((*entries)[1].key, "c");
    EXPECT_EQ((*entries)[1].word, "d");
    EXPECT_EQ((*entries)[2].key, "two\nlines") << "a quoted word runs across lines";
    EXPECT_EQ((*entries)[2].word, "has // no comment");
    EXPECT_EQ((*entries)[3].word, "");
    EXPECT_EQ((*entries)[4].word, "http://example") << "// starts a comment only before a word";
    ASSERT_EQ(parseKeyValues("d{e f}").value().front().entries.size(), 1U);
}

TEST(KeyValuesTest, KeepsTheConditionsThatFollowAnEntry) {
    const auto entries = parseKeyValues("\"font\" \"Tahoma\" [$WIN32]\n"
                                        "\"font\" [$OSX] \"Verdana\"\n"
                                        "block { } [!$X360]\n");
    ASSERT_TRUE(entries.has_value()) << nx::describe(entries.error());
    ASSERT_EQ(entries->size(), 3U);
    EXPECT_EQ((*entries)[0].condition, "[$WIN32]");
    EXPECT_EQ((*entries)[1].word, "Verdana");
    EXPECT_EQ((*entries)[1].condition, "[$OSX]");
    EXPECT_EQ((*entries)[2].condition, "[!$X360]");
}

TEST(KeyValuesTest, LocalizedStringsSpellLineBreaksWithABackslash) {
    const auto entries =
        parseKeyValues("\"lang\" { \"Tokens\" { \"Label\" \"one \\ntwo \\\"quoted\\\" \\\\ \\q\" } }",
                       KeyValuesEscapes::Backslash);
    ASSERT_TRUE(entries.has_value()) << nx::describe(entries.error());
    EXPECT_EQ(entries->front().find("Tokens")->findWord("Label"), "one \ntwo \"quoted\" \\ \\q");
    const auto asTheyAre = parseKeyValues(R"("path" "a\nb")");
    ASSERT_TRUE(asTheyAre.has_value());
    EXPECT_EQ(asTheyAre->front().word, R"(a\nb)") << "layouts keep their backslashes";
}

TEST(KeyValuesTest, RefusesWhatVgui2CannotRead) {
    EXPECT_EQ(errorOf("\"open"), KeyValuesError::UnclosedQuote);
    EXPECT_EQ(errorOf("a { b c"), KeyValuesError::UnclosedBlock);
    EXPECT_EQ(errorOf("a b }"), KeyValuesError::StrayBrace);
    EXPECT_EQ(errorOf("{ a b }"), KeyValuesError::StrayBrace);
    EXPECT_EQ(errorOf("a"), KeyValuesError::MissingValue);
    EXPECT_EQ(errorOf("a { b }"), KeyValuesError::MissingValue);
    std::string deep;
    for (std::size_t level = 0; level <= nx::assets::kKeyValuesMaxDepth; ++level) {
        deep += "a { ";
    }
    EXPECT_EQ(errorOf(deep), KeyValuesError::TooDeep);
    const auto unclosed = parseKeyValues("a {\n b c\n");
    ASSERT_FALSE(unclosed.has_value());
    EXPECT_EQ(unclosed.error().detail, "the block of line 1");
    EXPECT_TRUE(parseKeyValues("").has_value()) << "no entries";
}

TEST(KeyValuesTest, LocalizedFilesAreDecodedFromUtf16) {
    // "é" and a G clef (U+1D11E, a surrogate pair), after the UTF-16 byte order mark.
    const auto text =
        nx::assets::keyValuesText(bytesOf({0xFF, 0xFE, 'a', 0, 0xE9, 0, 0x34, 0xD8, 0x1E, 0xDD}));
    ASSERT_TRUE(text.has_value()) << nx::describe(text.error());
    EXPECT_EQ(*text, "a\xC3\xA9\xF0\x9D\x84\x9E");
    EXPECT_EQ(nx::assets::keyValuesText(bytesOf({0xEF, 0xBB, 0xBF, 'x'})).value(), "x")
        << "a UTF-8 mark dropped";
    EXPECT_EQ(nx::assets::keyValuesText(bytesOf({'x', 'y'})).value(), "xy");
    for (const std::vector<std::byte>& broken :
         {bytesOf({0xFF, 0xFE, 'a'}), bytesOf({0xFF, 0xFE, 0x34, 0xD8}),
          bytesOf({0xFF, 0xFE, 0x1E, 0xDD, 'a', 0}), bytesOf({0xFF, 0xFE, 0x34, 0xD8, 'a', 0})}) {
        const auto refused = nx::assets::keyValuesText(broken);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, KeyValuesError::BadEncoding);
    }
}

} // namespace
