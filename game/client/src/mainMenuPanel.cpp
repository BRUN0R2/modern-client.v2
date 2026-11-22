// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import nx.platform;
import nx.render;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

using nx::render::ScreenColor;
using TextFaces = nx::EnumArray<MainMenuFont, nx::ui::TextFace>;

constexpr nx::log::Category kLog{"client"};

/// What the menu covers the whole screen with while no map is played, under its background.
constexpr ScreenColor kGround{.red = 23, .green = 28, .blue = 21};
/// What darkens the background, so that what the menu writes over it reads.
constexpr ScreenColor kBackgroundVeil{.red = 20, .green = 26, .blue = 15, .alpha = 108};
constexpr ScreenColor kDialogSurface{.red = 55, .green = 64, .blue = 44};
constexpr ScreenColor kDialogInset{.red = 34, .green = 42, .blue = 29};
constexpr ScreenColor kDialogHeader{.red = 66, .green = 77, .blue = 52};
constexpr ScreenColor kEdgeLight{.red = 135, .green = 148, .blue = 107};
constexpr ScreenColor kEdgeShadow{.red = 23, .green = 30, .blue = 19};
constexpr ScreenColor kControlSurface{.red = 72, .green = 84, .blue = 58};
constexpr ScreenColor kSelectedSurface{.red = 97, .green = 109, .blue = 73};

constexpr nx::EnumArray<MainMenuFont, MenuFontSize> kFontSizes{
    nx::EnumArray<MainMenuFont, MenuFontSize>::Storage{
        MenuFontSize{.line = MenuUnits{31}, .isStrong = true},
        MenuFontSize{.line = MenuUnits{26}, .isStrong = false},
        MenuFontSize{.line = MenuUnits{18}, .isStrong = false}}};

/// The text of the menu's that names each item.
constexpr nx::EnumArray<MainMenuAction, std::string MainMenuTexts::*> kItemTexts{
    nx::EnumArray<MainMenuAction, std::string MainMenuTexts::*>::Storage{
        &MainMenuTexts::resumeGame, &MainMenuTexts::disconnect, &MainMenuTexts::newGame,
        &MainMenuTexts::options, &MainMenuTexts::quit}};

/// The text of the options' that names each tab.
constexpr nx::EnumArray<OptionCategory, std::string OptionsTexts::*> kTabTexts{
    nx::EnumArray<OptionCategory, std::string OptionsTexts::*>::Storage{
        &OptionsTexts::game, &OptionsTexts::keyboard, &OptionsTexts::mouse, &OptionsTexts::audio,
        &OptionsTexts::video, &OptionsTexts::crosshairTab}};

/// How `text`, a value of a choice, is written in the language of `texts`.
[[nodiscard]] auto writtenOf(const ChoiceText& text, const OptionsTexts& texts) noexcept -> std::string_view {
    if (const auto* const field = std::get_if<std::string OptionsTexts::*>(&text)) {
        return texts.**field;
    }
    return std::get<std::string>(text);
}

/// What the menu calls the item that does `action`, in the language of `texts`.
[[nodiscard]] auto itemNameOf(const MainMenuTexts& texts, MainMenuAction action) noexcept
    -> std::string_view {
    return texts.*kItemTexts[action];
}

// In the design's units: what the layout leaves to what draws in it.
constexpr MenuUnits kDialogHeaderTall{59};
constexpr MenuUnits kLogoWidth{322};
constexpr MenuUnits kTextShadowOffset{2};
constexpr ScreenColor kTextShadow{.red = 12, .green = 15, .blue = 9, .alpha = 225};
constexpr MenuUnits kAccentBar{4};
constexpr MenuUnits kHintKeyGap{8};
constexpr MenuUnits kHintGap{28};
constexpr MenuUnits kRowInset{16};
constexpr MenuUnits kTabRule{2};         ///< Under the tab shown.
constexpr MenuUnits kMapsNameGap{6};     ///< From the name of the list to its first row.
constexpr MenuUnits kRowButtonInset{16}; ///< From a row's button's sides to its text.
constexpr MenuUnits kFieldPadding{10};
constexpr MenuUnits kSliderThumbWide{12};
constexpr MenuUnits kSliderThumbTall{19};
constexpr MenuUnits kCheckWide{18};
/// What the room left is divided by to put a thing halfway into it.
constexpr std::int32_t kMiddleDivisor = 2;
/// The sides an inset or a margin is left on: left and right, or top and foot.
constexpr std::int32_t kBothSides = 2;

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

