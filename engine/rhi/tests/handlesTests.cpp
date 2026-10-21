// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.rhi;

namespace {

using nx::rhi::HandlePool;
using nx::rhi::TextureHandle;
using nx::rhi::TextureTag;

TEST(HandlePoolTest, FindsWhatItHolds) {
    HandlePool<TextureTag, std::string> pool;
    const TextureHandle first = pool.insert("first");
    const TextureHandle second = pool.insert("second");
    EXPECT_NE(first, second);
    ASSERT_NE(pool.find(first), nullptr);
    EXPECT_EQ(*pool.find(first), "first");
    EXPECT_EQ(pool.get(second), "second");
    EXPECT_EQ(pool.size(), 2U);
}

TEST(HandlePoolTest, TheNullHandleNamesNothing) {
    HandlePool<TextureTag, int> pool;
    std::ignore = pool.insert(1);
    EXPECT_TRUE(TextureHandle{}.isNull());
    EXPECT_EQ(pool.find(TextureHandle{}), nullptr);
    EXPECT_FALSE(pool.remove(TextureHandle{}).has_value());
}

TEST(HandlePoolTest, ARemovedResourcesHandleNeverNamesItsSuccessor) {
    HandlePool<TextureTag, int> pool;
    const TextureHandle old = pool.insert(1);
    EXPECT_EQ(pool.remove(old), std::optional<int>{1});
    EXPECT_EQ(pool.size(), 0U);

    const TextureHandle reused = pool.insert(2);
    EXPECT_EQ(reused.index(), old.index()) << "the slot is reused";
    EXPECT_NE(reused.generation(), old.generation());
    EXPECT_EQ(pool.find(old), nullptr) << "the stale handle names nothing";
    EXPECT_FALSE(pool.remove(old).has_value());
    EXPECT_EQ(pool.get(reused), 2);
}

TEST(HandlePoolDeathTest, GettingAStaleHandleBreaksTheContract) {
    HandlePool<TextureTag, int> pool;
    const TextureHandle handle = pool.insert(1);
    std::ignore = pool.remove(handle);
    EXPECT_EQ(pool.find(handle), nullptr) << "find is the query, without a contract";
    EXPECT_DEATH(std::ignore = pool.get(handle), "names no resource");
}

} // namespace
