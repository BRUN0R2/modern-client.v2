// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The ring of slots a renderer writes its frames to: each prepare the next slot, the draws after it the
// one it wrote, and a number for each prepare that a slot's turn coming round again does not repeat.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.render;

#include "contractRecorder.hpp"

namespace {

using nx::render::FrameSlots;
using nx::render::PrepareNumber;

/// A slot that tells which it is.
struct NamedSlot {
    int name = 0;
};

/// A ring of `count` slots named 0, 1, 2... in the order the prepares take them.
[[nodiscard]] auto namedRing(std::size_t count) -> FrameSlots<NamedSlot> {
    FrameSlots<NamedSlot> slots{count};
    int name = 0;
    for (NamedSlot& slot : slots) {
        slot.name = name++;
    }
    return slots;
}

TEST(FrameSlotsTest, EachPrepareWritesTheNextSlotRoundTheRing) {
    FrameSlots<NamedSlot> slots = namedRing(3);
    std::array<int, 7> written{};
    for (int& name : written) {
        name = slots.advance().name;
    }
    EXPECT_EQ(written, (std::array{0, 1, 2, 0, 1, 2, 0}));
}

TEST(FrameSlotsTest, TheDrawsReadTheSlotTheLastPrepareWrote) {
    FrameSlots<NamedSlot> slots = namedRing(3);
    for (int prepare = 0; prepare < 5; ++prepare) {
        const NamedSlot& written = slots.advance();
        EXPECT_EQ(&slots.prepared(), &written);
        EXPECT_EQ(&std::as_const(slots).prepared(), &written);
    }
}

TEST(FrameSlotsTest, ARingOfOneSlotWritesItEachPrepare) {
    FrameSlots<NamedSlot> slots{1};
    const NamedSlot& only = slots.advance();
    EXPECT_EQ(&slots.advance(), &only);
    EXPECT_EQ(&slots.prepared(), &only);
}

TEST(FrameSlotsTest, EachPrepareHasANumberOfItsOwnThoughItsSlotComesRoundAgain) {
    FrameSlots<NamedSlot> slots = namedRing(2);
    EXPECT_EQ(slots.lastPrepare(), PrepareNumber{}) << "none before the first";
    EXPECT_EQ(slots.advance().name, 0);
    EXPECT_EQ(slots.lastPrepare(), PrepareNumber{1});
    EXPECT_EQ(slots.advance().name, 1);
    EXPECT_EQ(slots.advance().name, 0);
    EXPECT_EQ(slots.lastPrepare(), PrepareNumber{3}) << "the first slot again, in another prepare";
}

TEST(FrameSlotsTest, ReadingThePreparedSlotBeforeAPrepareBreaksItsContract) {
    const nx::test::ContractRecorder recorder;
    const FrameSlots<NamedSlot> slots = namedRing(3);
    EXPECT_EQ(slots.prepared().name, 0) << "the slot the first prepare writes";
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

TEST(FrameSlotsTest, ARingOfNoSlotBreaksItsContract) {
    const nx::test::ContractRecorder recorder;
    FrameSlots<NamedSlot> slots{0};
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_EQ(std::ranges::distance(slots), 1) << "one slot, to write each prepare";
    const NamedSlot& only = slots.advance();
    EXPECT_EQ(&slots.advance(), &only);
}

} // namespace
