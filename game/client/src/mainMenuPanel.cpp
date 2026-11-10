// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

using nx::render::ScreenColor;
using TextFaces = nx::EnumArray<MainMenuFont, nx::ui::TextFace>;

constexpr nx::log::Category kLog{"client"};

/// What the menu covers the whole screen with while no map is played, under its background.
constexpr ScreenColor kGround{.red = 13, .green = 14, .blue = 16};
/// What darkens the background, so that what the menu writes over it reads.
constexpr ScreenColor kBackgroundVeil{.red = 6, .green = 7, .blue = 9, .alpha = 150};

constexpr nx::EnumArray<MainMenuFont, MenuFontSize> kFontSizes{
    nx::EnumArray<MainMenuFont, MenuFontSize>::Storage{
        MenuFontSize{.units = 84.0F, .isStrong = true}, MenuFontSize{.units = 34.0F, .isStrong = true},
        MenuFontSize{.units = 26.0F, .isStrong = false}, MenuFontSize{.units = 18.0F, .isStrong = false}}};

/// The game's name, the same in every language.
constexpr std::string_view kGameName = "COUNTER-STRIKE";
/// The text of the menu's that names each item.
constexpr nx::EnumArray<MainMenuAction, std::string MainMenuTexts::*> kItemTexts{
    nx::EnumArray<MainMenuAction, std::string MainMenuTexts::*>::Storage{
        &MainMenuTexts::resumeGame, &MainMenuTexts::disconnect, &MainMenuTexts::newGame,
        &MainMenuTexts::options, &MainMenuTexts::quit}};

/// The text of the options' that names each tab.
constexpr nx::EnumArray<OptionCategory, std::string OptionsTexts::*> kTabTexts{
    nx::EnumArray<OptionCategory, std::string OptionsTexts::*>::Storage{
        &OptionsTexts::mouse, &OptionsTexts::keyboard, &OptionsTexts::audio, &OptionsTexts::video,
        &OptionsTexts::game}};

/// What the menu calls the item that does `action`, in the language of `texts`.
[[nodiscard]] auto itemNameOf(const MainMenuTexts& texts, MainMenuAction action) noexcept
    -> std::string_view {
    return texts.*kItemTexts[action];
}

// In the design's units: what the layout leaves to what draws in it.
constexpr std::int32_t kRuleGap = 14;
constexpr std::int32_t kRuleWide = 72;
constexpr std::int32_t kRuleTall = 4;
constexpr std::int32_t kAccentBar = 4;
constexpr std::int32_t kItemInset = 24;
constexpr std::int32_t kHintKeyGap = 8;
constexpr std::int32_t kHintGap = 28;
constexpr std::int32_t kRowInset = 16;
constexpr std::int32_t kTabRule = 2;     ///< Under the tab shown.
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

/// `text` in `face`, ending at `end`, halfway down `rect`, in `color`.
void writeEnding(nx::ui::PanelPainter& painter, const nx::ui::TextFace& face, std::string_view text,
                 nx::Pixels end, const nx::ui::PixelRect& rect, ScreenColor color) {
    std::ignore = painter.text(face, text,
                               {.x = end - nx::ui::widthOf(*face.atlas, text),
                                .y = rect.y + ((rect.tall - face.atlas->lineHeight()) / kMiddleDivisor)},
                               color);
}

