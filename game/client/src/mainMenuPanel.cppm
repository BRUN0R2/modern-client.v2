// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What draws the main menu (docs/12-ui-hud-and-console.md, section 4): what cs.client:mainMenu says it
/// shows, laid out by cs.client:mainMenuLayout in rectangles and text of the screen, the look the project's
/// own (cs.client:menuDesign).
export module cs.client:mainMenuPanel;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
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

/// What draws the menu: its fonts made for the screen's height and the picture behind it, put on the GPU
/// with the screen's, and what it last laid out.
class MainMenuPanel {
public:
    /// What draws the menu of `listed`, writing those of `written`, the game's texts in the player's
    /// language; both must outlive it.
    MainMenuPanel(const MainMenuLook& listed, const Texts& written) noexcept;

    /// Whether load made its fonts for a screen `screenHeight` pixels tall.
    [[nodiscard]] inline auto isLoadedFor(std::uint32_t screenHeight) const noexcept -> bool {
        return facesHeight == screenHeight;
    }

    /// Puts on the GPU with `screen`, its uploads recorded in `commands`, the fonts for a screen
    /// `screenHeight` pixels tall, more than none, of the look's families among `fonts`; and, the first
    /// time, the picture behind the menu, read from `vfs` (readMenuBackground): one that cannot be read is
    /// logged, and the menu shows without it, as in a content that has none. Errors: every MenuPanelError.
    [[nodiscard]] auto load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                            nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                            std::uint32_t screenHeight) -> nx::Result<void, MenuPanelError>;

    /// Appends to `quads` the menu showing `frame` on a screen of `size`, the one its fonts were loaded for:
    /// over the scene darkened while a map is played, else over its background, covering the whole screen
    /// and darkened too; laid out again only when what it shows changes.
    void append(std::vector<nx::render::ScreenQuad>& quads, const MainMenuFrame& frame,
                nx::rhi::Extent2d size);

    /// Appends to `quads` what tells that the map `map` is loading, on a screen of `size`, the one its fonts
    /// were loaded for: over the menu's background, covering the whole screen, the game's name where the
    /// menu writes it and the map's where its items stand.
    void appendLoading(std::vector<nx::render::ScreenQuad>& quads, std::string_view map,
                       nx::rhi::Extent2d size);

private:
    /// What appendLoading laid out.
    struct ShownLoading {
        std::string map{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const ShownLoading&) const noexcept -> bool = default;
    };
    /// What append laid out.
    struct Shown {
        MainMenuFrame frame{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };

    const MainMenuLook* look;
    const Texts* texts;
    std::uint32_t facesHeight = 0; ///< The screen's height the fonts are made for; 0 before they are.
    nx::EnumArray<MainMenuFont, std::optional<MenuFace>> faces;
    bool isBackgroundRead = false; ///< Whether load looked for the picture behind the menu.
    std::optional<nx::render::ScreenImage> background; ///< None for one that could not be read.
    nx::render::RetainedQuads<Shown> shown;
    nx::render::RetainedQuads<ShownLoading> shownLoading;

    /// The faces load made, as a painter writes with them.
    [[nodiscard]] auto textFaces() const noexcept -> nx::EnumArray<MainMenuFont, nx::ui::TextFace>;
    /// Appends to `quads` what the menu stands over while no map is played, on a screen of `size`: its
    /// ground, and over it its background, covering the screen and darkened, when it has one.
    void appendGround(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) const;
};

} // namespace cs
