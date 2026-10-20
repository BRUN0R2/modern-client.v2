// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.math;

namespace nx::assets {

auto nameOf(std::span<const std::byte> field) -> std::string {
    return std::ranges::subrange(field.begin(), std::ranges::find(field, std::byte{0})) |
           std::views::transform([](std::byte byte) { return static_cast<char>(byte); }) |
           std::ranges::to<std::string>();
}

auto readVec3(std::span<const std::byte> bytes) noexcept -> Vec3 {
    return Vec3{.x = loadLittleEndian<float>(bytes),
                .y = loadLittleEndian<float>(bytes.subspan(sizeof(float))),
                .z = loadLittleEndian<float>(bytes.subspan(2 * sizeof(float)))};
}

auto readVec4(std::span<const std::byte> bytes) noexcept -> Vec4 {
    const Vec3 xyz = readVec3(bytes);
    return Vec4{
        .x = xyz.x, .y = xyz.y, .z = xyz.z, .w = loadLittleEndian<float>(bytes.subspan(3 * sizeof(float)))};
}

auto isFinite(Vec3 vector) noexcept -> bool {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

auto isFinite(Vec4 vector) noexcept -> bool {
    return isFinite(Vec3{.x = vector.x, .y = vector.y, .z = vector.z}) && std::isfinite(vector.w);
}

} // namespace nx::assets