/// Draws `line`, a text the player types, with `painter` in `face` within `rect`: ending at its end while it
/// fits, else scrolled so that its end shows; while it is typed, in gold and scrolled so that its cursor
/// shows, the cursor drawn where it is.
void paintTypedText(nx::ui::PanelPainter& painter, const nx::ui::TextFace& face, const ShownText& line,
                    const nx::ui::PixelRect& rect) {
    nx::ui::PanelPainter within = painter.inside(rect);
    const nx::Pixels wide = nx::ui::widthOf(*face.atlas, line.text);
    const nx::Pixels pen =
        line.cursorBytes
            ? nx::ui::widthOf(*face.atlas, std::string_view{line.text}.substr(0, *line.cursorBytes))
            : wide;
    const nx::Pixels room = rect.wide - nx::ui::kTextCursorWidth;
    const nx::Pixels start = wide <= room ? nx::Pixels{0} : -nx::ui::scrollToShow(pen, rect.wide);
    const nx::Pixels lineTall = face.atlas->lineHeight();
    const nx::Pixels top = (rect.tall - lineTall) / kMiddleDivisor;
    std::ignore =
        within.text(face, line.text, {.x = start, .y = top}, line.cursorBytes ? kMenuGold : kMenuInk);
    if (line.cursorBytes) {
        within.fill({.x = start + pen, .y = top, .wide = nx::ui::kTextCursorWidth, .tall = lineTall},
                    kMenuGold);
    }
}

void paintBevel(nx::ui::PanelPainter& painter, const nx::ui::PixelRect& rect, bool recessed = false) {
    const auto leading = recessed ? kEdgeShadow : kEdgeLight;
    const auto trailing = recessed ? kEdgeLight : kEdgeShadow;
    painter.fill({.x = rect.x, .y = rect.y, .wide = rect.wide, .tall = kMenuLineThick}, leading);
    painter.fill(
        {.x = rect.x + rect.wide - kMenuLineThick, .y = rect.y, .wide = kMenuLineThick, .tall = rect.tall},
        trailing);
    painter.fill({.x = rect.x, .y = rect.y, .wide = kMenuLineThick, .tall = rect.tall}, leading);
    painter.fill(
        {.x = rect.x, .y = rect.y + rect.tall - kMenuLineThick, .wide = rect.wide, .tall = kMenuLineThick},
        trailing);
}

void paintDialog(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const nx::ui::TextFace& face,
                 std::string_view title) {
    const auto& page = layout.page();
    painter.fill(page, kDialogSurface);
    const nx::Pixels headerTall = layout.pixels(kDialogHeaderTall);
    painter.fill({.x = page.x, .y = page.y, .wide = page.wide, .tall = headerTall}, kDialogHeader);
    painter.fill({.x = page.x, .y = page.y + headerTall, .wide = page.wide, .tall = kMenuLineThick},
                 kEdgeShadow);
    paintBevel(painter, page);
    std::ignore = painter.text(face, title, layout.pageTitle(), kMenuInk);
}

enum class MenuButtonLook : std::uint8_t { Secondary, Primary, Disabled };

void paintButton(nx::ui::PanelPainter& painter, const nx::ui::TextFace& face, std::string_view label,
                 const nx::ui::PixelRect& rect, MenuButtonLook look) {
    const bool primary = look == MenuButtonLook::Primary;
    const bool enabled = look != MenuButtonLook::Disabled;
    painter.fill(rect, primary ? kMenuGold : enabled ? kControlSurface : kDialogInset);
    paintBevel(painter, rect, !enabled);
    writeInMiddle(painter, face, label, rect, primary ? kOnMenuGold : enabled ? kMenuInk : kMenuInkFaint);
}

