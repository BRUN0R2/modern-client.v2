// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;

namespace {

struct TestNode : nx::jobs::QueueLink {
    int producer = 0;
    int sequence = 0;
};

using Queue = nx::jobs::IntrusiveQueue<TestNode>;

TEST(IntrusiveQueueTest, StartsEmpty) {
    Queue queue;
    EXPECT_FALSE(queue.hasVisibleNodes());
    EXPECT_EQ(queue.tryPop(), nullptr);
}

TEST(IntrusiveQueueTest, PopsInPushOrder) {
    constexpr int kCount = 5;
    std::array<TestNode, kCount> nodes;
    Queue queue;
    for (int index = 0; index < kCount; ++index) {
        nodes[static_cast<std::size_t>(index)].sequence = index;
        queue.push(nodes[static_cast<std::size_t>(index)]);
    }
    EXPECT_TRUE(queue.hasVisibleNodes());

    for (int index = 0; index < kCount; ++index) {
        const TestNode* const popped = queue.tryPop();
        ASSERT_NE(popped, nullptr);
        EXPECT_EQ(popped->sequence, index);
    }
    EXPECT_EQ(queue.tryPop(), nullptr);
    EXPECT_FALSE(queue.hasVisibleNodes());
}

TEST(IntrusiveQueueTest, ANodeCanBeQueuedAgainAfterItWasPopped) {
    TestNode node;
    Queue queue;
    for (int round = 0; round < 3; ++round) {
        queue.push(node);
        EXPECT_EQ(queue.tryPop(), &node);
        EXPECT_EQ(queue.tryPop(), nullptr);
    }
}

TEST(IntrusiveQueueTest, KeepsTheOrderOfEachProducerUnderContention) {
    constexpr int kProducerCount = 4;
    constexpr int kNodesPerProducer = 20'000;
    constexpr int kTotal = kProducerCount * kNodesPerProducer;

    std::vector<TestNode> nodes(static_cast<std::size_t>(kTotal));
    Queue queue;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([&nodes, &queue, producer] {
            for (int sequence = 0; sequence < kNodesPerProducer; ++sequence) {
                TestNode& node = nodes[static_cast<std::size_t>(producer * kNodesPerProducer + sequence)];
                node.producer = producer;
                node.sequence = sequence;
                queue.push(node);
            }
        });
    }

    std::array<int, kProducerCount> nextSequence{};
    int received = 0;
    int outOfOrder = 0;
    while (received < kTotal) {
        if (const TestNode* const node = queue.tryPop()) {
            auto& expected = nextSequence[static_cast<std::size_t>(node->producer)];
            if (node->sequence != expected) {
                ++outOfOrder;
            }
            expected = node->sequence + 1;
            ++received;
        } else {
            std::this_thread::yield();
        }
    }
    for (auto& producer : producers) {
        producer.join();
    }
    EXPECT_EQ(outOfOrder, 0);
    EXPECT_EQ(queue.tryPop(), nullptr);
}

TEST(IntrusiveQueueTest, ConcurrentConsumersTakeEachNodeOnce) {
    constexpr int kProducerCount = 2;
    constexpr int kConsumerCount = 3;
    constexpr int kNodesPerProducer = 20'000;
    constexpr int kTotal = kProducerCount * kNodesPerProducer;

    std::vector<TestNode> nodes(static_cast<std::size_t>(kTotal));
    std::vector<std::atomic<int>> taken(static_cast<std::size_t>(kTotal));
    std::atomic<int> received{0};
    Queue queue;

    std::vector<std::thread> threads;
    for (int producer = 0; producer < kProducerCount; ++producer) {
        threads.emplace_back([&nodes, &queue, producer] {
            for (int sequence = 0; sequence < kNodesPerProducer; ++sequence) {
                TestNode& node = nodes[static_cast<std::size_t>(producer * kNodesPerProducer + sequence)];
                node.sequence = producer * kNodesPerProducer + sequence;
                queue.push(node);
            }
        });
    }
    for (int consumer = 0; consumer < kConsumerCount; ++consumer) {
        threads.emplace_back([&] {
            while (received.load(std::memory_order_relaxed) < kTotal) {
                if (const TestNode* const node = queue.tryPop()) {
                    taken[static_cast<std::size_t>(node->sequence)].fetch_add(1, std::memory_order_relaxed);
                    received.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const auto wrong = std::ranges::count_if(
        taken, [](const std::atomic<int>& count) { return count.load(std::memory_order_seq_cst) != 1; });
    EXPECT_EQ(wrong, 0);
}

} // namespace
