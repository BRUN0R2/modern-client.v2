// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: the interface's points and rectangles written as the whole numbers of a test's expected
// values. Include after `import nx.core;` and `import nx.ui;`.

namespace nx::test {

/// The rectangle at `x`, `y`, `wide` by `tall` pixels.
[[nodiscard]] constexpr auto rectOf(std::int32_t x, std::int32_t y, std::int32_t wide,
                                    std::int32_t tall) noexcept -> nx::ui::PixelRect {
    return {.x = nx::Pixels{x}, .y = nx::Pixels{y}, .wide = nx::Pixels{wide}, .tall = nx::Pixels{tall}};
}

/// The point at `x`, `y` pixels.
[[nodiscard]] constexpr auto pointOf(std::int32_t x, std::int32_t y) noexcept -> nx::ui::PixelPoint {
    return {.x = nx::Pixels{x}, .y = nx::Pixels{y}};
}

} // namespace nx::test
