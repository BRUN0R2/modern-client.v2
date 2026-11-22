// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the project's own menus share of their look (docs/12-ui-hud-and-console.md, section 4): a design
/// 1080 lines tall made as large as the screen, the sizes of text made of the project's fonts for it, and
/// the colors: warm text and brass accents over olive panels.
export module cs.client:menuDesign;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;

export namespace cs {

/// The unit of MenuUnits.
struct MenuUnitsTag {
    static constexpr std::string_view kSuffix = " units";
};

/// Lengths of the menus' design: a screen's height is kMenuDesignHeight of them, whatever its pixels.
using MenuUnits = nx::Quantity<std::int32_t, MenuUnitsTag>;

/// How many lines tall the screen is that the menus are designed on.
inline constexpr MenuUnits kMenuDesignHeight{1080};

/// Why a menu cannot be drawn.
enum class MenuPanelError : std::uint8_t {
    ImageUnavailable, ///< The project's menu image could not be read or decoded (cause).
    NoFont,           ///< A family of the look's is among no fonts; the detail names it.
    Unusable,         ///< A face does not open at its size, or its glyphs cannot be drawn (cause).
    GpuFailed,        ///< The GPU refused a picture or an atlas (cause).
};

/// The menus' design on a screen: how many of its pixels each unit of the design takes.
class MenuScale {
public:
    /// The design on a screen `screenHeight` pixels tall.
    explicit MenuScale(std::uint32_t screenHeight) noexcept;

    /// `units` of the design's on this screen, to the nearest pixel, at least one of some.
    [[nodiscard]] auto pixels(MenuUnits units) const noexcept -> nx::Pixels;

private:
    float scale = 1.0F; ///< Pixels of the screen for each unit of the design.
};

/// A size of text: its line, and whether it stands out.
struct MenuFontSize {
    MenuUnits line{};
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

/// How thick an outline and a rule of a menu are drawn.
inline constexpr nx::Pixels kMenuLineThick{1};

/// Draws with `painter` the edges of `rect`, kMenuLineThick thick, in `color`.
void paintMenuOutline(nx::ui::PanelPainter& painter, const nx::ui::PixelRect& rect,
                      nx::render::ScreenColor color);

// The colors: warm text, olive surfaces and brass accents.
inline constexpr nx::render::ScreenColor kMenuInk{.red = 245, .green = 242, .blue = 224};
inline constexpr nx::render::ScreenColor kMenuInkSoft{.red = 225, .green = 224, .blue = 204};
inline constexpr nx::render::ScreenColor kMenuInkFaint{.red = 174, .green = 180, .blue = 151};
inline constexpr nx::render::ScreenColor kMenuLine{.red = 112, .green = 122, .blue = 92};
inline constexpr nx::render::ScreenColor kMenuCell{.red = 193, .green = 208, .blue = 159, .alpha = 29};
inline constexpr nx::render::ScreenColor kMenuCellLit{.red = 183, .green = 202, .blue = 141, .alpha = 63};
/// What a panel of a menu is filled with, over the scene or the menu's ground.
inline constexpr nx::render::ScreenColor kMenuGlass{.red = 36, .green = 44, .blue = 30, .alpha = 232};
/// What darkens the scene under a menu.
inline constexpr nx::render::ScreenColor kMenuBackdrop{.red = 12, .green = 16, .blue = 10, .alpha = 168};
inline constexpr nx::render::ScreenColor kMenuGold{.red = 221, .green = 189, .blue = 118};
/// What is written on the gold.
inline constexpr nx::render::ScreenColor kOnMenuGold{.red = 33, .green = 30, .blue = 16};

} // namespace cs
