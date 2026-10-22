// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The single-producer single-consumer queue: in order, full at its capacity, around its ring, and
// between two threads.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;

namespace {

using nx::jobs::QueueError;
using nx::jobs::SpscQueue;

TEST(SpscQueueTest, ElementsComeOutInTheirOrder) {
    SpscQueue<int, 4> queue;
    EXPECT_EQ(queue.pop(), std::nullopt);
    for (const int element : {1, 2, 3}) {
        ASSERT_TRUE(queue.push(element).has_value());
    }
    EXPECT_EQ(queue.pop(), 1);
    EXPECT_EQ(queue.pop(), 2);
    EXPECT_EQ(queue.pop(), 3);
    EXPECT_EQ(queue.pop(), std::nullopt);
}

TEST(SpscQueueTest, AFullQueueRefusesAndTakesAgainOnceOneIsOut) {
    SpscQueue<int, 2> queue;
    ASSERT_TRUE(queue.push(1).has_value());
    ASSERT_TRUE(queue.push(2).has_value());
    const auto refused = queue.push(3);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, QueueError::Full);
    EXPECT_TRUE(refused.error().detail.empty());
    EXPECT_EQ(queue.pop(), 1);
    ASSERT_TRUE(queue.push(3).has_value());
    EXPECT_EQ(queue.pop(), 2);
    EXPECT_EQ(queue.pop(), 3);
}

TEST(SpscQueueTest, ItGoesAroundItsRing) {
    SpscQueue<std::string, 4> queue;
    for (int round = 0; round < 100; ++round) {
        ASSERT_TRUE(queue.push(std::to_string(round)).has_value());
        ASSERT_TRUE(queue.push(std::to_string(-round)).has_value());
        EXPECT_EQ(queue.pop(), std::to_string(round));
        EXPECT_EQ(queue.pop(), std::to_string(-round));
    }
}

TEST(SpscQueueTest, EveryElementCrossesFromOneThreadToTheOtherInOrder) {
    constexpr int kElements = 200'000;
    auto queue = std::make_unique<SpscQueue<int, 64>>();
    std::thread producer{[&queue] {
        for (int element = 0; element < kElements;) {
            if (queue->push(element).has_value()) {
                ++element;
            } else {
                std::this_thread::yield();
            }
        }
    }};
    int expected = 0;
    while (expected < kElements) {
        if (const std::optional<int> element = queue->pop()) {
            ASSERT_EQ(*element, expected);
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    EXPECT_EQ(queue->pop(), std::nullopt);
}

} // namespace
