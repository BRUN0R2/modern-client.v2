// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

using nx::render::ScreenColor;
using TextFaces = nx::EnumArray<MainMenuFont, nx::ui::TextFace>;

/// What the menu covers the whole screen with while no map is played.
constexpr ScreenColor kGround{.red = 13, .green = 14, .blue = 16};

constexpr nx::EnumArray<MainMenuFont, MenuFontSize> kFontSizes{
    nx::EnumArray<MainMenuFont, MenuFontSize>::Storage{
        MenuFontSize{.units = 84.0F, .isStrong = true}, MenuFontSize{.units = 34.0F, .isStrong = true},
        MenuFontSize{.units = 26.0F, .isStrong = false}, MenuFontSize{.units = 18.0F, .isStrong = false}}};

// What it writes.
constexpr std::string_view kGameName = "COUNTER-STRIKE";
constexpr nx::EnumArray<MainMenuAction, std::string_view> kItemNames{
    nx::EnumArray<MainMenuAction, std::string_view>::Storage{"RESUME GAME", "DISCONNECT", "NEW GAME",
                                                             "QUIT"}};
constexpr std::string_view kMapsName = "MAP";
constexpr std::string_view kNoMaps = "The game has no maps.";
constexpr std::string_view kStart = "START";
constexpr std::string_view kBack = "BACK";
/// A key and what it does, as the menu's foot tells them.
struct Hint {
    std::string_view key{};
    std::string_view what{};
};
constexpr Hint kSelectHint{.key = "UP / DOWN", .what = "Select"};
constexpr Hint kChooseHint{.key = "ENTER", .what = "Choose"};
constexpr Hint kResumeHint{.key = "ESC", .what = "Resume"};
constexpr Hint kStartHint{.key = "ENTER", .what = "Start"};
constexpr Hint kBackHint{.key = "ESC", .what = "Back"};

// In the design's units: what the layout leaves to what draws in it.
constexpr std::int32_t kRuleGap = 14;
constexpr std::int32_t kRuleWide = 72;
constexpr std::int32_t kRuleTall = 4;
constexpr std::int32_t kAccentBar = 4;
constexpr std::int32_t kItemInset = 24;
constexpr std::int32_t kHintKeyGap = 8;
constexpr std::int32_t kHintGap = 28;
constexpr std::int32_t kRowInset = 16;
constexpr std::int32_t kMapsNameGap = 6; ///< From the name of the list to its first row.
/// What the room left is divided by to put a thing halfway into it.
constexpr std::int32_t kMiddleDivisor = 2;

/// `text` in `face` in the middle of `rect`, in `color`.
void writeInMiddle(nx::ui::PanelPainter& painter, const nx::ui::TextFace& face, std::string_view text,
                   const nx::ui::PixelRect& rect, ScreenColor color) {
    std::ignore =
        painter.text(face, text,
                     {.x = rect.x + ((rect.wide - nx::ui::widthOf(*face.atlas, text)) / kMiddleDivisor),
                      .y = rect.y + ((rect.tall - face.atlas->lineHeight()) / kMiddleDivisor)},
                     color);
}

/// Draws the page of a new game showing `page` by `layout` with `painter`, in `faces`.
void paintNewGame(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces,
                  const NewGamePage& page) {
    painter.fill(layout.page(), kMenuGlass);
    paintMenuOutline(painter, layout.page(), kMenuLine);
    std::ignore = painter.text(faces[MainMenuFont::Item], kItemNames[MainMenuAction::NewGame],
                               layout.pageTitle(), kMenuInk);

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    const nx::ui::TextFace& rowFace = faces[MainMenuFont::Row];
    const nx::ui::PixelRect top = layout.listRow(0);
    std::ignore = painter.text(
        hintFace, kMapsName,
        {.x = top.x, .y = top.y - layout.pixels(kMapsNameGap) - hintFace.atlas->lineHeight()}, kMenuInkFaint);
    const auto rowText = [&](const nx::ui::PixelRect& rect, std::string_view text, ScreenColor color) {
        std::ignore =
            painter.text(rowFace, text,
                         {.x = rect.x + layout.pixels(kRowInset),
                          .y = rect.y + ((rect.tall - rowFace.atlas->lineHeight()) / kMiddleDivisor)},
                         color);
    };
    if (page.rows.empty()) {
        rowText(top, kNoMaps, kMenuInkFaint);
    }
    for (const auto& [index, name] : std::views::enumerate(page.rows)) {
        const auto row = nx::narrow<std::size_t>(index);
        const nx::ui::PixelRect rect = layout.listRow(row);
        const bool isSelected = page.selectedRow == row;
        if (isSelected) {
            painter.fill(rect, kMenuCellLit);
            painter.fill({.x = rect.x, .y = rect.y, .wide = layout.pixels(kAccentBar), .tall = rect.tall},
                         kMenuGold);
        }
        rowText(rect, name, isSelected ? kMenuInk : kMenuInkSoft);
    }
    if (const std::optional<nx::ui::PixelRect> thumb = layout.listThumb(page.first, page.count)) {
        painter.fill(*thumb, kMenuInkFaint);
    }

    paintMenuOutline(painter, layout.backButton(), kMenuLine);
    writeInMiddle(painter, rowFace, kBack, layout.backButton(), kMenuInkSoft);
    const bool canStart = !page.rows.empty();
    painter.fill(layout.confirmButton(), canStart ? kMenuGold : kMenuCell);
    writeInMiddle(painter, rowFace, kStart, layout.confirmButton(), canStart ? kOnMenuGold : kMenuInkFaint);
}

