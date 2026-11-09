// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.rhi;
import nx.render;

namespace nx::ui {

namespace {

/// The part of `rect`, placed from `origin` on the screen, within `clip`; empty when they do not meet.
/// In 64 bits, so that no place a panel is given overflows on its way to the screen.
[[nodiscard]] auto clipped(const rhi::Rect& clip, PixelPoint origin, const PixelRect& rect) noexcept
    -> rhi::Rect {
    const std::int64_t x = std::int64_t{origin.x.value()} + rect.x.value();
    const std::int64_t y = std::int64_t{origin.y.value()} + rect.y.value();
    const std::int64_t left = std::max<std::int64_t>(x, clip.x);
    const std::int64_t top = std::max<std::int64_t>(y, clip.y);
    const std::int64_t right =
        std::min(x + std::max(rect.wide.value(), 0), std::int64_t{clip.x} + clip.size.width);
    const std::int64_t bottom =
        std::min(y + std::max(rect.tall.value(), 0), std::int64_t{clip.y} + clip.size.height);
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

PanelPainter::PanelPainter(std::vector<render::ScreenQuad>& quads, PixelPoint origin, rhi::Rect clip) noexcept
    : m_quads{&quads}, m_origin{origin}, m_clip{clip} {}

void PanelPainter::fill(const PixelRect& rect, render::ScreenColor color) {
    if (isClippedAway() || rect.wide <= Pixels{} || rect.tall <= Pixels{}) {
        return;
    }
    m_quads->push_back({.target = {.x = static_cast<float>((m_origin.x + rect.x).value()),
                                   .y = static_cast<float>((m_origin.y + rect.y).value()),
                                   .width = static_cast<float>(rect.wide.value()),
                                   .height = static_cast<float>(rect.tall.value())},
                        .color = color,
                        .blend = render::ScreenBlend::Translucent,
                        .clip = m_clip});
}

auto PanelPainter::text(const TextFace& face, std::string_view text, PixelPoint pen,
                        render::ScreenColor color) -> Pixels {
    expects(face.atlas != nullptr && face.image != nullptr,
            "PanelPainter::text: a face with its atlas on the GPU");
    if (isClippedAway()) {
        return pen.x + widthOf(*face.atlas, text);
    }
    const std::size_t first = m_quads->size();
    const float end =
        appendText(*m_quads, *face.atlas, *face.image, text, static_cast<float>((m_origin.x + pen.x).value()),
                   static_cast<float>((m_origin.y + pen.y).value()), color);
    for (render::ScreenQuad& quad : std::span{*m_quads}.subspan(first)) {
        quad.clip = m_clip;
    }
    return Pixels{narrow<std::int32_t>(std::lround(end))} - m_origin.x;
}

auto PanelPainter::inside(const PixelRect& rect) const noexcept -> PanelPainter {
    const std::int64_t left = std::int64_t{m_origin.x.value()} + rect.x.value();
    const std::int64_t top = std::int64_t{m_origin.y.value()} + rect.y.value();
    return PanelPainter{*m_quads,
                        {.x = Pixels{narrow<std::int32_t>(left)}, .y = Pixels{narrow<std::int32_t>(top)}},
                        clipped(m_clip, m_origin, rect)};
}

} // namespace nx::ui
