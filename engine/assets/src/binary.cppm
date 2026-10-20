// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the readers of binary formats share inside nx.assets: fixed-size names, and little-endian
/// vectors and whether they are finite. Nothing here is exported, and the definitions stay in
/// binary.cpp: GCC 16 mangles an iterator comparison instantiated in a module interface so that it
/// conflicts with the same comparison in an importer.
export module nx.assets:binary;

import std;
import nx.math;

namespace nx::assets {

/// The text of a fixed-size name field: up to its first zero byte, or all of it.
[[nodiscard]] auto nameOf(std::span<const std::byte> field) -> std::string;

/// The three little-endian floats at the start of `bytes`.
[[nodiscard]] auto readVec3(std::span<const std::byte> bytes) noexcept -> Vec3;

/// The four little-endian floats at the start of `bytes`.
[[nodiscard]] auto readVec4(std::span<const std::byte> bytes) noexcept -> Vec4;

[[nodiscard]] auto isFinite(Vec3 vector) noexcept -> bool;
[[nodiscard]] auto isFinite(Vec4 vector) noexcept -> bool;

} // namespace nx::assets
