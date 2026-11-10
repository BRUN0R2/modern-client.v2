// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What draws the main menu (docs/12-ui-hud-and-console.md, section 4): what cs.client:mainMenu says it
/// shows, laid out by cs.client:mainMenuLayout in rectangles and text of the screen, the look the project's
/// own (cs.client:menuDesign).
export module cs.client:mainMenuPanel;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import :mainMenu;
import :menuDesign;

export namespace cs {

/// The sizes of text the menu writes in.
enum class MainMenuFont : std::uint8_t {
    Title,
    Item,
    Row,
    Hint,
    Count,
};

/// What draws the menu: its fonts made for the screen's height, put on the GPU with the screen's, and what
/// it last laid out.
class MainMenuPanel {
public:
    /// What draws the menu of `listed`, which must outlive it.
    explicit MainMenuPanel(const MainMenuLook& listed) noexcept;

    /// Whether load made its fonts for a screen `screenHeight` pixels tall.
    [[nodiscard]] inline auto isLoadedFor(std::uint32_t screenHeight) const noexcept -> bool {
        return facesHeight == screenHeight;
    }

    /// Puts on the GPU with `screen`, its uploads recorded in `commands`, the fonts for a screen
    /// `screenHeight` pixels tall, more than none, of the look's families among `fonts`. Errors: every
    /// MenuPanelError.
    [[nodiscard]] auto load(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                            nx::rhi::CommandList& commands, std::uint32_t screenHeight)
        -> nx::Result<void, MenuPanelError>;

    /// Appends to `quads` the menu showing `frame` on a screen of `size`, the one its fonts were loaded for:
    /// over the scene darkened while a map is played, else over the whole screen; laid out again only when
    /// what it shows changes.
    void append(std::vector<nx::render::ScreenQuad>& quads, const MainMenuFrame& frame,
                nx::rhi::Extent2d size);

private:
    /// What append laid out.
    struct Shown {
        MainMenuFrame frame{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };

    const MainMenuLook* look;
    std::uint32_t facesHeight = 0; ///< The screen's height the fonts are made for; 0 before they are.
    nx::EnumArray<MainMenuFont, std::optional<MenuFace>> faces;
    nx::render::RetainedQuads<Shown> shown;
};

} // namespace cs
