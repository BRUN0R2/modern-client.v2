// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

namespace sample {

enum class Weapon : std::uint8_t {
    Knife,
    Glock,
    Ak47,
    Count,
};

enum class Sparse : std::uint16_t {
    First = 0,
    Tenth = 10,
};

enum class Signed : std::int8_t {
    Negative = -1,
    Zero = 0,
    Positive = 1,
};

} // namespace sample

TEST(Introspection, NamesEveryEnumeratorOfACountedEnum) {
    EXPECT_EQ(nx::enumName(sample::Weapon::Knife), "Knife");
    EXPECT_EQ(nx::enumName(sample::Weapon::Glock), "Glock");
    EXPECT_EQ(nx::enumName(sample::Weapon::Ak47), "Ak47");
    EXPECT_EQ(nx::enumName(sample::Weapon::Count), "Count");
}

TEST(Introspection, NamesEnumeratorsOfAnEnumWithoutCount) {
    EXPECT_EQ(nx::enumName(sample::Sparse::First), "First");
    EXPECT_EQ(nx::enumName(sample::Sparse::Tenth), "Tenth");
}

TEST(Introspection, ValuesWithoutEnumeratorHaveNoName) {
    EXPECT_TRUE(nx::enumName(static_cast<sample::Sparse>(5)).empty());
    EXPECT_TRUE(nx::enumName(static_cast<sample::Weapon>(200)).empty());
}

TEST(Introspection, NegativeValuesAreOutsideTheScannedRange) {
    EXPECT_TRUE(nx::enumName(sample::Signed::Negative).empty());
    EXPECT_EQ(nx::enumName(sample::Signed::Positive), "Positive");
}

TEST(Introspection, NamesTypes) {
    EXPECT_EQ(nx::typeName<sample::Weapon>(), "sample::Weapon");
    EXPECT_EQ(nx::shortTypeName<sample::Weapon>(), "Weapon");
    EXPECT_EQ(nx::typeName<int>(), "int");
}

TEST(Introspection, CountsEnumeratorsBeforeTheSentinel) {
    static_assert(nx::enumCount<sample::Weapon>() == 3);
    static_assert(nx::CountedEnum<sample::Weapon>);
    static_assert(!nx::CountedEnum<sample::Sparse>);
    SUCCEED();
}

TEST(Introspection, WorksInConstantExpressions) {
    static_assert(nx::enumName(sample::Weapon::Ak47) == "Ak47");
    static_assert(nx::shortTypeName<sample::Sparse>() == "Sparse");
    SUCCEED();
}
