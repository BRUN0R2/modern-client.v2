// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Stable 64-bit hashing and identifiers derived from explicit names.
///
/// hash64 implements the XXH64 algorithm bit for bit (verified against the reference library in
/// the tests). It reads bytes, not machine words, so the result is the same on every platform and
/// it can run in constant expressions.
export module nx.core:hash;

import std;
import :byteOrder;

export namespace nx {

namespace detail::xxh64 {

inline constexpr std::uint64_t kPrime1 = 0x9E37'79B1'85EB'CA87ULL;
inline constexpr std::uint64_t kPrime2 = 0xC2B2'AE3D'27D4'EB4FULL;
inline constexpr std::uint64_t kPrime3 = 0x1656'67B1'9E37'79F9ULL;
inline constexpr std::uint64_t kPrime4 = 0x85EB'CA77'C2B2'AE63ULL;
inline constexpr std::uint64_t kPrime5 = 0x27D4'EB2F'1656'67C5ULL;

inline constexpr std::size_t kLaneBytes = 8;
inline constexpr std::size_t kHalfLaneBytes = 4;
inline constexpr std::size_t kLaneCount = 4;
inline constexpr std::size_t kStripeBytes = kLaneBytes * kLaneCount;

inline constexpr int kRoundRotation = 31;
inline constexpr std::array<int, kLaneCount> kLaneMergeRotations{1, 7, 12, 18};
inline constexpr int kLaneTailRotation = 27;
inline constexpr int kHalfLaneTailRotation = 23;
inline constexpr int kByteTailRotation = 11;
inline constexpr int kAvalancheShift1 = 33;
inline constexpr int kAvalancheShift2 = 29;
inline constexpr int kAvalancheShift3 = 32;

/// Reads `count` bytes starting at `offset` as a little-endian unsigned integer.
template <class Byte>
[[nodiscard]] constexpr auto readLittleEndian(std::span<const Byte> bytes, std::size_t offset,
                                              std::size_t count) noexcept -> std::uint64_t {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto byte = static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[offset + index]));
        value |= byte << (index * kBitsPerByte);
    }
    return value;
}

[[nodiscard]] constexpr auto round(std::uint64_t accumulator, std::uint64_t input) noexcept -> std::uint64_t {
    accumulator += input * kPrime2;
    accumulator = std::rotl(accumulator, kRoundRotation);
    return accumulator * kPrime1;
}

[[nodiscard]] constexpr auto mergeRound(std::uint64_t accumulator, std::uint64_t lane) noexcept
    -> std::uint64_t {
    accumulator ^= round(0, lane);
    return accumulator * kPrime1 + kPrime4;
}

template <class Byte>
[[nodiscard]] constexpr auto hash(std::span<const Byte> bytes, std::uint64_t seed) noexcept -> std::uint64_t {
    const std::size_t length = bytes.size();
    std::size_t offset = 0;
    std::uint64_t result = 0;

    if (length >= kStripeBytes) {
        std::array<std::uint64_t, kLaneCount> lanes{seed + kPrime1 + kPrime2, seed + kPrime2, seed,
                                                    seed - kPrime1};
        while (length - offset >= kStripeBytes) {
            for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
                lanes[lane] =
                    round(lanes[lane], readLittleEndian(bytes, offset + lane * kLaneBytes, kLaneBytes));
            }
            offset += kStripeBytes;
        }
        for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
            result += std::rotl(lanes[lane], kLaneMergeRotations[lane]);
        }
        for (const auto lane : lanes) {
            result = mergeRound(result, lane);
        }
    } else {
        result = seed + kPrime5;
    }

    result += static_cast<std::uint64_t>(length);

    while (length - offset >= kLaneBytes) {
        result ^= round(0, readLittleEndian(bytes, offset, kLaneBytes));
        result = std::rotl(result, kLaneTailRotation) * kPrime1 + kPrime4;
        offset += kLaneBytes;
    }
    if (length - offset >= kHalfLaneBytes) {
        result ^= readLittleEndian(bytes, offset, kHalfLaneBytes) * kPrime1;
        result = std::rotl(result, kHalfLaneTailRotation) * kPrime2 + kPrime3;
        offset += kHalfLaneBytes;
    }
    while (offset < length) {
        result ^= readLittleEndian(bytes, offset, 1) * kPrime5;
        result = std::rotl(result, kByteTailRotation) * kPrime1;
        ++offset;
    }

    result ^= result >> kAvalancheShift1;
    result *= kPrime2;
    result ^= result >> kAvalancheShift2;
    result *= kPrime3;
    result ^= result >> kAvalancheShift3;
    return result;
}

} // namespace detail::xxh64

/// XXH64 of `bytes`.
[[nodiscard]] constexpr auto hash64(std::span<const std::byte> bytes, std::uint64_t seed = 0) noexcept
    -> std::uint64_t {
    return detail::xxh64::hash(bytes, seed);
}

/// XXH64 of the bytes of `text` (UTF-8 code units, no terminator).
[[nodiscard]] constexpr auto hash64(std::string_view text, std::uint64_t seed = 0) noexcept -> std::uint64_t {
    return detail::xxh64::hash(std::span{text}, seed);
}

/// Identifier that stays the same across builds, compilers and platforms (network, saves,
/// replays, plugins). It is derived from an explicit name, never from a compiler-generated type
/// name, whose spelling differs between compilers.
class StableId {
public:
    constexpr StableId() noexcept = default;

    [[nodiscard]] static constexpr auto fromName(std::string_view name) noexcept -> StableId {
        return StableId{hash64(name)};
    }

    [[nodiscard]] constexpr auto value() const noexcept -> std::uint64_t { return m_value; }
    [[nodiscard]] constexpr auto isValid() const noexcept -> bool { return m_value != 0; }

    [[nodiscard]] constexpr auto operator<=>(const StableId&) const noexcept = default;

private:
    constexpr explicit StableId(std::uint64_t value) noexcept : m_value{value} {}

    std::uint64_t m_value = 0;
};

} // namespace nx

template <>
struct std::hash<nx::StableId> {
    [[nodiscard]] inline auto operator()(nx::StableId id) const noexcept -> std::size_t {
        return static_cast<std::size_t>(id.value());
    }
};

/// Formats as 16 hexadecimal digits, e.g. "9e1c3a54c2b1f0d7". Takes no format spec: anything
/// other than "{}" is rejected when the format string is checked at compile time.
template <>
struct std::formatter<nx::StableId> {
    constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

    template <class FormatContext>
    auto format(nx::StableId id, FormatContext& context) const {
        return std::format_to(context.out(), "{:016x}", id.value());
    }
};
