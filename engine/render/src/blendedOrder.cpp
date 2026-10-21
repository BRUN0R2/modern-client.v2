// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;

namespace nx::render {

namespace {

/// The modes drawn last at one distance, in their order (R_RankForRenderMode); the others are first.
constexpr std::array kLastModes{RenderMode::TransTexture, RenderMode::TransAdd, RenderMode::Glow};

/// Where an entity of `mode` is drawn among the blended entities at one distance: 0 for the others,
/// then each of kLastModes in turn.
[[nodiscard]] auto rankOf(RenderMode mode) noexcept -> std::ptrdiff_t {
    const auto found = std::ranges::find(kLastModes, mode);
    return found == kLastModes.end() ? 0 : std::ranges::distance(kLastModes.begin(), found) + 1;
}

} // namespace

auto isDrawnBefore(const BlendedDraw& first, const BlendedDraw& second) noexcept -> bool {
    if (first.distance != second.distance) {
        return first.distance > second.distance;
    }
    return rankOf(first.mode) < rankOf(second.mode);
}

auto blendedTurnsOf(std::span<const std::span<const BlendedDraw>> renderers) -> std::vector<BlendedTurn> {
    struct Ordered {
        BlendedDraw draw{};
        BlendedTurn turn{};
    };
    std::vector<Ordered> ordered;
    for (std::size_t renderer = 0; renderer < renderers.size(); ++renderer) {
        for (const BlendedDraw& draw : renderers[renderer]) {
            ordered.push_back(Ordered{.draw = draw, .turn = {.renderer = renderer, .index = draw.index}});
        }
    }
    std::ranges::stable_sort(ordered, isDrawnBefore, &Ordered::draw);
    return ordered | std::views::transform(&Ordered::turn) | std::ranges::to<std::vector>();
}

} // namespace nx::render
