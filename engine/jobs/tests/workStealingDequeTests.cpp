// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;

#include "contractRecorder.hpp"

namespace {

using Deque = nx::jobs::WorkStealingDeque<std::uint32_t>;

TEST(WorkStealingDequeTest, OwnerPopsTheNewestElementFirst) {
    Deque deque;
    deque.push(1);
    deque.push(2);
    deque.push(3);

    EXPECT_EQ(deque.pop(), 3U);
    EXPECT_EQ(deque.pop(), 2U);
    EXPECT_EQ(deque.pop(), 1U);
    EXPECT_EQ(deque.pop(), std::nullopt);
}

TEST(WorkStealingDequeTest, ThievesTakeTheOldestElementFirst) {
    Deque deque;
    deque.push(1);
    deque.push(2);
    deque.push(3);

    EXPECT_EQ(deque.steal(), 1U);
    EXPECT_EQ(deque.steal(), 2U);
    EXPECT_EQ(deque.pop(), 3U);
    EXPECT_EQ(deque.steal(), std::nullopt);
    EXPECT_EQ(deque.sizeApprox(), 0U);
}

TEST(WorkStealingDequeTest, GrowsInsteadOfRejectingAPush) {
    constexpr std::uint32_t kCount = 1000;
    Deque deque{4};
    for (std::uint32_t value = 0; value < kCount; ++value) {
        deque.push(value);
    }
    EXPECT_GE(deque.capacity(), std::int64_t{kCount});
    EXPECT_EQ(deque.sizeApprox(), kCount);

    // Both ends still see every element in order after the copies.
    for (std::uint32_t value = 0; value < kCount / 2; ++value) {
        ASSERT_EQ(deque.steal(), value);
    }
    for (std::uint32_t value = kCount; value > kCount / 2; --value) {
        ASSERT_EQ(deque.pop(), value - 1);
    }
    EXPECT_EQ(deque.pop(), std::nullopt);
}

TEST(WorkStealingDequeTest, CapacityMustBeAPowerOfTwo) {
    const nx::test::ContractRecorder recorder;
    Deque deque{3};
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_EQ(nx::test::ContractRecorder::last().kind, nx::ContractKind::Precondition);

    // The deque stays usable if the handler lets execution continue.
    deque.push(7);
    EXPECT_EQ(deque.pop(), 7U);
}

TEST(WorkStealingDequeTest, EveryElementIsTakenExactlyOnceUnderContention) {
    constexpr std::uint32_t kElementCount = 100'000;
    constexpr int kThiefCount = 3;
    constexpr std::uint32_t kPopEvery = 3;

    Deque deque{2}; // tiny, so the ring grows while thieves are reading it
    std::vector<std::atomic<int>> taken(kElementCount);
    std::atomic<bool> producing{true};

    std::vector<std::thread> thieves;
    for (int thief = 0; thief < kThiefCount; ++thief) {
        thieves.emplace_back([&] {
            while (producing.load(std::memory_order_acquire) || deque.sizeApprox() != 0) {
                if (const auto value = deque.steal()) {
                    taken[*value].fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // The owner pushes everything and pops now and then, as a worker does.
    for (std::uint32_t value = 0; value < kElementCount; ++value) {
        deque.push(value);
        if (value % kPopEvery == 0) {
            if (const auto own = deque.pop()) {
                taken[*own].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    while (const auto own = deque.pop()) {
        taken[*own].fetch_add(1, std::memory_order_relaxed);
    }
    producing.store(false, std::memory_order_release);
    for (auto& thief : thieves) {
        thief.join();
    }

    const auto wrong = std::ranges::count_if(
        taken, [](const std::atomic<int>& count) { return count.load(std::memory_order_seq_cst) != 1; });
    EXPECT_EQ(wrong, 0);
}

} // namespace