/// Draws `control`, what changes the setting on the list's row `row` by `layout`, with `painter`, in `face`,
/// writing `texts`.
void paintOptionControl(nx::ui::PanelPainter& painter, const MainMenuLayout& layout,
                        const nx::ui::TextFace& face, const OptionsTexts& texts, const ShownControl& control,
                        std::size_t row) {
    const nx::ui::PixelRect rect = layout.optionControl(row);
    const nx::Pixels end = rect.x + rect.wide;
    const nx::Pixels padding = layout.pixels(kFieldPadding);
    const nx::ui::PixelRect field = layout.optionField(row);
    if (const auto* const slider = std::get_if<ShownSlider>(&control)) {
        const nx::ui::PixelRect track = layout.optionTrack(row);
        const nx::Pixels fill{
            static_cast<std::int32_t>(std::lround(static_cast<float>(track.wide.value()) * slider->share))};
        const nx::Pixels thumbWide = layout.pixels(kSliderThumbWide);
        const nx::Pixels thumbTall = layout.pixels(kSliderThumbTall);
        painter.fill(track, kEdgeShadow);
        painter.fill({.x = track.x, .y = track.y, .wide = fill, .tall = track.tall}, kMenuGold);
        const nx::ui::PixelRect thumb{.x = track.x + fill - (thumbWide / kMiddleDivisor),
                                      .y = track.y + ((track.tall - thumbTall) / kMiddleDivisor),
                                      .wide = thumbWide,
                                      .tall = thumbTall};
        painter.fill(thumb, kControlSurface);
        paintBevel(painter, thumb);
        const nx::Pixels valueX = track.x + track.wide + padding;
        const nx::ui::PixelRect value{.x = valueX, .y = field.y, .wide = end - valueX, .tall = field.tall};
        painter.fill(value, kDialogInset);
        paintBevel(painter, value, true);
        writeInMiddle(painter, face, sliderText(*slider), value, kMenuInk);
    } else if (const auto* const line = std::get_if<ShownText>(&control)) {
        painter.fill(field, kDialogInset);
        paintBevel(painter, field, true);
        if (line->cursorBytes) {
            paintMenuOutline(painter, field, kMenuGold);
        }
        paintTypedText(painter, face, *line,
                       {.x = field.x + padding,
                        .y = field.y,
                        .wide = field.wide - (kBothSides * padding),
                        .tall = field.tall});
    } else if (const auto* const choice = std::get_if<ShownChoice>(&control)) {
        painter.fill(field, choice->isAdjustable ? kControlSurface : kDialogInset);
        paintBevel(painter, field, !choice->isAdjustable);
        if (choice->chosen) {
            writeInMiddle(painter, face, writtenOf(*choice->chosen, texts), field,
                          choice->isAdjustable ? kMenuInk : kMenuInkFaint);
        }
        if (choice->isAdjustable) {
            writeEnding(painter, face, choice->opensMenu ? "v" : ">", end - padding, rect, kMenuGold);
        }
    } else if (const auto* const keys = std::get_if<ShownKeys>(&control)) {
        // Waited for, it asks for its key; else its keys, or that it has none.
        if (keys->isWaited) {
            writeEnding(painter, face, texts.pressAKey, end, rect, kMenuGold);
        } else if (keys->keys.empty()) {
            writeEnding(painter, face, texts.noKey, rect.x + rect.wide / kMiddleDivisor, rect, kMenuInkFaint);
        } else {
            const std::string_view names = keys->keys;
            const auto split = names.find(kOptionKeySeparator);
            writeEnding(painter, face, names.substr(0, split), rect.x + rect.wide / kMiddleDivisor, rect,
                        kMenuInk);
            if (split != std::string_view::npos) {
                writeEnding(painter, face, names.substr(split + kOptionKeySeparator.size()), end, rect,
                            kMenuInk);
            }
        }
    } else if (const auto* const button = std::get_if<ShownButton>(&control)) {
        // Outlined as the page's buttons are, ending where the row's control ends.
        const std::string_view label = texts.*button->label;
        const nx::Pixels wide =
            nx::ui::widthOf(*face.atlas, label) + (kBothSides * layout.pixels(kRowButtonInset));
        const nx::ui::PixelRect box{.x = end - wide, .y = field.y, .wide = wide, .tall = field.tall};
        paintButton(painter, face, label, box, MenuButtonLook::Secondary);
    } else {
        const bool isOn = std::get<ShownSwitch>(control).isOn;
        const auto label = std::string_view{isOn ? texts.on : texts.off};
        const nx::Pixels checkWide = layout.pixels(kCheckWide);
        const nx::ui::PixelRect check{.x = end - nx::ui::widthOf(*face.atlas, label) - padding - checkWide,
                                      .y = rect.y + ((rect.tall - checkWide) / kMiddleDivisor),
                                      .wide = checkWide,
                                      .tall = checkWide};
        painter.fill(check, isOn ? kMenuGold : kDialogInset);
        paintBevel(painter, check, !isOn);
        if (isOn) {
            const nx::Pixels inset = checkWide / 3;
            painter.fill({.x = check.x + inset,
                          .y = check.y + inset,
                          .wide = check.wide - (kBothSides * inset),
                          .tall = check.tall - (kBothSides * inset)},
                         kOnMenuGold);
        }
        writeEnding(painter, face, label, end, rect, isOn ? kMenuGold : kMenuInkSoft);
    }
}

