// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where the interface places things and how large it makes them (ADR-0020): points, sizes and
/// rectangles in whole pixels (nx::Pixels), from the top left of the screen or of a panel.
export module nx.ui:geometry;

import std;
import nx.core;

export namespace nx::ui {

/// A point, from a top left: rightward and downward, negative to its left or above it.
struct PixelPoint {
    Pixels x{};
    Pixels y{};

    [[nodiscard]] inline auto operator==(const PixelPoint&) const noexcept -> bool = default;
};

/// How wide and how tall something is; what it grew or shrank by when it changes size.
struct PixelSize {
    Pixels wide{};
    Pixels tall{};

    [[nodiscard]] inline auto operator==(const PixelSize&) const noexcept -> bool = default;
};

/// A rectangle: its top left, from the top left it is placed from, and its size, as a layout (.res)
/// writes them (`xpos`, `ypos`, `wide`, `tall`).
struct PixelRect {
    Pixels x{};
    Pixels y{};
    Pixels wide{};
    Pixels tall{};

    [[nodiscard]] inline auto operator==(const PixelRect&) const noexcept -> bool = default;

    /// Whether the point at `pointX`, `pointY`, in fractions of a pixel from the same top left (where the
    /// mouse is), is in it.
    [[nodiscard]] constexpr auto holds(float pointX, float pointY) const noexcept -> bool {
        return pointX >= static_cast<float>(x.value()) && pointX < static_cast<float>((x + wide).value()) &&
               pointY >= static_cast<float>(y.value()) && pointY < static_cast<float>((y + tall).value());
    }
};

} // namespace nx::ui
