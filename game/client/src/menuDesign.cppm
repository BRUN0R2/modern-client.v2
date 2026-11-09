// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the project's own menus share of their look (docs/12-ui-hud-and-console.md, section 4): a design
/// 1080 lines tall made as large as the screen, the sizes of text made of the project's fonts for it, and
/// the colors: warm white text and gold over dark glass.
export module cs.client:menuDesign;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;

export namespace cs {

/// How many lines tall the screen is that the menus are designed on.
inline constexpr float kMenuDesignHeight = 1080.0F;

/// Why a menu cannot be drawn.
enum class MenuPanelError : std::uint8_t {
    NoFont,    ///< A family of the look's is among no fonts; the detail names it.
    Unusable,  ///< A face does not open at its size, or its glyphs cannot be drawn (cause).
    GpuFailed, ///< The GPU refused a picture or an atlas (cause).
};

/// The menus' design on a screen: how many of its pixels each unit of the design takes.
class MenuScale {
public:
    /// The design on a screen `screenHeight` pixels tall.
    explicit MenuScale(std::uint32_t screenHeight) noexcept;

    /// `units` of the design's on this screen, to the nearest pixel, at least one of some.
    [[nodiscard]] auto pixels(std::int32_t units) const noexcept -> nx::Pixels;

private:
    float scale = 1.0F; ///< Pixels of the screen for each unit of the design.
};

/// A size of text: its line in the design's units, and whether it stands out.
struct MenuFontSize {
    float units = 0.0F;
    bool isStrong = false;
};

/// A size of text on the GPU.
struct MenuFace {
    nx::ui::GlyphAtlas atlas;
    nx::render::ScreenImage image;

    /// What a painter writes with.
    [[nodiscard]] inline auto textFace() const noexcept -> nx::ui::TextFace {
        return {.atlas = &atlas, .image = &image};
    }
};

/// Makes the text of `size` for a screen `screenHeight` pixels tall, of the family of `families` it asks
/// for among `fonts`, and puts its atlas, labelled `label`, on the GPU with `screen`, the upload recorded in
/// `commands`. Errors: every MenuPanelError.
[[nodiscard]] auto makeMenuFace(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                                nx::rhi::CommandList& commands, const MenuFontFamilies& families,
                                MenuFontSize size, std::uint32_t screenHeight, std::string_view label)
    -> nx::Result<MenuFace, MenuPanelError>;

// The colors: warm white text, dark glass over the scene, and the gold that stands out.
inline constexpr nx::render::ScreenColor kMenuInk{.red = 244, .green = 240, .blue = 231};
inline constexpr nx::render::ScreenColor kMenuInkSoft{.red = 244, .green = 240, .blue = 231, .alpha = 168};
inline constexpr nx::render::ScreenColor kMenuInkFaint{.red = 244, .green = 240, .blue = 231, .alpha = 102};
inline constexpr nx::render::ScreenColor kMenuLine{.red = 255, .green = 255, .blue = 255, .alpha = 31};
inline constexpr nx::render::ScreenColor kMenuCell{.red = 255, .green = 255, .blue = 255, .alpha = 13};
inline constexpr nx::render::ScreenColor kMenuCellLit{.red = 255, .green = 255, .blue = 255, .alpha = 28};
/// What darkens the scene under a menu.
inline constexpr nx::render::ScreenColor kMenuBackdrop{.red = 6, .green = 7, .blue = 9, .alpha = 184};
inline constexpr nx::render::ScreenColor kMenuGold{.red = 242, .green = 179, .blue = 61};
/// What is written on the gold.
inline constexpr nx::render::ScreenColor kOnMenuGold{.red = 27, .green = 19, .blue = 5};

} // namespace cs
