// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A buffer handed from its producer to its consumer and back: what is made every tick or every frame
// is read without being copied out, the storage kept from take to take.

#include <gtest/gtest.h>

import std;
import nx.core;

namespace {

struct Made {
    int id = 0;
};

/// The address the storage of `buffer` starts at, as an integer: the same until it grows.
[[nodiscard]] auto storageOf(const std::vector<Made>& buffer) -> std::uintptr_t {
    return std::bit_cast<std::uintptr_t>(buffer.data());
}

} // namespace

TEST(BufferHandover, TheTwoBuffersTakeTurns) {
    // What each take hands over lives in one of the two buffers of the handover, and each comes back to the
    // producer once the consumer moves on: a repeated production allocates nothing once warmed up.
    nx::BufferHandover<Made> handover;
    std::vector<std::uintptr_t> storages;
    for (int round = 0; round < 8; ++round) {
        handover.bufferForProducer().push_back(Made{.id = round});
        const std::span<const Made> taken = handover.handToConsumer();
        ASSERT_EQ(taken.size(), 1U);
        const std::uintptr_t storage = std::bit_cast<std::uintptr_t>(taken.data());
        if (std::ranges::find(storages, storage) == storages.end()) {
            storages.push_back(storage);
        }
    }

    EXPECT_LE(storages.size(), 2U) << "a buffer of the handover, not one made anew each take";
}

TEST(BufferHandover, HandsWhatTheProducerAppends) {
    nx::BufferHandover<Made> handover;
    handover.bufferForProducer().push_back(Made{.id = 1});
    handover.bufferForProducer().push_back(Made{.id = 2});

    const std::span<const Made> taken = handover.handToConsumer();

    ASSERT_EQ(taken.size(), 2U);
    EXPECT_EQ(taken[0].id, 1);
    EXPECT_EQ(taken[1].id, 2);
}

TEST(BufferHandover, ATakeWithNothingAppendedIsEmpty) {
    nx::BufferHandover<Made> handover;
    handover.bufferForProducer().push_back(Made{.id = 1});
    ASSERT_EQ(handover.handToConsumer().size(), 1U);

    EXPECT_TRUE(handover.handToConsumer().empty());
}

TEST(BufferHandover, EachTakeHoldsAppendsSinceTheLast) {
    nx::BufferHandover<Made> handover;
    handover.bufferForProducer().push_back(Made{.id = 1});
    ASSERT_EQ(handover.handToConsumer().size(), 1U);

    handover.bufferForProducer().push_back(Made{.id = 2});

    const std::span<const Made> taken = handover.handToConsumer();
    ASSERT_EQ(taken.size(), 1U) << "not the one the take before took";
    EXPECT_EQ(taken[0].id, 2);
}

TEST(BufferHandover, TheRoomComesBackAtTheTakeAfterTheOneThatReadIt) {
    nx::BufferHandover<Made> handover;
    handover.bufferForProducer().reserve(1);
    const std::uintptr_t first = storageOf(handover.bufferForProducer());
    handover.bufferForProducer().push_back(Made{});
    ASSERT_EQ(handover.handToConsumer().size(), 1U);

    // The consumer holds the buffer it was handed until it takes again, so the producer got the other one,
    // which still had to grow.
    EXPECT_EQ(handover.bufferForProducer().capacity(), 0U);
    handover.bufferForProducer().push_back(Made{});
    ASSERT_EQ(handover.handToConsumer().size(), 1U);

    EXPECT_EQ(storageOf(handover.bufferForProducer()), first) << "the buffer the consumer just returned";
}

TEST(BufferHandover, AConsumerReadsWhatItWasHandedUntilTheNextTake) {
    nx::BufferHandover<Made> handover;
    handover.bufferForProducer().push_back(Made{.id = 7});
    const std::span<const Made> first = handover.handToConsumer();
    ASSERT_EQ(first.size(), 1U);

    // The producer writes the other buffer, so what the consumer holds is untouched.
    handover.bufferForProducer().push_back(Made{.id = 8});

    EXPECT_EQ(first[0].id, 7);
    EXPECT_NE(first.data(), handover.handToConsumer().data()) << "the buffer the consumer held went back";
}

TEST(BufferHandover, ACopyKeepsWhatWasNotHandedOver) {
    nx::BufferHandover<Made> handover;
    handover.bufferForProducer().push_back(Made{.id = 1});
    ASSERT_EQ(handover.handToConsumer().size(), 1U);
    handover.bufferForProducer().push_back(Made{.id = 2});

    nx::BufferHandover<Made> copy = handover; // as a snapshot of what a tick made is

    const std::span<const Made> original = handover.handToConsumer();
    ASSERT_EQ(original.size(), 1U);
    EXPECT_EQ(original[0].id, 2) << "the original hands over what it made";
    const std::span<const Made> copied = copy.handToConsumer();
    ASSERT_EQ(copied.size(), 1U) << "the copy what it was made of";
    EXPECT_EQ(copied[0].id, 2);
}
