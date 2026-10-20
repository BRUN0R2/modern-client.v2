// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

namespace {

enum class HitGroup : std::uint8_t {
    Generic,
    Head,
    Chest,
    Stomach,
    Count,
};

} // namespace

TEST(EnumArray, IsSizedByTheCountSentinel) {
    static_assert(nx::EnumArray<HitGroup, float>::size() == 4);
    static_assert(sizeof(nx::EnumArray<HitGroup, float>) == sizeof(std::array<float, 4>));
    SUCCEED();
}

TEST(EnumArray, IsIndexedByEnumerators) {
    nx::EnumArray<HitGroup, float> damageScale{1.0F};
    damageScale[HitGroup::Head] = 4.0F;

    EXPECT_EQ(damageScale[HitGroup::Head], 4.0F);
    EXPECT_EQ(damageScale[HitGroup::Chest], 1.0F);
}

TEST(EnumArray, ListsItsKeysInDeclarationOrder) {
    std::vector<HitGroup> keys;
    for (const auto key : nx::EnumArray<HitGroup, int>::keys()) {
        keys.push_back(key);
    }

    EXPECT_EQ(keys, (std::vector{HitGroup::Generic, HitGroup::Head, HitGroup::Chest, HitGroup::Stomach}));
}

TEST(EnumArray, ComparesByValue) {
    const nx::EnumArray<HitGroup, int> left{nx::EnumArray<HitGroup, int>::Storage{1, 2, 3, 4}};
    auto right = left;

    EXPECT_EQ(left, right);
    right[HitGroup::Stomach] = 0;
    EXPECT_NE(left, right);
}

TEST(EnumArray, OutOfRangeKeysAreReportedAndStayInBounds) {
    const nx_test::ContractRecorder recorder;
    nx::EnumArray<HitGroup, int> values{nx::EnumArray<HitGroup, int>::Storage{1, 2, 3, 4}};

    const auto value = values[HitGroup::Count];

    EXPECT_EQ(recorder.count(), 1);
    EXPECT_EQ(value, 4);
}
