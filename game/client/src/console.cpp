// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;

namespace cs {

auto consoleCharacters() -> std::u32string {
    std::u32string characters;
    constexpr char32_t kSpace = 0x20;
    constexpr char32_t kTilde = 0x7E;
    constexpr char32_t kNoBreakSpace = 0xA0;
    constexpr char32_t kLastLatin1 = 0xFF;
    for (char32_t character = kSpace; character <= kTilde; ++character) {
        characters += character;
    }
    for (char32_t character = kNoBreakSpace; character <= kLastLatin1; ++character) {
        characters += character;
    }
    // The rest of code page 1252 that the console meets: the euro, quotes, dashes and the ellipsis.
    constexpr auto kCodePageExtras =
        std::to_array<char32_t>({0x20AC, 0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026});
    for (const char32_t character : kCodePageExtras) {
        characters += character;
    }
    return characters;
}

auto consoleBounds(nx::rhi::Extent2d size) -> nx::ui::PanelBounds {
    constexpr std::int32_t kMargin = 8; // an eighth of the screen on each side
    const auto wide = nx::narrow<std::int32_t>(size.width);
    const auto tall = nx::narrow<std::int32_t>(size.height);
    return {.x = wide / kMargin,
            .y = tall / kMargin,
            .wide = wide - (2 * (wide / kMargin)),
            .tall = tall - (2 * (tall / kMargin))};
}

auto ConsoleWindow::open(const WindowFonts& fonts, nx::console::Console& console, std::uint32_t screenHeight)
    -> nx::Result<std::unique_ptr<ConsoleWindow>, WindowFontError> {
    auto font = fonts.font(kConsoleFont, screenHeight);
    if (!font) {
        return std::unexpected{std::move(font.error())};
    }
    auto atlas = nx::ui::GlyphAtlas::build(*font, consoleCharacters());
    if (!atlas) {
        return nx::fail(WindowFontError::Unusable, std::string{kConsoleFont}, std::move(atlas.error()));
    }
    return std::make_unique<ConsoleWindow>(CreateToken{}, console, std::move(*font), std::move(*atlas));
}

ConsoleWindow::ConsoleWindow(CreateToken /*token*/, nx::console::Console& console, nx::ui::SchemeFontSet font,
                             nx::ui::GlyphAtlas atlas) noexcept
    : m_console{&console}, m_font{std::move(font)}, m_atlas{std::move(atlas)} {}

auto ConsoleWindow::load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                         nx::rhi::Extent2d size) -> nx::Result<void, nx::render::ScreenError> {
    auto image = screen.loadImage(commands, m_atlas.texels(), m_atlas.width(), m_atlas.height(), "console");
    if (!image) {
        return std::unexpected{std::move(image.error())};
    }
    m_image.emplace(std::move(*image));
    m_dialog.emplace(*m_console, consoleBounds(size));
    m_dialog->setFace({.atlas = &m_atlas, .image = &*m_image});
    return {};
}

void ConsoleWindow::paint(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) {
    if (!m_dialog || !m_isOpen) {
        return;
    }
    m_dialog->setBounds(consoleBounds(size));
    m_dialog->paintTree(nx::ui::PanelPainter{quads, size});
}

} // namespace cs
