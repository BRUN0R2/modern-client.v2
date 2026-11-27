// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What draws the team and appearance menus (docs/12-ui-hud-and-console.md, section 4): what
/// cs.client:teamMenu says they show, laid out by its TeamMenuLayout in the buy menu's look, its fonts and
/// each team's accent, the original's pictures of the characters (gfx/vgui/) among the game's files.
export module cs.client:teamMenuPanel;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;
import nx.ui;
import cs.shared;
import :menuDesign;
import :teamMenu;

export namespace cs {

/// The sizes of text the menu writes in.
enum class TeamMenuFont : std::uint8_t {
    Headline,
    Title,
    Caption,
    Count,
};

class TeamMenuPanel {
public:
    /// What draws the menu in the families of `look`, which must outlive it.
    explicit TeamMenuPanel(const BuyMenuLook& look) noexcept;

    /// Whether load made its fonts for a screen `screenHeight` pixels tall.
    [[nodiscard]] inline auto isLoadedFor(std::uint32_t screenHeight) const noexcept -> bool {
        return facesHeight == screenHeight;
    }

    /// Puts on the GPU with `screen`, its uploads recorded in `commands`, the fonts for a screen
    /// `screenHeight` pixels tall, of the look's families among `fonts`, and the pictures of `characters` and
    /// auto-select, read from `vfs` the first time; one that cannot be read is logged, and its row drawn
    /// without it. Errors: every MenuPanelError.
    [[nodiscard]] auto load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                            nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                            const Characters& characters, std::uint32_t screenHeight)
        -> nx::Result<void, MenuPanelError>;

    /// Appends to `quads` the menu showing `frame` on a screen of `size`, once loaded for its height, over
    /// the scene darkened: laid out again only when what it shows changes.
    void append(std::vector<nx::render::ScreenQuad>& quads, const TeamMenuFrame& frame,
                nx::rhi::Extent2d size);

private:
    struct Shown {
        TeamMenuFrame frame{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };

    [[nodiscard]] auto loadPicture(std::string_view path, const nx::assets::Vfs& vfs,
                                   nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, MenuPanelError>;

    const BuyMenuLook* look;
    std::uint32_t facesHeight = 0; ///< The screen's height the fonts are made for; 0 before they are.
    nx::EnumArray<TeamMenuFont, std::optional<MenuFace>> faces;
    /// By their files; none for one that cannot be read, which is not read again.
    std::map<std::string, std::optional<nx::render::ScreenImage>, std::less<>> pictures;
    nx::render::RetainedQuads<Shown> shown;
};

} // namespace cs
