// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Numbers stored in bytes: the little-endian fields of the file formats (WAD3, BSP, MDL, SPR,
/// WAV), read the same on any host.
export module nx.core:byteOrder;

import std;
import :contracts;
import :conversion;

export namespace nx {

/// The bits of a byte, as every format stores them.
inline constexpr std::size_t kBitsPerByte = std::numeric_limits<std::uint8_t>::digits;

/// The bytes that hold `bits` bits: a whole byte for the bits left over.
[[nodiscard]] constexpr auto bytesForBits(std::size_t bits) noexcept -> std::size_t {
    return (bits + kBitsPerByte - 1) / kBitsPerByte;
}

/// The `Value` stored little-endian in the first sizeof(Value) bytes of `bytes`, which must hold
/// them (the caller checks the bounds of external data first). Floats are IEEE 754, as the formats
/// store them.
template <class Value>
    requires Integer<Value> || (std::floating_point<Value> && std::numeric_limits<Value>::is_iec559)
[[nodiscard]] constexpr auto loadLittleEndian(std::span<const std::byte> bytes) noexcept -> Value {
    std::array<std::byte, sizeof(Value)> raw{};
    expects(bytes.size() >= raw.size(), "loadLittleEndian: fewer bytes than the value");
    if (bytes.size() < raw.size()) {
        return Value{}; // the handler let execution continue ("observe")
    }
    std::ranges::copy(bytes.first(raw.size()), raw.begin());
    if constexpr (std::endian::native == std::endian::big) {
        std::ranges::reverse(raw);
    }
    return std::bit_cast<Value>(raw);
}

} // namespace nx
