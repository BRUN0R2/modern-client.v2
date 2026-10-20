// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

namespace {

constexpr std::array kBytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                            std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}};

TEST(ByteOrderTest, ReadsLittleEndianIntegers) {
    static_assert(nx::loadLittleEndian<std::uint8_t>(kBytes) == 0x01);
    static_assert(nx::loadLittleEndian<std::uint16_t>(kBytes) == 0x0201);
    static_assert(nx::loadLittleEndian<std::uint32_t>(kBytes) == 0x0403'0201);
    static_assert(nx::loadLittleEndian<std::uint64_t>(kBytes) == 0x0807'0605'0403'0201);
    static_assert(nx::loadLittleEndian<std::int16_t>(std::array{std::byte{0xFE}, std::byte{0xFF}}) == -2);
}

TEST(ByteOrderTest, ReadsIeeeFloats) {
    constexpr std::array oneAndAHalf{std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x3F}};
    static_assert(nx::loadLittleEndian<float>(oneAndAHalf) == 1.5F);
}

TEST(ByteOrderTest, TooFewBytesBreakTheContract) {
    const nx::test::ContractRecorder recorder;
    EXPECT_EQ(nx::loadLittleEndian<std::uint32_t>(std::span{kBytes}.first(3)), 0U);
    EXPECT_EQ(recorder.count(), 1);
}

} // namespace
