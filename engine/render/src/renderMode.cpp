// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;

namespace nx::render {

namespace {

/// The blended render modes in the order the original draws them at one distance, after the others.
constexpr std::array kBlendedDrawOrder{RenderMode::TransTexture, RenderMode::TransAdd, RenderMode::Glow};

} // namespace

auto blendedRankOf(RenderMode mode) noexcept -> std::ptrdiff_t {
    const auto found = std::ranges::find(kBlendedDrawOrder, mode);
    return found == kBlendedDrawOrder.end() ? 0 : std::ranges::distance(kBlendedDrawOrder.begin(), found) + 1;
}

} // namespace nx::render