void paintCrosshairSample(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
                          const nx::ui::PixelRect& area, nx::rhi::Extent2d extent,
                          const CrosshairStyle& style, nx::EulerAngles punch, ScreenColor ground) {
    painter.fill(area, ground);
    const nx::rhi::Rect clip = painter.inside(area).clip();
    const float originX = static_cast<float>(extent.width / 2);
    const float originY = static_cast<float>(extent.height / 2);
    const float halfWide = static_cast<float>(area.wide.value()) * 0.5F;
    const float halfTall = static_cast<float>(area.tall.value()) * 0.5F;
    const float centerX = static_cast<float>(area.x.value()) + halfWide;
    const float centerY = static_cast<float>(area.y.value()) + halfTall;
    const std::size_t first = quads.size();
    const nx::render::ScreenPoint offset = style.followsRecoil
                                               ? crosshairRecoilOffset({.angles = punch}, punch, extent)
                                               : nx::render::ScreenPoint{};
    appendCrosshair(quads, style, crosshairDistanceOf(style, punch), extent, offset);
    auto marks = std::span{quads}.subspan(first);
    float reachX = 1.0F;
    float reachY = 1.0F;
    for (auto& mark : marks) {
        auto& rect = mark.target;
        rect.x -= originX;
        rect.y -= originY;
        reachX = std::max({reachX, std::abs(rect.x), std::abs(rect.x + rect.width)});
        reachY = std::max({reachY, std::abs(rect.y), std::abs(rect.y + rect.height)});
    }
    // The reticle normally stays at gameplay size. Fit it inside the preview at very wide resolutions.
    const float scale = std::min({1.0F, halfWide * 0.9F / reachX, halfTall * 0.9F / reachY});
    for (auto& mark : marks) {
        auto& rect = mark.target;
        rect.x = centerX + rect.x * scale;
        rect.y = centerY + rect.y * scale;
        rect.width *= scale;
        rect.height *= scale;
        mark.clip = clip;
    }
}