/// Draws the options showing `shown` by `layout` with `painter`, in `faces`, writing `texts`.
void paintOptions(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces,
                  const Texts& texts, const OptionsFrame& shown) {
    painter.fill(layout.page(), kMenuGlass);
    paintMenuOutline(painter, layout.page(), kMenuLine);
    std::ignore =
        painter.text(faces[MainMenuFont::Item], texts.mainMenu.options, layout.pageTitle(), kMenuInk);

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    const nx::ui::TextFace& rowFace = faces[MainMenuFont::Row];
    for (const OptionCategory tab : nx::EnumArray<OptionCategory, bool>::keys()) {
        const nx::ui::PixelRect rect = layout.optionTab(static_cast<std::size_t>(std::to_underlying(tab)));
        const bool isShown = tab == shown.category;
        painter.fill(rect, isShown ? kMenuCellLit : kMenuCell);
        if (isShown) {
            const nx::Pixels rule = layout.pixels(kTabRule);
            painter.fill({.x = rect.x, .y = rect.y + rect.tall - rule, .wide = rect.wide, .tall = rule},
                         kMenuGold);
        }
        // The tabs take the keys while no row is selected.
        writeInMiddle(painter, hintFace, texts.options.*kTabTexts[tab], rect,
                      isShown && !shown.selectedRow ? kMenuInk : kMenuInkSoft);
    }

    for (const auto& [index, option] : std::views::enumerate(shown.rows)) {
        const auto row = nx::narrow<std::size_t>(index);
        const nx::ui::PixelRect rect = layout.listRow(row);
        const bool isSelected = shown.selectedRow == row;
        if (isSelected) {
            painter.fill(rect, kMenuCellLit);
            painter.fill({.x = rect.x, .y = rect.y, .wide = layout.pixels(kAccentBar), .tall = rect.tall},
                         kMenuGold);
        }
        std::ignore =
            painter.text(rowFace, texts.options.*option.name,
                         {.x = rect.x + layout.pixels(kRowInset),
                          .y = rect.y + ((rect.tall - rowFace.atlas->lineHeight()) / kMiddleDivisor)},
                         isSelected ? kMenuInk : kMenuInkSoft);
        const nx::ui::PixelRect control = layout.optionControl(row);
        const nx::Pixels end = control.x + control.wide;
        if (const auto* const slider = std::get_if<ShownSlider>(&option.control)) {
            const nx::ui::PixelRect track = layout.optionTrack(row);
            painter.fill(track, kMenuLine);
            painter.fill({.x = track.x,
                          .y = track.y,
                          .wide = nx::Pixels{static_cast<std::int32_t>(
                              std::lround(static_cast<float>(track.wide.value()) * slider->share))},
                          .tall = track.tall},
                         kMenuGold);
            writeEnding(painter, rowFace, sliderText(*slider), end, control, kMenuInk);
        } else if (const auto* const keys = std::get_if<ShownKeys>(&option.control)) {
            // Waited for, it asks for its key; else its keys, or that it has none.
            const std::string_view written = keys->isWaited       ? std::string_view{texts.options.pressAKey}
                                             : keys->keys.empty() ? std::string_view{texts.options.noKey}
                                                                  : std::string_view{keys->keys};
            writeEnding(painter, rowFace, written, end, control,
                        keys->isWaited       ? kMenuGold
                        : keys->keys.empty() ? kMenuInkFaint
                                             : kMenuInk);
        } else {
            const bool isOn = std::get<ShownSwitch>(option.control).isOn;
            writeEnding(painter, rowFace, isOn ? texts.options.on : texts.options.off, end, control,
                        isOn ? kMenuGold : kMenuInkFaint);
        }
    }
    if (const std::optional<nx::ui::PixelRect> thumb = layout.listThumb(shown.first, shown.count)) {
        painter.fill(*thumb, kMenuInkFaint);
    }

    paintMenuOutline(painter, layout.backButton(), kMenuLine);
    writeInMiddle(painter, rowFace, texts.mainMenu.back, layout.backButton(), kMenuInkSoft);
}

