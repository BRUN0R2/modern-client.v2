// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.render;

namespace {

using nx::Seconds;
using nx::render::LightStyles;

TEST(LightStyleTest, EachLetterAfterAAddsTwentyTwo) {
    LightStyles styles;
    styles.set(0, "m");
    styles.set(1, "a");
    styles.set(2, "z");
    const nx::render::LightStyleValues values = styles.valuesAt(Seconds{0.0F});
    EXPECT_EQ(values[0], nx::render::kNormalLightStyleValue);
    EXPECT_EQ(values[0], 264);
    EXPECT_EQ(values[1], 0);
    EXPECT_EQ(values[2], 550);
}

TEST(LightStyleTest, AStyleWithoutAPatternIsAlmostNormal) {
    const LightStyles styles;
    EXPECT_EQ(styles.valuesAt(Seconds{3.0F})[40], 256) << "the original's 256, below 'm'";
}

TEST(LightStyleTest, PatternsStepTenLettersASecondAndRepeat) {
    LightStyles styles;
    styles.set(4, "mamamamamama"); // the fast strobe
    styles.set(9, "abc");
    EXPECT_EQ(styles.valuesAt(Seconds{0.05F})[4], 264) << "letter 0";
    EXPECT_EQ(styles.valuesAt(Seconds{0.15F})[4], 0) << "letter 1";
    EXPECT_EQ(styles.valuesAt(Seconds{0.25F})[9], 44) << "letter 2";
    EXPECT_EQ(styles.valuesAt(Seconds{0.35F})[9], 0) << "letter 3 is letter 0 again";
}

TEST(LightStyleTest, CharactersOutsideTheLettersAreHeldToThem) {
    LightStyles styles;
    styles.set(0, "M~");
    EXPECT_EQ(styles.valuesAt(Seconds{0.0F})[0], 0) << "'M' is before 'a'";
    EXPECT_EQ(styles.valuesAt(Seconds{0.1F})[0], 550) << "'~' is after 'z'";
    styles.set(0, "");
    EXPECT_EQ(styles.valuesAt(Seconds{0.0F})[0], 256) << "unset again";
}

} // namespace