void paintCrosshairPreview(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
                           const MainMenuLayout& layout, const nx::ui::TextFace& face,
                           const OptionsTexts& texts, const CrosshairStyle& style, nx::rhi::Extent2d extent) {
    const auto preview = layout.crosshairPreview();
    const nx::Pixels headingHeight = layout.pixels(MenuUnits{36});
    const nx::Pixels inset = layout.pixels(MenuUnits{10});
    const nx::Pixels sampleHeight = (preview.tall - (2 * inset) - headingHeight) / 2;
    const nx::Pixels labelHeight = layout.pixels(MenuUnits{28});
    painter.fill(preview, kDialogInset);
    paintBevel(painter, preview, true);
    writeInMiddle(painter, face, texts.crosshairPreview,
                  {.x = preview.x, .y = preview.y, .wide = preview.wide, .tall = headingHeight}, kMenuInk);
    const auto sample = [&](nx::Pixels top, std::string_view label, nx::EulerAngles punch,
                            ScreenColor ground) {
        const nx::ui::PixelRect labelRect{
            .x = preview.x + inset, .y = top, .wide = preview.wide - (2 * inset), .tall = labelHeight};
        writeInMiddle(painter, face, label, labelRect, kMenuInkSoft);
        paintCrosshairSample(quads, painter,
                             {.x = labelRect.x,
                              .y = top + labelHeight,
                              .wide = labelRect.wide,
                              .tall = sampleHeight - labelHeight},
                             extent, style, punch, ground);
    };
    const nx::Pixels top = preview.y + headingHeight;
    sample(top, texts.crosshairIdle, {}, {.red = 43, .green = 50, .blue = 48});
    sample(top + sampleHeight + inset, texts.crosshairFiring,
           {.pitch = nx::Degrees{-4.0F}, .yaw = nx::Degrees{1.25F}}, {.red = 107, .green = 99, .blue = 82});
}

void paintChoiceMenu(nx::ui::PanelPainter& painter, const MainMenuLayout& layout,
                     const nx::ui::TextFace& face, const OptionsTexts& texts, const ShownChoiceMenu& shown) {
    const auto menu = layout.choiceMenu(shown.row, shown.values.size());
    if (menu.rows == 0) {
        return;
    }
    const std::size_t first = std::min(shown.first, shown.values.size() - menu.rows);
    painter.fill(menu.bounds, kDialogInset);
    for (std::size_t item = 0; item < menu.rows; ++item) {
        const std::size_t index = first + item;
        const auto rect = menu.item(item);
        if (index == shown.highlighted) {
            painter.fill(rect, kSelectedSurface);
        }
        writeInMiddle(painter, face, writtenOf(shown.values[index], texts), rect,
                      index == shown.chosen ? kMenuGold : kMenuInk);
    }
    paintBevel(painter, menu.bounds);
    if (menu.rows < shown.values.size()) {
        const nx::Pixels width = layout.pixels(kTabRule);
        const auto total = nx::narrow<std::int32_t>(shown.values.size());
        painter.fill({.x = menu.bounds.x + menu.bounds.wide - width,
                      .y = menu.bounds.y + menu.bounds.tall * nx::narrow<std::int32_t>(first) / total,
                      .wide = width,
                      .tall = menu.bounds.tall * nx::narrow<std::int32_t>(menu.rows) / total},
                     kMenuInkFaint);
    }
}

