// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

namespace {

using nx::console::CommandLine;
using nx::console::parseScript;
using nx::console::splitFirstCommand;
using nx::console::tokenize;

using Words = std::vector<std::string>;

[[nodiscard]] auto wordsOf(const std::vector<CommandLine>& commands) -> std::vector<Words> {
    std::vector<Words> words;
    for (const CommandLine& command : commands) {
        words.push_back(command.words);
    }
    return words;
}

TEST(NamesTest, CaseInsensitive) {
    constexpr nx::console::CaseInsensitiveLess less;
    EXPECT_TRUE(less("Alpha", "beta"));
    EXPECT_FALSE(less("BETA", "beta"));
    EXPECT_FALSE(less("beta", "BETA"));
}

TEST(NamesTest, Validity) {
    using nx::console::isValidName;
    EXPECT_TRUE(isValidName("mp_roundtime"));
    EXPECT_TRUE(isValidName("+jump"));
    EXPECT_TRUE(isValidName("rz_infection_delay"));
    EXPECT_FALSE(isValidName(""));
    EXPECT_FALSE(isValidName("two words"));
    EXPECT_FALSE(isValidName("tab\tbed"));
    EXPECT_FALSE(isValidName("semi;colon"));
    EXPECT_FALSE(isValidName("\"quoted\""));
    EXPECT_FALSE(isValidName("//comment"));
}

TEST(NamesTest, EditDistance) {
    using nx::console::editDistance;
    EXPECT_EQ(editDistance("sv_gravity", "sv_gravity"), 0U);
    EXPECT_EQ(editDistance("SV_GRAVITY", "sv_gravity"), 0U);
    EXPECT_EQ(editDistance("sv_gravty", "sv_gravity"), 1U);
    EXPECT_EQ(editDistance("sv_garvity", "sv_gravity"), 2U);
    EXPECT_EQ(editDistance("", "abc"), 3U);
    EXPECT_EQ(editDistance("kitten", "sitting"), 3U);
}

TEST(ParserTest, SplitsAtLineBreaksAndSemicolons) {
    EXPECT_EQ(wordsOf(parseScript("echo one; echo two\necho three")),
              (std::vector<Words>{{"echo", "one"}, {"echo", "two"}, {"echo", "three"}}));
}

TEST(ParserTest, SkipsEmptyCommands) {
    EXPECT_EQ(wordsOf(parseScript(";;\n\n  ; echo one;\n\t\n")), (std::vector<Words>{{"echo", "one"}}));
    EXPECT_TRUE(parseScript("").empty());
}

TEST(ParserTest, QuotesGroupWordsAndProtectSemicolons) {
    EXPECT_EQ(wordsOf(parseScript(R"(alias jump "+jump; wait; -jump"; echo done)")),
              (std::vector<Words>{{"alias", "jump", "+jump; wait; -jump"}, {"echo", "done"}}));
    EXPECT_EQ(tokenize(R"(hostname "My Server")").words, (Words{"hostname", "My Server"}));
    EXPECT_EQ(tokenize(R"(sv_password "")").words, (Words{"sv_password", ""}));
}

TEST(ParserTest, CommentsRunToTheEndOfTheLine) {
    EXPECT_EQ(wordsOf(parseScript("// a whole line\nsv_gravity 800 // trailing\necho after")),
              (std::vector<Words>{{"sv_gravity", "800"}, {"echo", "after"}}));
    // A `;` inside a comment does not start another command.
    EXPECT_EQ(wordsOf(parseScript("echo one // two; echo three\necho four")),
              (std::vector<Words>{{"echo", "one"}, {"echo", "four"}}));
}

TEST(ParserTest, CommentsStartOnlyAtTheStartOfAWord) {
    EXPECT_EQ(tokenize(R"(sv_downloadurl "http://example.com/cs")").words,
              (Words{"sv_downloadurl", "http://example.com/cs"}));
    EXPECT_EQ(tokenize("sv_downloadurl http://example.com/cs").words,
              (Words{"sv_downloadurl", "http://example.com/cs"}));
    // Regression: the splitter took the `//` inside the word for a comment and kept the `;`.
    EXPECT_EQ(wordsOf(parseScript("sv_downloadurl http://example.com/cs; echo done")),
              (std::vector<Words>{{"sv_downloadurl", "http://example.com/cs"}, {"echo", "done"}}));
}

TEST(ParserTest, ASemicolonInsideAnUnquotedWordEndsTheCommand) {
    // A `"` inside a word is an ordinary character: it opens no quoted word, so the `;` splits.
    EXPECT_EQ(wordsOf(parseScript(R"(echo ab"cd;echo ef)")),
              (std::vector<Words>{{"echo", R"(ab"cd)"}, {"echo", "ef"}}));
}

TEST(ParserTest, AnUnterminatedQuoteRunsToTheEndOfTheCommand) {
    EXPECT_EQ(tokenize(R"(say "hello there)").words, (Words{"say", "hello there"}));
    EXPECT_EQ(wordsOf(parseScript("say \"one; two\necho next")),
              (std::vector<Words>{{"say", "one; two"}, {"echo", "next"}}));
}

TEST(ParserTest, QuotesInsideAWordAreKept) {
    EXPECT_EQ(tokenize(R"(echo a"b c"d)").words, (Words{"echo", R"(a"b)", R"(c"d)"}));
    EXPECT_EQ(tokenize(R"(echo "a"b)").words, (Words{"echo", "a", "b"}));
}

TEST(ParserTest, CarriageReturnsAndTabsAreWhitespace) {
    EXPECT_EQ(wordsOf(parseScript("sv_gravity\t800\r\nmp_roundtime 2.5\r\n")),
              (std::vector<Words>{{"sv_gravity", "800"}, {"mp_roundtime", "2.5"}}));
}

TEST(ParserTest, ArgumentTextIsWhatFollowsTheName) {
    const CommandLine line = tokenize(R"(  say   "hello"  world  )");
    EXPECT_EQ(line.name(), "say");
    EXPECT_EQ(line.argumentText, R"("hello"  world)");
    EXPECT_EQ(line.arguments().size(), 2U);
    EXPECT_TRUE(tokenize("status").argumentText.empty());
    EXPECT_TRUE(tokenize("").name().empty());
}

TEST(ParserTest, SplitFirstCommandReturnsTheRemainder) {
    const auto [command, remainder] = splitFirstCommand("echo \"a;b\"; echo c\necho d");
    EXPECT_EQ(command, "echo \"a;b\"");
    EXPECT_EQ(remainder, " echo c\necho d");
    EXPECT_TRUE(splitFirstCommand("echo end").remainder.empty());
}

TEST(ParserTest, ARealConfig) {
    constexpr std::string_view kConfig = "// server.cfg\r\n"
                                         "hostname \"Modern-Client.v2 | de_dust2 only\"\r\n"
                                         "mp_roundtime 2.5\r\n"
                                         "mp_freezetime 6 // seconds\r\n"
                                         "sv_password \"\"\r\n"
                                         "alias +jumpthrow \"+jump; -attack\"\r\n"
                                         "alias -jumpthrow \"-jump\"\r\n"
                                         "exec banned.cfg; exec listip.cfg\r\n";
    EXPECT_EQ(wordsOf(parseScript(kConfig)), (std::vector<Words>{
                                                 {"hostname", "Modern-Client.v2 | de_dust2 only"},
                                                 {"mp_roundtime", "2.5"},
                                                 {"mp_freezetime", "6"},
                                                 {"sv_password", ""},
                                                 {"alias", "+jumpthrow", "+jump; -attack"},
                                                 {"alias", "-jumpthrow", "-jump"},
                                                 {"exec", "banned.cfg"},
                                                 {"exec", "listip.cfg"},
                                             }));
}

} // namespace
