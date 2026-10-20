// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

namespace {

enum class Button : std::uint8_t {
    Attack,
    Jump,
    Duck,
    Use,
    Attack2,
    Reload,
    Count,
};

/// More enumerators than a single 64-bit word holds.
enum class WideSet : std::uint16_t {
    First = 0,
    Middle = 64,
    Last = 99,
    Count = 100,
};

} // namespace

TEST(Flags, StartsEmpty) {
    const nx::Flags<Button> buttons;

    EXPECT_TRUE(buttons.none());
    EXPECT_EQ(buttons.count(), 0U);
}

TEST(Flags, SetsResetsAndTogglesSingleFlags) {
    nx::Flags<Button> buttons;

    buttons.set(Button::Jump).set(Button::Duck);
    EXPECT_TRUE(buttons.test(Button::Jump));
    EXPECT_TRUE(buttons.test(Button::Duck));
    EXPECT_FALSE(buttons.test(Button::Attack));

    buttons.reset(Button::Jump).toggle(Button::Attack);
    EXPECT_FALSE(buttons.test(Button::Jump));
    EXPECT_TRUE(buttons.test(Button::Attack));
    EXPECT_EQ(buttons.count(), 2U);
}

TEST(Flags, BuildsFromAList) {
    const nx::Flags<Button> duckJump{Button::Duck, Button::Jump};

    EXPECT_EQ(duckJump.count(), 2U);
    EXPECT_TRUE(duckJump.containsAll({Button::Jump}));
    EXPECT_TRUE(duckJump.containsAny({Button::Use, Button::Duck}));
    EXPECT_FALSE(duckJump.containsAny({Button::Use, Button::Reload}));
}

TEST(Flags, CombinesWithSetOperators) {
    const nx::Flags<Button> attacks{Button::Attack, Button::Attack2};
    const nx::Flags<Button> primary{Button::Attack, Button::Jump};

    EXPECT_EQ(attacks | primary, (nx::Flags<Button>{Button::Attack, Button::Attack2, Button::Jump}));
    EXPECT_EQ(attacks & primary, (nx::Flags<Button>{Button::Attack}));
    EXPECT_EQ(attacks ^ primary, (nx::Flags<Button>{Button::Attack2, Button::Jump}));
}

TEST(Flags, ComplementStaysWithinTheValidEnumerators) {
    EXPECT_EQ(nx::Flags<Button>::all().count(), nx::enumCount<Button>());
    EXPECT_EQ((~nx::Flags<Button>{Button::Use}).count(), nx::enumCount<Button>() - 1);
    EXPECT_EQ(nx::Flags<WideSet>::all().count(), nx::enumCount<WideSet>());
}

TEST(Flags, SpansSeveralWords) {
    nx::Flags<WideSet> flags{WideSet::First, WideSet::Middle, WideSet::Last};

    EXPECT_TRUE(flags.test(WideSet::Middle));
    EXPECT_TRUE(flags.test(WideSet::Last));
    EXPECT_EQ(flags.count(), 3U);
    flags.reset(WideSet::Middle);
    EXPECT_FALSE(flags.test(WideSet::Middle));
}

TEST(Flags, OutOfRangeFlagsAreReportedAndIgnored) {
    const nx::test::ContractRecorder recorder;
    nx::Flags<Button> buttons;

    buttons.set(Button::Count);

    EXPECT_EQ(recorder.count(), 1);
    EXPECT_TRUE(buttons.none());
}
