// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.render;

namespace nx::ui {

namespace {

/// The names of the alignments, in the order of TextAlignment.
constexpr std::array<std::string_view, std::to_underlying(TextAlignment::Count)> kAlignmentNames{
    "north-west", "north", "north-east", "west", "center", "east", "south-west", "south", "south-east"};

/// What the room left is divided by to put a thing halfway into a length.
constexpr std::int32_t kMiddleDivisor = 2;

/// How far into a length `room` a thing `size` long starts: at its start, halfway, or at its end
/// (`side` 0, 1 or 2).
[[nodiscard]] constexpr auto placed(Pixels room, Pixels size, std::size_t side) noexcept -> Pixels {
    return side == 0 ? Pixels{} : side == 1 ? (room - size) / kMiddleDivisor : room - size;
}

} // namespace

auto textAlignmentNamed(std::string_view name) noexcept -> std::optional<TextAlignment> {
    const auto found = std::ranges::find_if(
        kAlignmentNames, [name](std::string_view known) { return equalsIgnoringCase(known, name); });
    return found != kAlignmentNames.end()
               ? std::optional{static_cast<TextAlignment>(std::distance(kAlignmentNames.begin(), found))}
               : std::nullopt;
}

Label::Label(std::string name, PixelRect bounds) : Panel{std::move(name), bounds} {}

auto Label::textPosition() const -> PixelPoint {
    const Pixels width = m_face.atlas != nullptr ? widthOf(*m_face.atlas, m_text) : Pixels{};
    const Pixels height = m_face.atlas != nullptr ? m_face.atlas->lineHeight() : Pixels{};
    // The alignments run west to east in each of three rows, north to south.
    const auto index = static_cast<std::size_t>(std::to_underlying(m_alignment));
    constexpr std::size_t kColumns = 3;
    return {.x = placed(bounds().wide, width, index % kColumns),
            .y = placed(bounds().tall, height, index / kColumns)};
}

void Label::paint(PanelPainter& painter) const {
    Panel::paint(painter);
    if (m_face.atlas == nullptr || m_text.empty()) {
        return;
    }
    std::ignore = painter.text(m_face, m_text, textPosition(), m_color); // where its pen ends is no matter
}

} // namespace nx::ui
