// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;

namespace {

struct TestNode {
    std::atomic<nx::jobs::PoolLink> nextFree{nx::jobs::PoolLink::None};
    std::atomic<int> holder{-1};
};

using Pool = nx::jobs::ConcurrentPool<TestNode>;

TEST(ConcurrentPoolTest, AcquiresDistinctNodes) {
    constexpr std::size_t kCount = 1000; // spans several chunks
    Pool pool;
    std::set<nx::jobs::PoolIndex> indices;
    std::set<const TestNode*> addresses;
    for (std::size_t acquired = 0; acquired < kCount; ++acquired) {
        const auto index = pool.acquire();
        indices.insert(index);
        addresses.insert(&pool[index]);
    }
    EXPECT_EQ(indices.size(), kCount);
    EXPECT_EQ(addresses.size(), kCount);
    EXPECT_EQ(pool.createdCount(), kCount);
}

TEST(ConcurrentPoolTest, ReusesReleasedNodesBeforeCreatingNewOnes) {
    Pool pool;
    const auto first = pool.acquire();
    const auto second = pool.acquire();
    pool.release(first);

    EXPECT_EQ(pool.acquire(), first);
    EXPECT_NE(pool.acquire(), second);
    EXPECT_EQ(pool.createdCount(), 3U);
}

TEST(ConcurrentPoolTest, NodesKeepTheirAddressWhileThePoolGrows) {
    constexpr int kGrowth = 10'000;
    Pool pool;
    const auto first = pool.acquire();
    const TestNode* const address = &pool[first];
    for (int acquired = 0; acquired < kGrowth; ++acquired) {
        std::ignore = pool.acquire(); // only the growth matters here
    }
    EXPECT_EQ(&pool[first], address);
}

TEST(ConcurrentPoolTest, NoNodeIsHeldByTwoThreadsAtOnce) {
    constexpr int kThreadCount = 4;
    constexpr int kRounds = 20'000;
    constexpr std::size_t kHeldAtOnce = 8; // mixes acquisition order with release order

    Pool pool;
    std::atomic<int> conflicts{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        threads.emplace_back([&pool, &conflicts, thread] {
            std::vector<nx::jobs::PoolIndex> held;
            for (int round = 0; round < kRounds; ++round) {
                const auto index = pool.acquire();
                if (pool[index].holder.exchange(thread, std::memory_order_relaxed) != -1) {
                    conflicts.fetch_add(1, std::memory_order_relaxed);
                }
                held.push_back(index);
                if (held.size() == kHeldAtOnce) {
                    for (const auto releasing : held) {
                        pool[releasing].holder.store(-1, std::memory_order_relaxed);
                        pool.release(releasing);
                    }
                    held.clear();
                }
            }
            for (const auto releasing : held) {
                pool[releasing].holder.store(-1, std::memory_order_relaxed);
                pool.release(releasing);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(conflicts.load(std::memory_order_seq_cst), 0);
    EXPECT_LE(pool.createdCount(), std::size_t{kThreadCount} * kHeldAtOnce);
}

} // namespace
