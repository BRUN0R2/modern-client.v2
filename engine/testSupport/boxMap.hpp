// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: the fuzz corpus's box map (tests/fuzz/corpus/bsp/boxEmbedded.bsp: 64 units, its floor at
// z = 0, in its hulls too) given other entities, and stretched for a navigation mesh. Include after
// <gtest/gtest.h>, `import std;` and `import nx.platform;`, in a test that defines
// NX_FUZZ_CORPUS_DIRECTORY.

namespace nx::test {

/// Where the header of a BSP keeps the offset and the length of its entities.
inline constexpr std::size_t kBspEntitiesLump = 4;

/// The bytes of the box map whose entities are `entities`, the text of its entities lump, at the end of
/// its file, its terminating zero included.
[[nodiscard]] inline auto boxMapWith(std::string_view entities) -> std::vector<std::byte> {
    auto seed =
        nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp");
    EXPECT_TRUE(seed.has_value());
    std::vector<std::byte> bytes = seed.value_or(std::vector<std::byte>{});
    const auto offset = static_cast<std::int32_t>(bytes.size());
    const auto length = static_cast<std::int32_t>(entities.size() + 1);
    std::ranges::copy(std::as_bytes(std::span{entities}), std::back_inserter(bytes));
    bytes.push_back(std::byte{0});
    const auto header =
        std::bit_cast<std::array<std::byte, sizeof(std::int32_t) * 2>>(std::array{offset, length});
    std::ranges::copy(header, bytes.begin() + static_cast<std::ptrdiff_t>(kBspEntitiesLump));
    return bytes;
}

/// The side of the box map.
inline constexpr float kBoxSide = 64.0F;

/// The box map whose entities are `entities`, its vertices stretched to `width` along x and `depth`
/// along y from kBoxSide: the navigation meshes of the tests walk its top, kBoxSide above its floor, over
/// that much; its hulls stay the box's.
[[nodiscard]] inline auto stretchedBoxMapWith(std::string_view entities, float width, float depth)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes = boxMapWith(entities);
    // A BSP's vertices are its fourth lump: their offset and length after the version word and the three
    // lumps before them.
    constexpr std::size_t kVertexLump = 3;
    constexpr std::size_t kLumpFields = 2;
    constexpr std::size_t kHeaderAt = sizeof(std::int32_t) + kVertexLump * kLumpFields * sizeof(std::int32_t);
    std::array<std::int32_t, kLumpFields> lump{};
    EXPECT_GE(bytes.size(), kHeaderAt + sizeof(lump));
    std::memcpy(lump.data(), bytes.data() + kHeaderAt, sizeof(lump));
    const auto offset = static_cast<std::size_t>(lump[0]);
    const auto length = static_cast<std::size_t>(lump[1]);
    using Vertex = std::array<float, 3>;
    EXPECT_TRUE(offset <= bytes.size() && length <= bytes.size() - offset && length % sizeof(Vertex) == 0);
    for (std::size_t at = offset; at + sizeof(Vertex) <= offset + length; at += sizeof(Vertex)) {
        Vertex point{};
        std::memcpy(point.data(), bytes.data() + at, sizeof(point));
        point[0] *= width / kBoxSide;
        point[1] *= depth / kBoxSide;
        std::memcpy(bytes.data() + at, point.data(), sizeof(point));
    }
    return bytes;
}

} // namespace nx::test
