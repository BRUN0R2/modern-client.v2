// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: the fuzz corpus's box map (tests/fuzz/corpus/bsp/boxEmbedded.bsp: 64 units, its floor at
// z = 0, in its hulls too) given other entities. Include after <gtest/gtest.h>, `import std;` and
// `import nx.platform;`, in a test that defines NX_FUZZ_CORPUS_DIRECTORY.

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

} // namespace nx::test
