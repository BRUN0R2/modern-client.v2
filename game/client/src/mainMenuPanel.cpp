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
constexpr float kHalf = 0.5F;
constexpr ScreenColor kCrosshairIdleGround{.red = 43, .green = 50, .blue = 48};
constexpr ScreenColor kCrosshairFiringGround{.red = 107, .green = 99, .blue = 82};
constexpr nx::EulerAngles kCrosshairIdlePunch{};
constexpr nx::EulerAngles kCrosshairFiringPunch{.pitch = nx::Degrees{-4.0F}, .yaw = nx::Degrees{1.25F}};

/// What the menu covers the whole screen with while no map is played, under its background.
constexpr ScreenColor kGround{.red = 0, .green = 0, .blue = 0};

// The original's items: its TrackerScheme.res's InGameDesktop colors.
constexpr ScreenColor kItemText{.red = 238, .green = 174, .blue = 0};  ///< MenuColor
constexpr ScreenColor kItemArmed{.red = 255, .green = 186, .blue = 0}; ///< ArmedMenuColor
constexpr ScreenColor kItemShadow{.red = 0, .green = 0, .blue = 0, .alpha = 230};
/// What each copy of an armed item's glow is drawn in (BlurMenuColor, faint).
constexpr ScreenColor kItemGlow{.red = 255, .green = 178, .blue = 67, .alpha = 34};
/// Where the copies of the glow go about the text: each way, by its reach.
constexpr std::array<std::array<std::int32_t, 2>, 8> kItemGlowDirections{
    {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
/// How far the glow reaches about the text.
constexpr MenuUnits kItemGlowReach{3};

// The original's windows: its TrackerScheme.res's colors, of Steam's classic look.
constexpr ScreenColor kControlBg{.red = 76, .green = 88, .blue = 68};       ///< ControlBG
constexpr ScreenColor kWindowBg{.red = 62, .green = 70, .blue = 55};        ///< WindowBG and ListBG
constexpr ScreenColor kControlDarkBg{.red = 90, .green = 106, .blue = 80};  ///< ControlDarkBG
constexpr ScreenColor kSelectionBg{.red = 149, .green = 136, .blue = 49};   ///< SelectionBG
constexpr ScreenColor kBorderBright{.red = 136, .green = 145, .blue = 128}; ///< BorderBright
constexpr ScreenColor kBorderDark{.red = 40, .green = 46, .blue = 34};      ///< BorderDark
constexpr ScreenColor kBorderSelection{.red = 0, .green = 0, .blue = 0};    ///< BorderSelection
constexpr ScreenColor kBaseText{.red = 216, .green = 222, .blue = 211};     ///< BaseText
constexpr ScreenColor kBrightText{.red = 255, .green = 255, .blue = 255};   ///< BrightBaseText
constexpr ScreenColor kDimText{.red = 160, .green = 170, .blue = 149};      ///< DimBaseText
constexpr ScreenColor kDisabledText{.red = 117, .green = 128, .blue = 111}; ///< DisabledText1
constexpr ScreenColor kBrightControl{.red = 196, .green = 181, .blue = 80}; ///< BrightControlText
constexpr ScreenColor kSliderTrack{.red = 31, .green = 31, .blue = 31};     ///< SliderTrackColor

/// The original's items are Trebuchet MS, bold (MenuLarge); its windows' text Tahoma (Default).
constexpr std::int32_t kBoldWeight = 700;
constexpr nx::EnumArray<MainMenuFont, MenuFontSize> kFontSizes{
    nx::EnumArray<MainMenuFont, MenuFontSize>::Storage{
        MenuFontSize{.line = MenuUnits{36}, .isStrong = true, .weight = kBoldWeight},
        MenuFontSize{.line = MenuUnits{24}, .isStrong = false},
        MenuFontSize{.line = MenuUnits{20}, .isStrong = false},
        MenuFontSize{.line = MenuUnits{26}, .isStrong = false, .weight = kBoldWeight}}};

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
constexpr MenuUnits kLogoWidth{322};
/// How tall the original's logo shows: about as on a screen of 600 lines, the original's own.
constexpr MenuUnits kOriginalLogoTall{58};
constexpr MenuUnits kTextShadowOffset{2};
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
        within.text(face, line.text, {.x = start, .y = top}, line.cursorBytes ? kBrightText : kBaseText);
    if (line.cursorBytes) {
        within.fill({.x = start + pen, .y = top, .wide = nx::ui::kTextCursorWidth, .tall = lineTall},
                    kBaseText);
    }
}

void paintBevel(nx::ui::PanelPainter& painter, const nx::ui::PixelRect& rect, bool recessed = false) {
    const auto leading = recessed ? kBorderDark : kBorderBright;
    const auto trailing = recessed ? kBorderBright : kBorderDark;
    painter.fill({.x = rect.x, .y = rect.y, .wide = rect.wide, .tall = kMenuLineThick}, leading);
    painter.fill(
        {.x = rect.x + rect.wide - kMenuLineThick, .y = rect.y, .wide = kMenuLineThick, .tall = rect.tall},
        trailing);
    painter.fill({.x = rect.x, .y = rect.y, .wide = kMenuLineThick, .tall = rect.tall}, leading);
    painter.fill(
        {.x = rect.x, .y = rect.y + rect.tall - kMenuLineThick, .wide = rect.wide, .tall = kMenuLineThick},
        trailing);
}

/// A window of the original's: of its controls' color, its edges lit at its top left, its title at its top
/// left in white.
void paintDialog(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const nx::ui::TextFace& face,
                 std::string_view title) {
    const auto& page = layout.page();
    painter.fill(page, kControlBg);
    paintBevel(painter, page);
    std::ignore = painter.text(face, title, layout.pageTitle(), kBrightText);
}

enum class MenuButtonLook : std::uint8_t { Secondary, Primary, Disabled };

/// A button of the original's: raised, the default one outlined in black besides.
void paintButton(nx::ui::PanelPainter& painter, const nx::ui::TextFace& face, std::string_view label,
                 const nx::ui::PixelRect& rect, MenuButtonLook look) {
    painter.fill(rect, kControlBg);
    if (look == MenuButtonLook::Primary) {
        paintMenuOutline(painter, rect, kBorderSelection);
        paintBevel(painter, {.x = rect.x + kMenuLineThick,
                             .y = rect.y + kMenuLineThick,
                             .wide = rect.wide - (kBothSides * kMenuLineThick),
                             .tall = rect.tall - (kBothSides * kMenuLineThick)});
    } else {
        paintBevel(painter, rect);
    }
    writeInMiddle(painter, face, label, rect, look == MenuButtonLook::Disabled ? kDisabledText : kBaseText);
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
        // A slider of the original's: a dark track, recessed, its handle raised.
        painter.fill(track, kSliderTrack);
        paintBevel(painter, track, true);
        const nx::ui::PixelRect thumb{.x = track.x + fill - (thumbWide / kMiddleDivisor),
                                      .y = track.y + ((track.tall - thumbTall) / kMiddleDivisor),
                                      .wide = thumbWide,
                                      .tall = thumbTall};
        painter.fill(thumb, kControlBg);
        paintBevel(painter, thumb);
        const nx::Pixels valueX = track.x + track.wide + padding;
        const nx::ui::PixelRect value{.x = valueX, .y = field.y, .wide = end - valueX, .tall = field.tall};
        painter.fill(value, kWindowBg);
        paintBevel(painter, value, true);
        writeInMiddle(painter, face, sliderText(*slider), value, kBaseText);
    } else if (const auto* const line = std::get_if<ShownText>(&control)) {
        painter.fill(field, kWindowBg);
        paintBevel(painter, field, true);
        if (line->cursorBytes) {
            paintMenuOutline(painter, field, kBrightControl);
        }
        paintTypedText(painter, face, *line,
                       {.x = field.x + padding,
                        .y = field.y,
                        .wide = field.wide - (kBothSides * padding),
                        .tall = field.tall});
    } else if (const auto* const choice = std::get_if<ShownChoice>(&control)) {
        // A combo box of the original's: a recessed pane of text, its arrow at its right.
        painter.fill(field, choice->isAdjustable ? kWindowBg : kControlBg);
        paintBevel(painter, field, true);
        if (choice->chosen) {
            writeInMiddle(painter, face, writtenOf(*choice->chosen, texts), field,
                          choice->isAdjustable ? kBaseText : kDisabledText);
        }
        if (choice->isAdjustable) {
            writeEnding(painter, face, choice->opensMenu ? "v" : ">", end - padding, rect, kDimText);
        }
    } else if (const auto* const keys = std::get_if<ShownKeys>(&control)) {
        // Waited for, it asks for its key; else its keys, or that it has none.
        if (keys->isWaited) {
            writeEnding(painter, face, texts.pressAKey, end, rect, kBrightControl);
        } else if (keys->keys.empty()) {
            writeEnding(painter, face, texts.noKey, rect.x + rect.wide / kMiddleDivisor, rect, kDimText);
        } else {
            const std::string_view names = keys->keys;
            const auto split = names.find(kOptionKeySeparator);
            writeEnding(painter, face, names.substr(0, split), rect.x + rect.wide / kMiddleDivisor, rect,
                        kBaseText);
            if (split != std::string_view::npos) {
                writeEnding(painter, face, names.substr(split + kOptionKeySeparator.size()), end, rect,
                            kBaseText);
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
        // A check button of the original's: a recessed box, its check in the selected controls' color.
        painter.fill(check, kWindowBg);
        paintBevel(painter, check, true);
        if (isOn) {
            const nx::Pixels inset = checkWide / 4;
            painter.fill({.x = check.x + inset,
                          .y = check.y + inset,
                          .wide = check.wide - (kBothSides * inset),
                          .tall = check.tall - (kBothSides * inset)},
                         kBrightControl);
        }
        writeEnding(painter, face, label, end, rect, isOn ? kBaseText : kDimText);
    }
}

void paintCrosshairSample(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
                          const nx::ui::PixelRect& area, nx::rhi::Extent2d extent,
                          const CrosshairStyle& style, nx::EulerAngles punch, ScreenColor ground) {
    painter.fill(area, ground);
    const nx::rhi::Rect clip = painter.inside(area).clip();
    const float originX = static_cast<float>(extent.width) * kHalf;
    const float originY = static_cast<float>(extent.height) * kHalf;
    const float halfWide = static_cast<float>(area.wide.value()) * kHalf;
    const float halfTall = static_cast<float>(area.tall.value()) * kHalf;
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
    painter.fill(preview, kWindowBg);
    paintBevel(painter, preview, true);
    writeInMiddle(painter, face, texts.crosshairPreview,
                  {.x = preview.x, .y = preview.y, .wide = preview.wide, .tall = headingHeight}, kBaseText);
    const auto sample = [&](nx::Pixels top, std::string_view label, nx::EulerAngles punch,
                            ScreenColor ground) {
        const nx::ui::PixelRect labelRect{
            .x = preview.x + inset, .y = top, .wide = preview.wide - (2 * inset), .tall = labelHeight};
        writeInMiddle(painter, face, label, labelRect, kDimText);
        paintCrosshairSample(quads, painter,
                             {.x = labelRect.x,
                              .y = top + labelHeight,
                              .wide = labelRect.wide,
                              .tall = sampleHeight - labelHeight},
                             extent, style, punch, ground);
    };
    const nx::Pixels top = preview.y + headingHeight;
    sample(top, texts.crosshairIdle, kCrosshairIdlePunch, kCrosshairIdleGround);
    sample(top + sampleHeight + inset, texts.crosshairFiring, kCrosshairFiringPunch, kCrosshairFiringGround);
}

void paintChoiceMenu(nx::ui::PanelPainter& painter, const MainMenuLayout& layout,
                     const nx::ui::TextFace& face, const OptionsTexts& texts, const ShownChoiceMenu& shown) {
    const auto menu = layout.choiceMenu(shown.row, shown.values.size());
    if (menu.rows == 0) {
        return;
    }
    const std::size_t first = std::min(shown.first, shown.values.size() - menu.rows);
    painter.fill(menu.bounds, kControlBg);
    for (std::size_t item = 0; item < menu.rows; ++item) {
        const std::size_t index = first + item;
        const auto rect = menu.item(item);
        const bool isArmed = index == shown.highlighted;
        if (isArmed) {
            painter.fill(rect, kSelectionBg);
        }
        const ScreenColor color = isArmed ? kBrightText : index == shown.chosen ? kBrightControl : kBaseText;
        writeInMiddle(painter, face, writtenOf(shown.values[index], texts), rect, color);
    }
    paintBevel(painter, menu.bounds);
    if (menu.rows < shown.values.size()) {
        const nx::Pixels width = layout.pixels(kTabRule);
        const auto total = nx::narrow<std::int32_t>(shown.values.size());
        painter.fill({.x = menu.bounds.x + menu.bounds.wide - width,
                      .y = menu.bounds.y + menu.bounds.tall * nx::narrow<std::int32_t>(first) / total,
                      .wide = width,
                      .tall = menu.bounds.tall * nx::narrow<std::int32_t>(menu.rows) / total},
                     kBorderBright);
    }
}

/// Draws the options showing `shown` by `layout` with `painter`, in `faces`, writing `texts`.
void paintOptions(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
                  const MainMenuLayout& pageLayout, const TextFaces& faces, const Texts& texts,
                  const OptionsFrame& shown, nx::rhi::Extent2d extent) {
    const MainMenuLayout layout = shown.crosshairPreview ? pageLayout.withCrosshairPreview() : pageLayout;
    paintDialog(painter, layout, faces[MainMenuFont::Title], texts.mainMenu.options);

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    const nx::ui::TextFace& rowFace = faces[MainMenuFont::Row];
    for (const OptionCategory tab : nx::EnumArray<OptionCategory, bool>::keys()) {
        const nx::ui::PixelRect rect = layout.optionTab(static_cast<std::size_t>(std::to_underlying(tab)));
        const bool isShown = tab == shown.category;
        // A property sheet's tabs: the one shown raised and open onto its page, the others behind it.
        painter.fill(rect, isShown ? kControlBg : kWindowBg);
        paintBevel(painter, rect);
        if (isShown) {
            painter.fill({.x = rect.x + kMenuLineThick,
                          .y = rect.y + rect.tall - kMenuLineThick,
                          .wide = rect.wide - (kBothSides * kMenuLineThick),
                          .tall = layout.pixels(kTabRule)},
                         kControlBg);
        }
        // The tabs take the keys while no row is selected.
        writeInMiddle(painter, rowFace, texts.options.*kTabTexts[tab], rect, isShown ? kBrightText : kDimText);
    }

    const auto top = layout.listRow(0);
    const auto last = layout.listRow(layout.listRows() - 1);
    const nx::Pixels headerHeight =
        shown.category == OptionCategory::Keyboard ? layout.pixels(MenuUnits{36}) : nx::Pixels{};
    const nx::ui::PixelRect table{.x = top.x,
                                  .y = top.y - headerHeight,
                                  .wide = top.wide,
                                  .tall = last.y + last.tall - top.y + headerHeight};
    painter.fill(table, kWindowBg);
    paintBevel(painter, table, true);
    if (shown.category == OptionCategory::Keyboard) {
        // A list's header of the original's: raised, as its buttons.
        const nx::ui::PixelRect header{.x = table.x, .y = table.y, .wide = table.wide, .tall = headerHeight};
        painter.fill(header, kControlBg);
        paintBevel(painter, header);
        const nx::Pixels inset = layout.pixels(kRowInset);
        std::ignore =
            painter.text(hintFace, texts.options.action,
                         {.x = header.x + inset,
                          .y = header.y + ((header.tall - hintFace.atlas->lineHeight()) / kMiddleDivisor)},
                         kBaseText);
        const auto control = layout.optionControl(0);
        writeEnding(painter, hintFace, texts.options.keys, control.x + control.wide / kMiddleDivisor, header,
                    kBaseText);
        writeEnding(painter, hintFace, texts.options.alternateKey, control.x + control.wide, header,
                    kBaseText);
    }

    for (const auto& [index, option] : std::views::enumerate(shown.rows)) {
        const auto row = nx::narrow<std::size_t>(index);
        const nx::ui::PixelRect rect = layout.listRow(row);
        const bool isSelected = shown.selectedRow == row;
        if (isSelected) {
            painter.fill(rect, kSelectionBg);
        }
        std::ignore =
            painter.text(rowFace, texts.options.*option.name,
                         {.x = rect.x + layout.pixels(kRowInset),
                          .y = rect.y + ((rect.tall - rowFace.atlas->lineHeight()) / kMiddleDivisor)},
                         isSelected ? kBrightText : kBaseText);
        paintOptionControl(painter, layout, rowFace, texts.options, option.control, row);
    }
    if (const std::optional<nx::ui::PixelRect> thumb = layout.listThumb(shown.first, shown.count)) {
        painter.fill(*thumb, kControlDarkBg);
        paintBevel(painter, *thumb);
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
    paintDialog(painter, layout, faces[MainMenuFont::Title], texts.newGame);

    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    const nx::ui::TextFace& rowFace = faces[MainMenuFont::Row];
    const nx::ui::PixelRect top = layout.listRow(0);
    std::ignore = painter.text(
        hintFace, texts.map,
        {.x = top.x, .y = top.y - layout.pixels(kMapsNameGap) - hintFace.atlas->lineHeight()}, kDimText);
    const auto rowText = [&](const nx::ui::PixelRect& rect, std::string_view text, ScreenColor color) {
        std::ignore =
            painter.text(rowFace, text,
                         {.x = rect.x + layout.pixels(kRowInset),
                          .y = rect.y + ((rect.tall - rowFace.atlas->lineHeight()) / kMiddleDivisor)},
                         color);
    };
    // The original's list: a recessed pane, the row selected in the selection's color.
    const nx::ui::PixelRect last = layout.listRow(layout.listRows() - 1);
    const nx::ui::PixelRect list{.x = top.x, .y = top.y, .wide = top.wide, .tall = last.y + last.tall - top.y};
    painter.fill(list, kWindowBg);
    paintBevel(painter, list, true);
    if (page.rows.empty()) {
        rowText(top, texts.noMaps, kDimText);
    }
    for (const auto& [index, name] : std::views::enumerate(page.rows)) {
        const auto row = nx::narrow<std::size_t>(index);
        const nx::ui::PixelRect rect = layout.listRow(row);
        const bool isSelected = page.selectedRow == row;
        if (isSelected) {
            painter.fill(rect, kSelectionBg);
        }
        rowText(rect, name, isSelected ? kBrightText : kBaseText);
    }
    if (const std::optional<nx::ui::PixelRect> thumb = layout.listThumb(page.first, page.count)) {
        painter.fill(*thumb, kControlDarkBg);
        paintBevel(painter, *thumb);
    }

    paintButton(painter, rowFace, texts.goBack, layout.backButton(), MenuButtonLook::Secondary);
    paintButton(painter, rowFace, texts.start, layout.confirmButton(),
                page.rows.empty() ? MenuButtonLook::Disabled : MenuButtonLook::Primary);
}

/// Draws the logo over the navigation, retaining the image's aspect ratio: the original's as tall as on its
/// own screen, the project's as wide as its design.
void paintLogo(std::vector<nx::render::ScreenQuad>& quads, const MainMenuLayout& layout,
               const nx::render::ScreenImage& logo, bool isOriginal) {
    const float shape = static_cast<float>(logo.width()) / static_cast<float>(logo.height());
    const float wide = isOriginal ? static_cast<float>(layout.pixels(kOriginalLogoTall).value()) * shape
                                  : static_cast<float>(layout.pixels(kLogoWidth).value());
    const float tall = wide / shape;
    const auto& top = layout.title();
    quads.push_back({.image = &logo,
                     .source = {.width = logo.width(), .height = logo.height()},
                     .target = {.x = static_cast<float>(top.x.value()),
                                .y = static_cast<float>(top.y.value()),
                                .width = wide,
                                .height = tall},
                     .filter = nx::render::ScreenFilter::Linear});
}

/// A row of the items' column, as the original's: orange, casting a drop shadow, brighter while armed.
void paintItemRow(nx::ui::PanelPainter& painter, const MainMenuLayout& layout, const nx::ui::TextFace& face,
                  const nx::ui::PixelRect& rect, std::string_view text, bool isLit) {
    const nx::Pixels top = rect.y + ((rect.tall - face.atlas->lineHeight()) / kMiddleDivisor);
    const nx::Pixels shadow = layout.pixels(kTextShadowOffset);
    if (isLit) {
        // The original's armed item glows (MenuBlurLarge: its text blurred, added over the background):
        // faint copies of the text all about it.
        const nx::Pixels reach = layout.pixels(kItemGlowReach);
        for (const auto [across, down] : kItemGlowDirections) {
            std::ignore = painter.text(face, text, {.x = rect.x + (across * reach), .y = top + (down * reach)},
                                       kItemGlow);
        }
    }
    std::ignore = painter.text(face, text, {.x = rect.x + shadow, .y = top + shadow}, kItemShadow);
    std::ignore = painter.text(face, text, {.x = rect.x, .y = top}, isLit ? kItemArmed : kItemText);
}

/// Draws the menu showing `frame` by `layout` with `painter`, in `faces`, writing those of `written`.
void paintMenu(std::vector<nx::render::ScreenQuad>& quads, nx::ui::PanelPainter& painter,
               const MainMenuLayout& layout, const TextFaces& faces, const Texts& written,
               const MainMenuFrame& frame, nx::rhi::Extent2d extent, const nx::render::ScreenImage& logo,
               bool isOriginalLogo) {
    const MainMenuTexts& texts = written.mainMenu;
    // A key and what it does, one after the other along the menu's foot.
    const nx::ui::TextFace& hintFace = faces[MainMenuFont::Hint];
    nx::ui::PixelPoint pen = layout.hints();
    const auto tell = [&](std::string_view key, std::string_view what) {
        pen.x = painter.text(hintFace, key, pen, kBaseText) + layout.pixels(kHintKeyGap);
        pen.x = painter.text(hintFace, what, pen, kDimText) + layout.pixels(kHintGap);
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
    // The original's items tell no keys.
    paintLogo(quads, layout, logo, isOriginalLogo);
    for (const auto& [index, action] : std::views::enumerate(frame.items)) {
        paintItemRow(painter, layout, faces[MainMenuFont::Item], layout.item(nx::narrow<std::size_t>(index)),
                     itemNameOf(texts, action), frame.selected == action);
    }
}

/// The picture of `path` among the files of `vfs`; none, logged, when it cannot be read.
[[nodiscard]] auto readPicture(const nx::assets::Vfs& vfs, std::string_view path)
    -> std::optional<nx::assets::RgbaImage> {
    const auto file = nx::assets::VfsPath::parseGoldsrc(path);
    if (!file) {
        nx::log::warning(kLog, "the original logo {:?} is no path: {}", path, nx::describe(file.error()));
        return std::nullopt;
    }
    const auto bytes = vfs.readAll(*file);
    if (!bytes) {
        nx::log::info(kLog, "the main menu shows the project's logo: {}", nx::describe(bytes.error()));
        return std::nullopt;
    }
    auto picture = nx::assets::readTga(*bytes);
    if (!picture) {
        nx::log::warning(kLog, "the original logo {:?} is no picture: {}", path, nx::describe(picture.error()));
        return std::nullopt;
    }
    return std::move(*picture);
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
    if (!logo && look->originalLogo) {
        if (const auto picture = readPicture(vfs, *look->originalLogo)) {
            auto loaded = screen.loadImage(commands, picture->pixels, picture->width, picture->height,
                                           "mainMenu.originalLogo");
            if (!loaded) {
                return nx::fail(MenuPanelError::GpuFailed, *look->originalLogo, std::move(loaded.error()));
            }
            logo.emplace(std::move(*loaded));
            isOriginalLogo = true;
        }
    }
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
        // The original's fonts where the system has them, else the project's.
        auto face = look->originalFont ? makeMenuFace(fonts, screen, commands, *look->originalFont,
                                                      kFontSizes[font], screenHeight, "mainMenu")
                                       : nx::fail(MenuPanelError::NoFont, std::string{"no original font"});
        if (!face && face.error().code == MenuPanelError::NoFont) {
            face = makeMenuFace(fonts, screen, commands, look->font, kFontSizes[font], screenHeight, "mainMenu");
        }
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
                     paintMenu(laidOut, painter, layout, textFaces(), *texts, frame, size, *logo,
                               isOriginalLogo);
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
                            paintLogo(laidOut, layout, *logo, isOriginalLogo);
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
        // Stretched over the screen whole, as the original's.
        quads.push_back({.image = &*background,
                         .source = {.width = background->width(), .height = background->height()},
                         .target = {.width = static_cast<float>(size.width),
                                    .height = static_cast<float>(size.height)},
                         .filter = nx::render::ScreenFilter::Linear});
    }
}

} // namespace cs
