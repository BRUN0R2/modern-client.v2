// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.rhi;
import nx.render;

namespace nx::ui {

namespace {

/// The part of the rectangle at `x`, `y`, `wide` by `tall` within `clip`; empty when they do not meet.
[[nodiscard]] auto clipped(const rhi::Rect& clip, std::int64_t x, std::int64_t y, std::int64_t wide,
                           std::int64_t tall) noexcept -> rhi::Rect {
    const std::int64_t left = std::max<std::int64_t>(x, clip.x);
    const std::int64_t top = std::max<std::int64_t>(y, clip.y);
    const std::int64_t right = std::min(x + wide, std::int64_t{clip.x} + clip.size.width);
    const std::int64_t bottom = std::min(y + tall, std::int64_t{clip.y} + clip.size.height);
    if (right <= left || bottom <= top) {
        return {.x = clip.x, .y = clip.y};
    }
    return {.x = narrow<std::int32_t>(left),
            .y = narrow<std::int32_t>(top),
            .size = {.width = narrow<std::uint32_t>(right - left),
                     .height = narrow<std::uint32_t>(bottom - top)}};
}

} // namespace

PanelPainter::PanelPainter(std::vector<render::ScreenQuad>& quads, rhi::Extent2d size) noexcept
    : m_quads{&quads}, m_clip{.x = 0, .y = 0, .size = size} {}

PanelPainter::PanelPainter(std::vector<render::ScreenQuad>& quads, std::int32_t x, std::int32_t y,
                           rhi::Rect clip) noexcept
    : m_quads{&quads}, m_x{x}, m_y{y}, m_clip{clip} {}

void PanelPainter::fill(std::int32_t x, std::int32_t y, std::int32_t wide, std::int32_t tall,
                        render::ScreenColor color) {
    if (isClippedAway() || wide <= 0 || tall <= 0) {
        return;
    }
    m_quads->push_back({.target = {.x = static_cast<float>(m_x + x),
                                   .y = static_cast<float>(m_y + y),
                                   .width = static_cast<float>(wide),
                                   .height = static_cast<float>(tall)},
                        .color = color,
                        .blend = render::ScreenBlend::Translucent,
                        .clip = m_clip});
}

auto PanelPainter::text(const TextFace& face, std::string_view text, std::int32_t x, std::int32_t y,
                        render::ScreenColor color) -> std::int32_t {
    expects(face.atlas != nullptr && face.image != nullptr,
            "PanelPainter::text: a face with its atlas on the GPU");
    if (isClippedAway()) {
        return x + widthOf(*face.atlas, text);
    }
    const std::size_t first = m_quads->size();
    const float end = appendText(*m_quads, *face.atlas, *face.image, text, static_cast<float>(m_x + x),
                                 static_cast<float>(m_y + y), color);
    for (render::ScreenQuad& quad : std::span{*m_quads}.subspan(first)) {
        quad.clip = m_clip;
    }
    return narrow<std::int32_t>(std::lround(end)) - m_x;
}

auto PanelPainter::inside(std::int32_t x, std::int32_t y, std::int32_t wide, std::int32_t tall) const noexcept
    -> PanelPainter {
    const std::int64_t left = std::int64_t{m_x} + x;
    const std::int64_t top = std::int64_t{m_y} + y;
    return PanelPainter{*m_quads, narrow<std::int32_t>(left), narrow<std::int32_t>(top),
                        clipped(m_clip, left, top, std::max(wide, 0), std::max(tall, 0))};
}

} // namespace nx::ui
