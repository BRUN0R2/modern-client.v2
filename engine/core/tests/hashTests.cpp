#include <gtest/gtest.h>
#include <xxhash.h>

import std;
import nx.core;

namespace {

/// Longest input checked against the reference: covers several 32-byte stripes and every tail.
constexpr std::size_t kLongestInput = 200;

[[nodiscard]] auto patternBytes(std::size_t length) -> std::vector<std::byte> {
    std::vector<std::byte> bytes(length);
    std::uint8_t value = 0x5A;
    for (auto& byte : bytes) {
        value = static_cast<std::uint8_t>(value * 37 + 11);
        byte = static_cast<std::byte>(value);
    }
    return bytes;
}

[[nodiscard]] auto referenceHash(std::span<const std::byte> bytes, std::uint64_t seed) -> std::uint64_t {
    return XXH64(bytes.data(), bytes.size(), seed);
}

} // namespace

TEST(Hash, MatchesTheReferenceImplementationForEveryLengthAndSeed) {
    for (const std::uint64_t seed : {0ULL, 1ULL, 0x9E37'79B9'7F4A'7C15ULL}) {
        for (std::size_t length = 0; length <= kLongestInput; ++length) {
            const auto bytes = patternBytes(length);
            ASSERT_EQ(nx::hash64(bytes, seed), referenceHash(bytes, seed))
                << "length " << length << ", seed " << seed;
        }
    }
}

TEST(Hash, MatchesTheKnownValueOfTheEmptyInput) {
    static_assert(nx::hash64(std::string_view{}) == 0xEF46'DB37'51D8'E999ULL);
    SUCCEED();
}

TEST(Hash, TextAndItsBytesHashTheSame) {
    constexpr std::string_view text = "cs::Transform";

    EXPECT_EQ(nx::hash64(text), nx::hash64(std::as_bytes(std::span{text})));
    EXPECT_EQ(nx::hash64(text), XXH64(text.data(), text.size(), 0));
}

TEST(Hash, RunsInConstantExpressions) {
    constexpr auto value = nx::hash64("weapon_ak47");

    static_assert(value != nx::hash64("weapon_m4a1"));
    EXPECT_EQ(value, XXH64("weapon_ak47", std::string_view{"weapon_ak47"}.size(), 0));
}

TEST(StableId, DependsOnlyOnTheName) {
    constexpr auto transform = nx::StableId::fromName("cs::Transform");

    static_assert(transform == nx::StableId::fromName("cs::Transform"));
    static_assert(transform != nx::StableId::fromName("cs::Velocity"));
    static_assert(transform.isValid());
    static_assert(!nx::StableId{}.isValid());
    EXPECT_EQ(transform.value(), nx::hash64("cs::Transform"));
}

TEST(StableId, FormatsAsSixteenHexDigits) {
    const auto text = std::format("{}", nx::StableId::fromName("cs::Transform"));

    EXPECT_EQ(text.size(), 16U);
    EXPECT_EQ(text, std::format("{:016x}", nx::hash64("cs::Transform")));
}

TEST(StableId, WorksAsAHashKey) {
    std::unordered_set<nx::StableId> ids;
    ids.insert(nx::StableId::fromName("cs::Transform"));
    ids.insert(nx::StableId::fromName("cs::Transform"));
    ids.insert(nx::StableId::fromName("cs::Velocity"));

    EXPECT_EQ(ids.size(), 2U);
}