/// Draws the page of a new game showing `page` by `layout` with `painter`, in `faces`, writing `texts`.
void paintNewGame(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces,
                  const MainMenuTexts& texts, const NewGamePage& page) {
    painter.fill(layout.page(), kMenuGlass);
    paintMenuOutline(painter, layout.page(), kMenuLine);
    std::ignore = painter.text(faces[MainMenuFont::Item], texts.newGame, layout.pageTitle(), kMenuInk);

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    const nx::ui::TextFace& rowFace = faces[MainMenuFont::Row];
    const nx::ui::PixelRect top = layout.listRow(0);
    std::ignore = painter.text(
        hintFace, texts.map,
        {.x = top.x, .y = top.y - layout.pixels(kMapsNameGap) - hintFace.atlas->lineHeight()}, kMenuInkFaint);
    const auto rowText = [&](const nx::ui::PixelRect& rect, std::string_view text, ScreenColor color) {
        std::ignore =
            painter.text(rowFace, text,
                         {.x = rect.x + layout.pixels(kRowInset),
                          .y = rect.y + ((rect.tall - rowFace.atlas->lineHeight()) / kMiddleDivisor)},
                         color);
    };
    if (page.rows.empty()) {
        rowText(top, texts.noMaps, kMenuInkFaint);
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
    writeInMiddle(painter, rowFace, texts.back, layout.backButton(), kMenuInkSoft);
    const bool canStart = !page.rows.empty();
    painter.fill(layout.confirmButton(), canStart ? kMenuGold : kMenuCell);
    writeInMiddle(painter, rowFace, texts.start, layout.confirmButton(),
                  canStart ? kOnMenuGold : kMenuInkFaint);
}

/// Writes the game's name where `layout` puts it, over its rule.
void paintGameName(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces) {
    const nx::ui::TextFace& titleFace = faces[MainMenuFont::Title];
    const nx::ui::PixelPoint& title = layout.title();
    std::ignore = painter.text(titleFace, kGameName, title, kMenuInk);
    painter.fill({.x = title.x,
                  .y = title.y + titleFace.atlas->lineHeight() + layout.pixels(kRuleGap),
                  .wide = layout.pixels(kRuleWide),
                  .tall = layout.pixels(kRuleTall)},
                 kMenuGold);
}

/// A row of the items' column in `rect`: `text` in `face`, lit with the gold bar on its left when `isLit`.
void paintItemRow(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const nx::ui::TextFace& face,
                  const nx::ui::PixelRect& rect, std::string_view text, bool isLit) {
    if (isLit) {
        painter.fill(rect, kMenuCellLit);
        painter.fill({.x = rect.x, .y = rect.y, .wide = layout.pixels(kAccentBar), .tall = rect.tall},
                     kMenuGold);
    }
    std::ignore = painter.text(face, text,
                               {.x = rect.x + layout.pixels(kItemInset),
                                .y = rect.y + ((rect.tall - face.atlas->lineHeight()) / kMiddleDivisor)},
                               isLit ? kMenuInk : kMenuInkSoft);
}

/// Draws the menu showing `frame` by `layout` with `painter`, in `faces`, writing those of `written`.
void paintMenu(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces,
               const Texts& written, const MainMenuFrame& frame) {
    const MainMenuTexts& texts = written.mainMenu;
    paintGameName(painter, layout, faces);
    for (const auto& [index, action] : std::views::enumerate(frame.items)) {
        paintItemRow(painter, layout, faces[MainMenuFont::Item], layout.item(nx::narrow<std::size_t>(index)),
                     itemNameOf(texts, action), frame.selected == action);
    }

    // A key and what it does, one after the other along the menu's foot.
    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    nx::ui::PixelPoint pen = layout.hints();
    const auto tell = [&](std::string_view key, std::string_view what) {
        pen.x = painter.text(hintFace, key, pen, kMenuInkSoft) + layout.pixels(kHintKeyGap);
        pen.x = painter.text(hintFace, what, pen, kMenuInkFaint) + layout.pixels(kHintGap);
    };
    tell(texts.keyUpDown, texts.select);
    if (frame.options) {
        if (frame.options->category == OptionCategory::Keyboard) {
            tell(texts.keyEnter, texts.change);
            tell(texts.keyDelete, texts.clear);
        } else {
            tell(texts.keyLeftRight, texts.change);
        }
        tell(texts.keyEscape, texts.goBack);
        paintOptions(painter, layout, faces, written, *frame.options);
        return;
    }
    if (frame.newGame) {
        tell(texts.keyEnter, texts.play);
        tell(texts.keyEscape, texts.goBack);
        paintNewGame(painter, layout, faces, texts, *frame.newGame);
        return;
    }
    tell(texts.keyEnter, texts.choose);
    if (std::ranges::contains(frame.items, MainMenuAction::ResumeGame)) {
        tell(texts.keyEscape, texts.resume);
    }
}

} // namespace

