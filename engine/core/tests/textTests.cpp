// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

namespace {

TEST(Text, LowersOnlyAsciiCapitals) {
    static_assert(nx::toLowerAscii('A') == 'a');
    static_assert(nx::toLowerAscii('Z') == 'z');
    static_assert(nx::toLowerAscii('a') == 'a');
    static_assert(nx::toLowerAscii('_') == '_');
    static_assert(nx::toLowerAscii('@') == '@'); // the byte before 'A'
    static_assert(nx::toLowerAscii('[') == '['); // the byte after 'Z'
    EXPECT_EQ(nx::toLowerAscii('\xC9'), '\xC9') << "bytes beyond ASCII stay as they are";
}

TEST(Text, FindsAsciiControlBytes) {
    static_assert(nx::isAsciiControl('\0'));
    static_assert(nx::isAsciiControl('\n'));
    static_assert(nx::isAsciiControl('\x1F'));
    static_assert(nx::isAsciiControl('\x7F'));
    static_assert(!nx::isAsciiControl(' '));
    static_assert(!nx::isAsciiControl('~'));
    EXPECT_FALSE(nx::isAsciiControl('\xC3')) << "bytes beyond ASCII are not control bytes";
}

TEST(Text, ComparesIgnoringAsciiCase) {
    static_assert(nx::equalsIgnoringCase("SV_Gravity", "sv_gravity"));
    static_assert(!nx::equalsIgnoringCase("sv_gravity", "sv_gravit"));
    EXPECT_FALSE(nx::equalsIgnoringCase("\xC9", "\xE9")) << "case beyond ASCII is not folded";
}

TEST(Text, WordsBounds) {
    EXPECT_EQ(nx::boundsText<int>(0, 100), "in [0, 100]");
    EXPECT_EQ(nx::boundsText<double>(0.5, std::nullopt), "of at least 0.5");
    EXPECT_EQ(nx::boundsText<float>(std::nullopt, 2.5F), "of at most 2.5");
    EXPECT_EQ(nx::boundsText<int>(std::nullopt, std::nullopt), "");
}

TEST(Text, EncodesUtf8) {
    std::string text;
    for (const char32_t character : {U'A', U'\u00E9', U'\u20AC', U'\U0001F600', U'\U0010FFFF'}) {
        nx::appendUtf8(text, character);
    }
    EXPECT_EQ(text, "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF");
}

TEST(Text, DecodesUtf8) {
    EXPECT_EQ(nx::decodeUtf8("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"), U"A\u00E9\u20AC\U0001F600");
    EXPECT_EQ(nx::decodeUtf8(""), U"");
    EXPECT_EQ(nx::decodeUtf8(std::string_view{"a\0b", 3}), std::u32string(U"a\0b", 3))
        << "NUL is a character";
}

TEST(Text, DecodesWhatIsNotUtf8AsReplacements) {
    constexpr char32_t kBad = nx::kReplacementCharacter;
    EXPECT_EQ(nx::decodeUtf8("a\x80z"), (std::u32string{U'a', kBad, U'z'})) << "a stray continuation";
    EXPECT_EQ(nx::decodeUtf8("\xC0\xAF"), (std::u32string{kBad, kBad})) << "an overlong form";
    EXPECT_EQ(nx::decodeUtf8("\xE0\x80\xAF"), (std::u32string{kBad, kBad, kBad})) << "an overlong form";
    EXPECT_EQ(nx::decodeUtf8("\xED\xA0\x80"), (std::u32string{kBad, kBad, kBad})) << "a surrogate";
    EXPECT_EQ(nx::decodeUtf8("\xF4\x90\x80\x80"), (std::u32string{kBad, kBad, kBad, kBad}))
        << "past U+10FFFF";
    EXPECT_EQ(nx::decodeUtf8("\xE2\x82z"), (std::u32string{kBad, U'z'}))
        << "a sequence cut short: one replacement, and what cut it read anew";
    EXPECT_EQ(nx::decodeUtf8("\xF0\x9F\x98"), (std::u32string{kBad})) << "cut short by the end";
    EXPECT_EQ(nx::decodeUtf8("\xFF"), (std::u32string{kBad}));
}

} // namespace