/// Draws the options showing `shown` by `layout` with `painter`, in `faces`, writing `texts`.
void paintOptions(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
                  const MainMenuLayout& pageLayout, const TextFaces& faces, const Texts& texts,
                  const OptionsFrame& shown, nx::rhi::Extent2d extent) {
    const MainMenuLayout layout = shown.crosshairPreview ? pageLayout.withCrosshairPreview() : pageLayout;
    paintDialog(painter, layout, faces[MainMenuFont::Item], texts.mainMenu.options);

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    const nx::ui::TextFace& rowFace = faces[MainMenuFont::Row];
    for (const OptionCategory tab : nx::EnumArray<OptionCategory, bool>::keys()) {
        const nx::ui::PixelRect rect = layout.optionTab(static_cast<std::size_t>(std::to_underlying(tab)));
        const bool isShown = tab == shown.category;
        painter.fill(rect, isShown ? kSelectedSurface : kControlSurface);
        paintBevel(painter, rect, isShown);
        if (isShown) {
            const nx::Pixels rule = layout.pixels(kTabRule);
            painter.fill({.x = rect.x, .y = rect.y + rect.tall - rule, .wide = rect.wide, .tall = rule},
                         kMenuGold);
        }
        // The tabs take the keys while no row is selected.
        writeInMiddle(painter, rowFace, texts.options.*kTabTexts[tab], rect, isShown ? kMenuGold : kMenuInk);
    }

    const auto top = layout.listRow(0);
    const auto last = layout.listRow(layout.listRows() - 1);
    const nx::Pixels headerHeight =
        shown.category == OptionCategory::Keyboard ? layout.pixels(MenuUnits{36}) : nx::Pixels{};
    const nx::ui::PixelRect table{.x = top.x,
                                  .y = top.y - headerHeight,
                                  .wide = top.wide,
                                  .tall = last.y + last.tall - top.y + headerHeight};
    painter.fill(table, kDialogInset);
    paintBevel(painter, table, true);
    if (shown.category == OptionCategory::Keyboard) {
        const nx::ui::PixelRect header{.x = table.x, .y = table.y, .wide = table.wide, .tall = headerHeight};
        const nx::Pixels inset = layout.pixels(kRowInset);
        std::ignore =
            painter.text(hintFace, texts.options.action,
                         {.x = header.x + inset,
                          .y = header.y + ((header.tall - hintFace.atlas->lineHeight()) / kMiddleDivisor)},
                         kMenuGold);
        const auto control = layout.optionControl(0);
        writeEnding(painter, hintFace, texts.options.keys, control.x + control.wide / kMiddleDivisor, header,
                    kMenuGold);
        writeEnding(painter, hintFace, texts.options.alternateKey, control.x + control.wide, header,
                    kMenuGold);
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
        paintOptionControl(painter, layout, rowFace, texts.options, option.control, row);
    }
    if (const std::optional<nx::ui::PixelRect> thumb = layout.listThumb(shown.first, shown.count)) {
        painter.fill(*thumb, kMenuInkFaint);
    }

    if (shown.crosshairPreview) {
        paintCrosshairPreview(quads, painter, layout, hintFace, texts.options, *shown.crosshairPreview,
                              extent);
        paintButton(painter, rowFace, texts.mainMenu.goBack, layout.backButton(), MenuButtonLook::Secondary);
        paintButton(painter, rowFace, texts.options.apply, layout.confirmButton(),
                    shown.canApplyCrosshair ? MenuButtonLook::Primary : MenuButtonLook::Disabled);
    } else {
        paintButton(painter, rowFace, texts.options.done, layout.confirmButton(), MenuButtonLook::Primary);
    }
    if (shown.choiceMenu) {
        paintChoiceMenu(painter, layout, rowFace, texts.options, *shown.choiceMenu);
    }
}

/// Draws the page of a new game showing `page` by `layout` with `painter`, in `faces`, writing `texts`.
void paintNewGame(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const TextFaces& faces,
                  const MainMenuTexts& texts, const NewGamePage& page) {
    paintDialog(painter, layout, faces[MainMenuFont::Item], texts.newGame);

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

    paintButton(painter, rowFace, texts.goBack, layout.backButton(), MenuButtonLook::Secondary);
    paintButton(painter, rowFace, texts.start, layout.confirmButton(),
                page.rows.empty() ? MenuButtonLook::Disabled : MenuButtonLook::Primary);
}

/// Draws the project's logo over the navigation, retaining the image's aspect ratio.
void paintLogo(std::vector<nx::render::ScreenQuad>& quads, const MainMenuLayout& layout,
               const nx::render::ScreenImage& logo) {
    const auto wide = static_cast<float>(layout.pixels(kLogoWidth).value());
    const auto tall = wide * static_cast<float>(logo.height()) / static_cast<float>(logo.width());
    const auto& top = layout.title();
    quads.push_back({.image = &logo,
                     .source = {.width = logo.width(), .height = logo.height()},
                     .target = {.x = static_cast<float>(top.x.value()),
                                .y = static_cast<float>(top.y.value()),
                                .width = wide,
                                .height = tall},
                     .filter = nx::render::ScreenFilter::Linear});
}

/// A row of the items' column, with gold text for the selected row.
void paintItemRow(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const nx::ui::TextFace& face,
                  const nx::ui::PixelRect& rect, std::string_view text, bool isLit) {
    const nx::Pixels top = rect.y + ((rect.tall - face.atlas->lineHeight()) / kMiddleDivisor);
    const nx::Pixels shadow = layout.pixels(kTextShadowOffset);
    std::ignore = painter.text(face, text, {.x = rect.x + shadow, .y = top + shadow}, kTextShadow);
    std::ignore = painter.text(face, text, {.x = rect.x, .y = top}, isLit ? kMenuGold : kMenuInk);
}

