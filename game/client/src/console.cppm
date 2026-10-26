// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's window as the client shows it (docs/12-ui-hud-and-console.md, section 5): its font,
/// the Default of the scheme of the installation's windows (WindowFonts); the characters its atlas
/// holds; where it goes on the screen; and the window itself, the same in next and nxview.
export module cs.client:console;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;
import :windowFonts;

export namespace cs {

/// The font of kWindowScheme the console writes in.
inline constexpr std::string_view kConsoleFont = "Default";

/// The characters the console's atlas holds: those of Windows' code page 1252 that print, which the
/// original's console shows.
[[nodiscard]] auto consoleCharacters() -> std::u32string;

/// Where the console's window goes on a screen of `size`: in its middle, three quarters of it each
/// way, until the original's place is measured.
[[nodiscard]] auto consoleBounds(nx::rhi::Extent2d size) -> nx::ui::PanelBounds;

/// The console's window over a console: its font at a screen's height, the atlas of its characters, put
/// on the GPU with the screen's pictures, and the dialog it shows them in, painted while it is open. It
/// stays where it is made: its dialog points into it.
class ConsoleWindow {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The window of `console`, which must outlive it, for a screen `screenHeight` pixels tall, in
    /// kConsoleFont of `fonts`. Errors: those of WindowFonts::font, and Unusable (the atlas of its
    /// characters cannot be had): the console then has no window.
    [[nodiscard]] static auto open(const WindowFonts& fonts, nx::console::Console& console,
                                   std::uint32_t screenHeight)
        -> nx::Result<std::unique_ptr<ConsoleWindow>, WindowFontError>;

    /// Use open(). Public only so that open() can use std::make_unique.
    ConsoleWindow(CreateToken token, nx::console::Console& console, nx::ui::SchemeFontSet font,
                  nx::ui::GlyphAtlas atlas) noexcept;

    ConsoleWindow(const ConsoleWindow&) = delete;
    auto operator=(const ConsoleWindow&) -> ConsoleWindow& = delete;
    ConsoleWindow(ConsoleWindow&&) = delete;
    auto operator=(ConsoleWindow&&) -> ConsoleWindow& = delete;
    ~ConsoleWindow() = default;

    /// Puts the atlas on the GPU with `screen`, its uploads recorded in `commands`, and makes the
    /// dialog where consoleBounds puts it on a screen of `size`. Errors: those of
    /// nx::render::ScreenRenderer::loadImage.
    [[nodiscard]] auto load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                            nx::rhi::Extent2d size) -> nx::Result<void, nx::render::ScreenError>;
    /// The dialog, once loaded: the console's output too; none before.
    [[nodiscard]] inline auto dialog() noexcept -> nx::ui::ConsoleDialog* {
        return m_dialog ? &*m_dialog : nullptr;
    }

    [[nodiscard]] inline auto isOpen() const noexcept -> bool { return m_isOpen; }
    /// Opens the window, or closes it; returns whether it is open now.
    inline auto toggle() noexcept -> bool { return m_isOpen = !m_isOpen; }

    /// Paints the dialog, loaded and open, into `quads`, where consoleBounds puts it on a screen of
    /// `size`.
    void paint(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size);

private:
    nx::console::Console* m_console;
    nx::ui::SchemeFontSet m_font;
    nx::ui::GlyphAtlas m_atlas;
    std::optional<nx::render::ScreenImage> m_image; ///< The atlas on the GPU, once loaded.
    std::optional<nx::ui::ConsoleDialog> m_dialog;  ///< Made as it is loaded.
    bool m_isOpen = false;
};

} // namespace cs
