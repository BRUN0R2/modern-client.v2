// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a share of a change goes over its time.
export module nx.math:easing;

export namespace nx {

/// `share`, from 0 to 1, eased in and out: slow first and last, fast between (smoothstep).
[[nodiscard]] constexpr auto smoothstep(float share) noexcept -> float {
    // 3s^2 - 2s^3, the cubic from 0 to 1 that is flat at both ends.
    constexpr float kSquareFactor = 3.0F;
    constexpr float kCubeFactor = 2.0F;
    return share * share * (kSquareFactor - (kCubeFactor * share));
}

} // namespace nx