/// Draws the menu showing `frame` by `layout` with `painter`, in `faces`, writing those of `written`.
void paintMenu(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
               const MainMenuLayout& layout, const TextFaces& faces, const Texts& written,
               const MainMenuFrame& frame, nx::rhi::Extent2d extent, const nx::render::ScreenImage& logo) {
    const MainMenuTexts& texts = written.mainMenu;
    // A key and what it does, one after the other along the menu's foot.
    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    nx::ui::PixelPoint pen = layout.hints();
    const auto tell = [&](std::string_view key, std::string_view what) {
        pen.x = painter.text(hintFace, key, pen, kMenuInkSoft) + layout.pixels(kHintKeyGap);
        pen.x = painter.text(hintFace, what, pen, kMenuInkFaint) + layout.pixels(kHintGap);
    };
    if (frame.options) {
        paintOptions(quads, painter, layout, faces, written, *frame.options, extent);
        const auto button = layout.confirmButton();
        pen = {.x = layout.pageTitle().x,
               .y = button.y + ((button.tall - hintFace.atlas->lineHeight()) / kMiddleDivisor)};
        if (frame.options->crosshairPreview) {
            tell(written.options.applyKey, written.options.apply);
            return;
        }
        tell(texts.keyUpDown, texts.select);
        if (frame.options->category == OptionCategory::Keyboard) {
            tell(texts.keyEnter, texts.change);
            tell(texts.keyDelete, texts.clear);
        } else {
            if (frame.options->category == OptionCategory::Video) {
                tell(texts.keyEnter, texts.choose);
            }
            tell(texts.keyLeftRight, texts.change);
        }
        return;
    }
    if (frame.newGame) {
        paintNewGame(painter, layout, faces, texts, *frame.newGame);
        return;
    }
    paintLogo(quads, layout, logo);
    for (const auto& [index, action] : std::views::enumerate(frame.items)) {
        paintItemRow(painter, layout, faces[MainMenuFont::Item], layout.item(nx::narrow<std::size_t>(index)),
                     itemNameOf(texts, action), frame.selected == action);
    }

    tell(texts.keyUpDown, texts.select);
    tell(texts.keyEnter, texts.choose);
    if (std::ranges::contains(frame.items, MainMenuAction::ResumeGame)) {
        tell(texts.keyEscape, texts.resume);
    }
}

} // namespace

MainMenuPanel::MainMenuPanel(const MainMenuLook& listed, const Texts& written) noexcept
    : look{&listed}, texts{&written} {}

void MainMenuPanel::writeIn(const Texts& written) noexcept {
    texts = &written;
    shown.forget();
    shownLoading.forget();
}

auto MainMenuPanel::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                         nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                         const std::filesystem::path& logoPath, std::uint32_t screenHeight)
    -> nx::Result<void, MenuPanelError> {
    nx::expects(screenHeight > 0, "MainMenuPanel::load: a screen of some height");
    if (!logo) {
        const std::string label = nx::platform::toUtf8(logoPath);
        auto bytes = nx::platform::readFile(logoPath);
        if (!bytes) {
            return nx::fail(MenuPanelError::ImageUnavailable, label, std::move(bytes.error()));
        }
        auto picture = nx::assets::readTga(*bytes);
        if (!picture) {
            return nx::fail(MenuPanelError::ImageUnavailable, label, std::move(picture.error()));
        }
        auto loaded =
            screen.loadImage(commands, picture->pixels, picture->width, picture->height, "mainMenu.logo");
        if (!loaded) {
            return nx::fail(MenuPanelError::GpuFailed, label, std::move(loaded.error()));
        }
        logo.emplace(std::move(*loaded));
    }
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
                     paintMenu(laidOut, painter, layout, textFaces(), *texts, frame, size, *logo);
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
                            paintLogo(laidOut, layout, *logo);
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