MainMenuPanel::MainMenuPanel(const MainMenuLook& listed, const Texts& written) noexcept
    : look{&listed}, texts{&written} {}

auto MainMenuPanel::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                         nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                         std::uint32_t screenHeight) -> nx::Result<void, MenuPanelError> {
    nx::expects(screenHeight > 0, "MainMenuPanel::load: a screen of some height");
    if (!std::exchange(isBackgroundRead, true)) {
        if (const auto picture = readMenuBackground(vfs, look->background); !picture) {
            nx::log::warning(kLog, "the main menu shows no background: {}", nx::describe(picture.error()));
        } else {
            auto loaded = screen.loadImage(commands, picture->pixels, picture->width, picture->height,
                                           look->background);
            if (!loaded) {
                return nx::fail(MenuPanelError::GpuFailed, look->background, std::move(loaded.error()));
            }
            background.emplace(std::move(*loaded));
        }
    }
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
                     const MainMenuLayout layout{size, frame.items.size()};
                     if (frame.game == MenuGame::Played) {
                         nx::ui::PanelPainter{laidOut, size}.fill(layout.screen(), kMenuBackdrop);
                     } else {
                         appendGround(laidOut, size);
                     }
                     nx::ui::PanelPainter painter{laidOut, size};
                     paintMenu(painter, layout, textFaces(), *texts, frame);
                 });
}

void MainMenuPanel::appendLoading(std::vector<nx::render::ScreenQuad>& quads, std::string_view map,
                                  nx::rhi::Extent2d size) {
    nx::expects(isLoadedFor(size.height), "MainMenuPanel::appendLoading: its fonts loaded for the screen");
    shownLoading.append(quads, ShownLoading{.map = std::string{map}, .size = size},
                        [&](std::vector<nx::render::ScreenQuad>& laidOut) {
                            // Two rows where the items stand: what is going on, and the map.
                            constexpr std::size_t kRows = 2;
                            const MainMenuLayout layout{size, kRows};
                            const TextFaces shownFaces = textFaces();
                            appendGround(laidOut, size);
                            nx::ui::PanelPainter painter{laidOut, size};
                            paintGameName(painter, layout, shownFaces);
                            paintItemRow(painter, layout, shownFaces[MainMenuFont::Item], layout.item(0),
                                         texts->mainMenu.loading, true);
                            paintItemRow(painter, layout, shownFaces[MainMenuFont::Item], layout.item(1), map,
                                         false);
                        });
}

auto MainMenuPanel::textFaces() const noexcept -> TextFaces {
    TextFaces shownFaces;
    for (const MainMenuFont font : TextFaces::keys()) {
        shownFaces[font] = faces[font]->textFace();
    }
    return shownFaces;
}

void MainMenuPanel::appendGround(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) const {
    const nx::ui::PixelRect whole{.wide = nx::Pixels{nx::narrow<std::int32_t>(size.width)},
                                  .tall = nx::Pixels{nx::narrow<std::int32_t>(size.height)}};
    nx::ui::PanelPainter{quads, size}.fill(whole, kGround);
    if (background) {
        quads.push_back(
            {.image = &*background,
             .source = {.width = background->width(), .height = background->height()},
             .target = coveringRect({.width = background->width(), .height = background->height()}, size),
             .filter = nx::render::ScreenFilter::Linear});
        nx::ui::PanelPainter{quads, size}.fill(whole, kBackgroundVeil);
    }
}

} // namespace cs
