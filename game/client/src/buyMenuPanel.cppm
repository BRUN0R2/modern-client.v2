// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What draws the buy menu (docs/12-ui-hud-and-console.md, section 4): what cs.client:buyMenu says it shows,
/// laid out by cs.client:buyMenuLayout in rectangles, pictures and text of the screen, the look the project's
/// own; and what is told of a refusal over the game, or over the menu while it is open.
export module cs.client:buyMenuPanel;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;
import nx.ui;
import cs.shared;
import :buyMenu;

export namespace cs {

/// Why the buy menu cannot be drawn.
enum class BuyMenuPanelError : std::uint8_t {
    NoFont,    ///< No family of the look's is among the system's fonts; the detail names them.
    Unusable,  ///< A face does not open at its size, or its glyphs cannot be drawn (cause).
    GpuFailed, ///< The GPU refused a picture or an atlas (cause).
};

/// The sizes of text the menu writes in.
enum class BuyMenuFont : std::uint8_t {
    Headline,
    Title,
    Strong,
    Body,
    Caption,
    Count,
};

/// `money` as the menu writes it: "$2,500".
[[nodiscard]] auto dollarsText(nx::Money money) -> std::string;
/// `seconds` as the menu writes them: "1:05".
[[nodiscard]] auto clockText(std::uint32_t seconds) -> std::string;
/// The share of a hit `share` lets through as the menu writes it: "77.5%".
[[nodiscard]] auto shareText(float share) -> std::string;

/// What draws the menu: its fonts made for the screen's height and its pictures, put on the GPU with the
/// screen's, and what it last laid out.
class BuyMenuPanel {
public:
    /// What draws the menu of `sold`, which must outlive it.
    explicit BuyMenuPanel(const BuyMenuContent& sold) noexcept;

    /// Whether load made its fonts.
    [[nodiscard]] inline auto isLoaded() const noexcept -> bool { return facesHeight != 0; }

    /// Puts on the GPU with `screen`, its uploads recorded in `commands`, what is not there yet: the fonts
    /// for a screen `screenHeight` pixels tall, in the first of the look's families among `fonts`, made again
    /// as that height changes; and the pictures of what is sold, read from `vfs`, a weapon made as the game
    /// runs too. A picture that cannot be read is logged, and what it shows drawn without it. Errors: every
    /// BuyMenuPanelError.
    [[nodiscard]] auto load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                            nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                            std::uint32_t screenHeight) -> nx::Result<void, BuyMenuPanelError>;

    /// Tells `text` from `now`, for a moment.
    void showNotice(std::string_view text, nx::Seconds now);
    /// Forgets what it tells, as a map ends.
    inline void clearNotice() noexcept { notice.reset(); }

    /// Appends to `quads` what darkens the scene under the menu and the HUD, on a screen of `size`.
    static void appendBackdrop(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size);
    /// Appends to `quads` the menu showing `frame` on a screen of `size`, once loaded: laid out again only
    /// when what it shows changes, but for its line of the time to buy, which shows `timeShare`
    /// (buyTimeShareOf).
    void append(std::vector<nx::render::ScreenQuad>& quads, const BuyMenuFrame& frame, float timeShare,
                nx::rhi::Extent2d size);
    /// Appends to `quads` what it tells at `now`, over the menu while `isMenuOpen`, else over the game; the
    /// moment over, it tells nothing more.
    void appendNotice(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size, nx::Seconds now,
                      bool isMenuOpen);

private:
    /// A size of text on the GPU.
    struct Face {
        nx::ui::GlyphAtlas atlas;
        nx::render::ScreenImage image;
    };
    /// What append laid out.
    struct Shown {
        BuyMenuFrame frame{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };
    /// What is told, since when.
    struct Notice {
        std::string text{};
        nx::Seconds shownAt{};
    };

    [[nodiscard]] auto makeFaces(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                                 nx::rhi::CommandList& commands, std::uint32_t screenHeight)
        -> nx::Result<void, BuyMenuPanelError>;
    [[nodiscard]] auto loadPicture(std::string_view path, const nx::assets::Vfs& vfs,
                                   nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, BuyMenuPanelError>;
    [[nodiscard]] auto textFaces() const noexcept -> nx::EnumArray<BuyMenuFont, nx::ui::TextFace>;

    BuyMenuContent content;
    std::uint32_t facesHeight = 0; ///< The screen's height the fonts are made for; 0 before they are.
    nx::EnumArray<BuyMenuFont, std::optional<Face>> faces;
    /// By their files; none for one that cannot be read, which is not read again.
    std::map<std::string, std::optional<nx::render::ScreenImage>, std::less<>> pictures;
    nx::render::RetainedQuads<Shown> shown;
    std::optional<Notice> notice;
};

} // namespace cs