/// Draws the menu showing `frame` by `layout` with `painter`, in `faces`.
void paintMenu(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces,
               const MainMenuFrame& frame) {
    painter.fill(layout.screen(), frame.game == MenuGame::Played ? kMenuBackdrop : kGround);

    const nx::ui::TextFace& titleFace = faces[MainMenuFont::Title];
    const nx::ui::PixelPoint& title = layout.title();
    std::ignore = painter.text(titleFace, kGameName, title, kMenuInk);
    painter.fill({.x = title.x,
                  .y = title.y + titleFace.atlas->lineHeight() + layout.pixels(kRuleGap),
                  .wide = layout.pixels(kRuleWide),
                  .tall = layout.pixels(kRuleTall)},
                 kMenuGold);

    const nx::ui::TextFace& itemFace = faces[MainMenuFont::Item];
    for (const auto& [index, action] : std::views::enumerate(frame.items)) {
        const nx::ui::PixelRect rect = layout.item(nx::narrow<std::size_t>(index));
        const bool isSelected = frame.selected == action;
        if (isSelected) {
            painter.fill(rect, kMenuCellLit);
            painter.fill({.x = rect.x, .y = rect.y, .wide = layout.pixels(kAccentBar), .tall = rect.tall},
                         kMenuGold);
        }
        std::ignore =
            painter.text(itemFace, kItemNames[action],
                         {.x = rect.x + layout.pixels(kItemInset),
                          .y = rect.y + ((rect.tall - itemFace.atlas->lineHeight()) / kMiddleDivisor)},
                         isSelected ? kMenuInk : kMenuInkSoft);
    }

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    nx::ui::PixelPoint pen = layout.hints();
    const auto tell = [&](const Hint& hint) {
        pen.x = painter.text(hintFace, hint.key, pen, kMenuInkSoft) + layout.pixels(kHintKeyGap);
        pen.x = painter.text(hintFace, hint.what, pen, kMenuInkFaint) + layout.pixels(kHintGap);
    };
    tell(kSelectHint);
    if (frame.newGame) {
        tell(kStartHint);
        tell(kBackHint);
        paintNewGame(painter, layout, faces, *frame.newGame);
        return;
    }
    tell(kChooseHint);
    if (std::ranges::contains(frame.items, MainMenuAction::ResumeGame)) {
        tell(kResumeHint);
    }
}

} // namespace

MainMenuPanel::MainMenuPanel(const MainMenuLook& listed) noexcept : look{&listed} {}

auto MainMenuPanel::load(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                         nx::rhi::CommandList& commands, std::uint32_t screenHeight)
    -> nx::Result<void, MenuPanelError> {
    nx::expects(screenHeight > 0, "MainMenuPanel::load: a screen of some height");
    for (const MainMenuFont font : TextFaces::keys()) {
        auto face =
            makeMenuFace(fonts, screen, commands, look->font, kFontSizes[font], screenHeight, "mainMenu");
        if (!face) {
            return std::unexpected{std::move(face.error())};
        }
        faces[font].emplace(std::move(*face));
    }
    facesHeight = screenHeight;
    return {};
}

void MainMenuPanel::append(std::vector<nx::render::ScreenQuad>& quads, const MainMenuFrame& frame,
                           nx::rhi::Extent2d size) {
    nx::expects(isLoadedFor(size.height), "MainMenuPanel::append: its fonts loaded for the screen");
    shown.append(quads, Shown{.frame = frame, .size = size},
                 [&](std::vector<nx::render::ScreenQuad>& laidOut) {
                     TextFaces textFaces;
                     for (const MainMenuFont font : TextFaces::keys()) {
                         textFaces[font] = faces[font]->textFace();
                     }
                     nx::ui::PanelPainter painter{laidOut, size};
                     paintMenu(painter, MainMenuLayout{size, frame.items.size()}, textFaces, frame);
                 });
}

} // namespace cs
