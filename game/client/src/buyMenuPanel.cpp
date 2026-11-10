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
using TextFaces = nx::EnumArray<BuyMenuFont, nx::ui::TextFace>;

constexpr nx::log::Category kLog{"client"};

// The colors, besides the menus' (cs.client:menuDesign): what warns, and each team's accent, the
// Terrorists' the menus' gold.
constexpr ScreenColor kDanger{.red = 244, .green = 101, .blue = 79};
constexpr ScreenColor kDangerGlass{.red = 60, .green = 22, .blue = 18, .alpha = 216};
constexpr nx::EnumArray<Team, ScreenColor> kAccents{
    nx::EnumArray<Team, ScreenColor>::Storage{kMenuGold, ScreenColor{.red = 103, .green = 184, .blue = 246}}};
constexpr nx::EnumArray<Team, ScreenColor> kOnAccents{
    nx::EnumArray<Team, ScreenColor>::Storage{kOnMenuGold, ScreenColor{.red = 4, .green = 19, .blue = 31}}};
constexpr std::uint8_t kWholePicture = 255;
constexpr std::uint8_t kTooDearPicture = 107;
constexpr std::uint8_t kTabPicture = 128;

constexpr nx::EnumArray<BuyMenuFont, MenuFontSize> kFontSizes{
    nx::EnumArray<BuyMenuFont, MenuFontSize>::Storage{
        MenuFontSize{.units = 56.0F, .isStrong = true}, MenuFontSize{.units = 32.0F, .isStrong = false},
        MenuFontSize{.units = 25.0F, .isStrong = true}, MenuFontSize{.units = 21.0F, .isStrong = false},
        MenuFontSize{.units = 16.0F, .isStrong = false}}};

// What it writes.
constexpr std::string_view kMenuName = "BUY";
constexpr nx::EnumArray<Team, std::string_view> kTeamNames{
    nx::EnumArray<Team, std::string_view>::Storage{"TERRORISTS", "COUNTER-TERRORISTS"}};
constexpr nx::EnumArray<BuyTab, std::string_view> kTabNames{nx::EnumArray<BuyTab, std::string_view>::Storage{
    "PISTOLS", "SHOTGUNS", "SMGS", "RIFLES", "MACHINE GUN", "EQUIPMENT"}};
constexpr nx::EnumArray<BuyTab, std::string_view> kKindNames{nx::EnumArray<BuyTab, std::string_view>::Storage{
    "PISTOL", "SHOTGUN", "SUBMACHINE GUN", "RIFLE", "MACHINE GUN", "EQUIPMENT"}};
constexpr nx::EnumArray<Buyers, std::string_view> kBuyersNames{
    nx::EnumArray<Buyers, std::string_view>::Storage{"TERRORISTS ONLY", "COUNTER-TERRORISTS ONLY",
                                                     "BOTH TEAMS"}};
constexpr std::string_view kOwned = "OWNED";
constexpr std::string_view kWorn = "WORN";
constexpr std::string_view kBuy = "BUY";
constexpr std::string_view kNone = "None";
constexpr std::string_view kWithHelmet = "with helmet";
constexpr std::string_view kMiddleDot = "\u00B7";

// What a stat's bar is full at: the most of Counter-Strike's weapons, about.
constexpr float kMostDamage = 115.0F;
constexpr float kMostRoundsAMinute = 900.0F;
constexpr float kMostMoveSpeed = 260.0F;

constexpr std::uint32_t kSecondsAMinute = 60;
constexpr std::size_t kThousandDigits = 3;

// How long a notice shows, the last of it fading.
constexpr nx::Seconds kNoticeLife{2.0F};
constexpr nx::Seconds kNoticeFade{0.2F};

// In the design's units: what the layout leaves to what draws in it.
constexpr std::int32_t kAccentBar = 3;
constexpr std::int32_t kKeyBadge = 25;
constexpr std::int32_t kInsetLeft = 14;
constexpr std::int32_t kInsetRight = 10;
constexpr std::int32_t kCellInset = 16;
constexpr std::int32_t kTextGap = 12;
constexpr std::int32_t kBaselineOverTrack = 14;
constexpr std::int32_t kHeaderTextGap = 18;
constexpr std::int32_t kTabPictureWide = 72;
constexpr std::int32_t kTabPictureTall = 40;
constexpr std::int32_t kDetailInset = 20;
constexpr std::int32_t kDetailTop = 18;
constexpr std::int32_t kStatGap = 16;
constexpr std::int32_t kStatBarGap = 5;
constexpr std::int32_t kStatBarTall = 3;
constexpr std::int32_t kSectionGap = 22;
constexpr std::int32_t kButtonInset = 16;
constexpr std::int32_t kNoteGap = 10;
constexpr std::int32_t kTilePictureWide = 96;
constexpr std::int32_t kTilePictureTall = 60;
constexpr std::int32_t kHintGap = 16;
constexpr std::int32_t kBadgeTextGap = 7;
constexpr std::int32_t kNoticeOverMenu = 54;
constexpr std::int32_t kNoticeOverGame = 300;
constexpr std::int32_t kNoticePadX = 18;
constexpr std::int32_t kNoticePadY = 8;
/// What the room left is divided by to put a thing halfway into it.
constexpr std::int32_t kMiddleDivisor = 2;
/// A padding on each side of the notice: left and right, top and bottom.
constexpr std::int32_t kPaddedSides = 2;

/// `color`, as opaque as `share` of what it is.
[[nodiscard]] constexpr auto fadedBy(ScreenColor color, float share) noexcept -> ScreenColor {
    color.alpha = static_cast<std::uint8_t>(static_cast<float>(color.alpha) * share);
    return color;
}

/// Draws the menu's parts: rectangles, text and pictures of the screen, by the layout's design.
class Paint {
public:
    Paint(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size, const BuyMenuLayout& layout,
          const TextFaces& faces,
          const std::map<std::string, std::optional<nx::render::ScreenImage>, std::less<>>& pictures,
          Team team)
        : output{&quads}, painter{quads, size}, design{&layout}, textFaces{&faces}, loaded{&pictures},
          teamAccent{kAccents[team]}, onTeamAccent{kOnAccents[team]} {}

    [[nodiscard]] inline auto accent() const noexcept -> ScreenColor { return teamAccent; }
    [[nodiscard]] inline auto onAccent() const noexcept -> ScreenColor { return onTeamAccent; }
    [[nodiscard]] inline auto layout() const noexcept -> const BuyMenuLayout& { return *design; }
    [[nodiscard]] inline auto px(std::int32_t units) const noexcept -> nx::Pixels {
        return design->pixels(units);
    }

    void fill(const nx::ui::PixelRect& rect, ScreenColor color) { painter.fill(rect, color); }

    /// `rect`'s edges in `color` (paintMenuOutline).
    void outline(const nx::ui::PixelRect& rect, ScreenColor color) { paintMenuOutline(painter, rect, color); }

    /// `rect` filled with `color`, a bar in the team's accent on its left when `isLit`.
    void cell(const nx::ui::PixelRect& rect, ScreenColor color, bool isLit) {
        fill(rect, color);
        if (isLit) {
            painter.fill({.x = rect.x, .y = rect.y, .wide = px(kAccentBar), .tall = rect.tall}, teamAccent);
        }
    }

    [[nodiscard]] auto lineOf(BuyMenuFont font) const noexcept -> nx::Pixels {
        return (*textFaces)[font].atlas->lineHeight();
    }
    [[nodiscard]] auto ascentOf(BuyMenuFont font) const noexcept -> nx::Pixels {
        return (*textFaces)[font].atlas->ascent();
    }
    [[nodiscard]] auto widthOf(BuyMenuFont font, std::string_view text) const -> nx::Pixels {
        return nx::ui::widthOf(*(*textFaces)[font].atlas, text);
    }
    [[nodiscard]] auto wrapped(BuyMenuFont font, std::string_view text, nx::Pixels wide) const
        -> std::vector<std::string> {
        return nx::ui::wrapText(*(*textFaces)[font].atlas, text, wide);
    }

    /// `text` with its line's top at `y` and its pen from `x`. Returns where the pen ends.
    auto write(BuyMenuFont font, std::string_view text, nx::Pixels x, nx::Pixels y, ScreenColor color)
        -> nx::Pixels {
        return painter.text((*textFaces)[font], text, {.x = x, .y = y}, color);
    }
    /// `text` ending at `end`.
    void writeEnding(BuyMenuFont font, std::string_view text, nx::Pixels end, nx::Pixels y,
                     ScreenColor color) {
        std::ignore = write(font, text, end - widthOf(font, text), y, color);
    }

    /// How wide the badge of `key` is: square, or wider for a key of a long name.
    [[nodiscard]] auto keyBadgeWide(std::string_view key) const -> nx::Pixels {
        return std::max(px(kKeyBadge), widthOf(BuyMenuFont::Caption, key) + px(kBadgeTextGap));
    }

    /// A key's badge at `x`, in the middle of `rect`'s height: lit, it is filled with the team's accent.
    /// Returns where it ends.
    auto keyBadge(std::string_view key, nx::Pixels x, const nx::ui::PixelRect& rect, bool isLit)
        -> nx::Pixels {
        constexpr BuyMenuFont kFont = BuyMenuFont::Caption;
        const nx::Pixels tall = px(kKeyBadge);
        const nx::Pixels textWide = widthOf(kFont, key);
        const nx::Pixels wide = keyBadgeWide(key);
        const nx::ui::PixelRect badge{
            .x = x, .y = rect.y + ((rect.tall - tall) / 2), .wide = wide, .tall = tall};
        if (isLit) {
            fill(badge, teamAccent);
        } else {
            outline(badge, kMenuLine);
        }
        std::ignore = write(kFont, key, x + ((wide - textWide) / 2), badge.y + ((tall - lineOf(kFont)) / 2),
                            isLit ? onTeamAccent : kMenuInk);
        return x + wide;
    }

    /// The picture of the file `path`, whole and as large as fits in the middle of `box`, as opaque as
    /// `alpha`; nothing for one that could not be read.
    void picture(std::string_view path, const nx::ui::PixelRect& box, std::uint8_t alpha) {
        const auto found = loaded->find(path);
        if (found == loaded->end() || !found->second) {
            return;
        }
        const nx::render::ScreenImage& image = *found->second;
        const float wide = static_cast<float>(image.width());
        const float tall = static_cast<float>(image.height());
        // Drawn in fractions of a pixel, scaled to fit.
        const auto boxX = static_cast<float>(box.x.value());
        const auto boxY = static_cast<float>(box.y.value());
        const auto boxWide = static_cast<float>(box.wide.value());
        const auto boxTall = static_cast<float>(box.tall.value());
        const float fit = std::min(boxWide / wide, boxTall / tall);
        output->push_back(
            {.image = &image,
             .source = {.x = 0, .y = 0, .width = image.width(), .height = image.height()},
             .target = {.x = boxX + ((boxWide - (wide * fit)) / static_cast<float>(kMiddleDivisor)),
                        .y = boxY + ((boxTall - (tall * fit)) / static_cast<float>(kMiddleDivisor)),
                        .width = wide * fit,
                        .height = tall * fit},
             .color = {.alpha = alpha},
             .blend = nx::render::ScreenBlend::Translucent,
             .filter = nx::render::ScreenFilter::Linear});
    }

private:
    std::vector<nx::render::ScreenQuad>* output;
    nx::ui::PanelPainter painter;
    const BuyMenuLayout* design;
    const TextFaces* textFaces;
    const std::map<std::string, std::optional<nx::render::ScreenImage>, std::less<>>* loaded;
    ScreenColor teamAccent;
    ScreenColor onTeamAccent;
};

/// What the money of `frame` will be after what is selected, or how far short it falls; empty when nothing to
/// buy is selected.
[[nodiscard]] auto moneyAfterText(const BuyMenuFrame& frame) -> std::string {
    if (!frame.detail || frame.detail->entry.state == BuyItemState::Owned) {
        return {};
    }
    const nx::Money price = frame.detail->entry.price;
    return frame.money < price ? std::format("{} short", dollarsText(price - frame.money))
                               : std::format("{} after buying", dollarsText(frame.money - price));
}

void paintHeader(Paint& paint, const BuyMenuFrame& frame) {
    const nx::ui::PixelRect& header = paint.layout().header();
    const nx::ui::PixelRect track = paint.layout().timeBar();
    const nx::Pixels baseline = track.y - paint.px(kBaselineOverTrack);
    const auto topFor = [&](BuyMenuFont font) {
        return baseline - paint.ascentOf(font);
    };

    nx::Pixels pen =
        paint.write(BuyMenuFont::Headline, kMenuName, header.x, topFor(BuyMenuFont::Headline), kMenuInk);
    pen = paint.write(BuyMenuFont::Body, kTeamNames[frame.team], pen + paint.px(kHeaderTextGap),
                      topFor(BuyMenuFont::Body), paint.accent());
    pen = paint.write(BuyMenuFont::Body, "Time to buy ", pen + paint.px(kHeaderTextGap),
                      topFor(BuyMenuFont::Body), kMenuInkSoft);
    std::ignore = paint.write(BuyMenuFont::Body, clockText(frame.secondsLeft), pen, topFor(BuyMenuFont::Body),
                              kMenuInk);

    const nx::Pixels end = header.x + header.wide;
    const nx::Pixels moneyTop = topFor(BuyMenuFont::Headline);
    paint.writeEnding(BuyMenuFont::Headline, dollarsText(frame.money), end, moneyTop, paint.accent());
    const bool isShort = frame.detail && frame.detail->entry.state == BuyItemState::TooDear;
    paint.writeEnding(BuyMenuFont::Body, moneyAfterText(frame), end,
                      moneyTop - paint.lineOf(BuyMenuFont::Body), isShort ? kDanger : kMenuInkSoft);

    paint.fill(track, kMenuLine);
}

void paintRail(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    for (const auto& [index, listed] : std::views::enumerate(frame.tabs)) {
        const nx::ui::PixelRect rect = layout.tab(nx::narrow<std::size_t>(index));
        const bool isShownTab = listed.tab == frame.tab;
        paint.cell(rect, isShownTab ? kMenuCellLit : kMenuCell, isShownTab);
        const nx::Pixels pen =
            paint.keyBadge(std::to_string(kBuyTabKeys[listed.tab]), rect.x + paint.px(kInsetLeft), rect,
                           isShownTab && frame.isArmed);
        std::ignore = paint.write(BuyMenuFont::Body, kTabNames[listed.tab], pen + paint.px(kTextGap),
                                  rect.y + ((rect.tall - paint.lineOf(BuyMenuFont::Body)) / 2),
                                  isShownTab ? kMenuInk : kMenuInkSoft);
        const nx::Pixels wide = paint.px(kTabPictureWide);
        const nx::Pixels tall = paint.px(kTabPictureTall);
        paint.picture(listed.picture,
                      {.x = rect.x + rect.wide - paint.px(kInsetRight) - wide,
                       .y = rect.y + ((rect.tall - tall) / 2),
                       .wide = wide,
                       .tall = tall},
                      isShownTab ? kWholePicture : kTabPicture);
    }
    for (const auto& [index, row] : std::views::enumerate(frame.ammo)) {
        const nx::ui::PixelRect rect = layout.ammoRow(nx::narrow<std::size_t>(index), frame.tabs.size());
        paint.cell(rect, kMenuCell, false);
        const nx::Pixels pen =
            paint.keyBadge(std::to_string(row.key), rect.x + paint.px(kInsetLeft), rect, false) +
            paint.px(kTextGap);
        const nx::Pixels top =
            rect.y + ((rect.tall - paint.lineOf(BuyMenuFont::Body) - paint.lineOf(BuyMenuFont::Caption)) / 2);
        std::ignore =
            paint.write(BuyMenuFont::Body, row.key == kPrimaryAmmoKey ? "Primary ammo" : "Pistol ammo", pen,
                        top, kMenuInk);
        const std::string facts =
            !row.ammo ? std::string{"No weapon"}
            : row.state == BuyItemState::Owned
                ? std::format("{} {}/{} {} full", row.ammo->name, row.ammo->carried, row.ammo->most,
                              kMiddleDot)
                : std::format("{} {}/{} {} +{} {}", row.ammo->name, row.ammo->carried, row.ammo->most,
                              kMiddleDot, row.ammo->boxRounds, dollarsText(row.ammo->boxPrice));
        std::ignore = paint.write(BuyMenuFont::Caption, facts, pen, top + paint.lineOf(BuyMenuFont::Body),
                                  row.state == BuyItemState::TooDear ? kDanger : kMenuInkSoft);
    }
}

void paintGrid(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    const bool hasDetail = frame.detail.has_value();
    for (const auto& [index, entry] : std::views::enumerate(frame.entries)) {
        const nx::ui::PixelRect rect = layout.cell(nx::narrow<std::size_t>(index), frame.entries.size());
        const bool isSelected = hasDetail && frame.detail->entry.item == entry.item;
        paint.cell(rect, isSelected ? kMenuCellLit : kMenuCell, isSelected);

        const nx::Pixels inset = paint.px(kCellInset);
        const nx::Pixels room = rect.wide - (2 * inset);
        const bool isOwned = entry.state == BuyItemState::Owned;
        const BuyMenuFont priceFont = isOwned ? BuyMenuFont::Caption : BuyMenuFont::Body;
        const std::string price = !isOwned                                        ? dollarsText(entry.price)
                                  : std::holds_alternative<ArmorKind>(entry.item) ? std::string{kWorn}
                                                                                  : std::string{kOwned};
        const std::vector<std::string> nameRows = paint.wrapped(BuyMenuFont::Body, entry.name, room);
        const bool isApart = isPriceApart(nameRows.size(), paint.widthOf(BuyMenuFont::Body, entry.name),
                                          paint.widthOf(priceFont, price), paint.px(kTextGap), room);
        const nx::Pixels line = paint.lineOf(BuyMenuFont::Body);
        const auto rows = nx::narrow<std::int32_t>(nameRows.size()) + (isApart ? 1 : 0);
        const nx::Pixels rowY = rect.y + rect.tall - inset - (rows * line);
        const bool isTooDear = entry.state == BuyItemState::TooDear;
        paint.picture(entry.picture,
                      {.x = rect.x + inset,
                       .y = rect.y + inset,
                       .wide = rect.wide - (2 * inset),
                       .tall = rowY - rect.y - inset},
                      isTooDear ? kTooDearPicture : kWholePicture);

        const BuyMenuFont digitFont = frame.isArmed ? BuyMenuFont::Strong : BuyMenuFont::Caption;
        std::ignore = paint.write(digitFont, std::to_string(entry.key), rect.x + inset, rect.y + (inset / 2),
                                  frame.isArmed ? paint.accent() : kMenuInkFaint);

        nx::Pixels y = rowY;
        for (const std::string& row : nameRows) {
            std::ignore =
                paint.write(BuyMenuFont::Body, row, rect.x + inset, y, isTooDear ? kMenuInkFaint : kMenuInk);
            y += line;
        }
        // On the name's last row, or a row of its own: at the foot of the cell either way.
        const nx::Pixels priceY = rect.y + rect.tall - inset - paint.lineOf(priceFont);
        paint.writeEnding(priceFont, price, rect.x + rect.wide - inset, priceY,
                          isOwned     ? paint.accent()
                          : isTooDear ? kDanger
                                      : kMenuInkSoft);
    }
}

/// A stat of the item selected at `y`: its name, its value and a bar of it. Returns where it ends.
auto paintStat(Paint& paint, const nx::ui::PixelRect& inner, nx::Pixels y, std::string_view name,
               std::string_view value, float share) -> nx::Pixels {
    std::ignore = paint.write(BuyMenuFont::Body, name, inner.x, y, kMenuInkSoft);
    paint.writeEnding(BuyMenuFont::Body, value, inner.x + inner.wide, y, kMenuInk);
    const nx::Pixels barY = y + paint.lineOf(BuyMenuFont::Body) + paint.px(kStatBarGap);
    const nx::Pixels tall = paint.px(kStatBarTall);
    paint.fill({.x = inner.x, .y = barY, .wide = inner.wide, .tall = tall}, kMenuLine);
    paint.fill({.x = inner.x,
                .y = barY,
                .wide = nx::Pixels{static_cast<std::int32_t>(static_cast<float>(inner.wide.value()) *
                                                             std::clamp(share, 0.0F, 1.0F))},
                .tall = tall},
               kMenuInk);
    return barY + tall + paint.px(kStatGap);
}

/// What a gun tells of itself, from `y`.
void paintGunFacts(Paint& paint, const nx::ui::PixelRect& inner, nx::Pixels y, const BuyGunFacts& gun) {
    y = paintStat(paint, inner, y, "Damage", std::to_string(gun.damage),
                  static_cast<float>(gun.damage) / kMostDamage);
    y = paintStat(paint, inner, y, "Rate of fire",
                  gun.isSemiAutomatic ? std::format("{} / min {} semi", gun.roundsAMinute, kMiddleDot)
                                      : std::format("{} / min", gun.roundsAMinute),
                  static_cast<float>(gun.roundsAMinute) / kMostRoundsAMinute);
    y = paintStat(paint, inner, y, "Through armor", shareText(gun.throughArmor), gun.throughArmor);
    y = paintStat(paint, inner, y, "Move speed", std::format("{} u/s", std::lround(gun.moveSpeed.value())),
                  gun.moveSpeed.value() / kMostMoveSpeed);

    const std::array facts{std::pair{std::string_view{"CLIP"}, std::to_string(gun.clip)},
                           std::pair{std::string_view{"CARRIED"}, std::to_string(gun.carried)},
                           std::pair{std::string_view{"RELOAD"}, std::format("{:g} s", gun.reload.count())}};
    const nx::Pixels column = inner.wide / nx::narrow<std::int32_t>(facts.size());
    for (const auto& [index, fact] : std::views::enumerate(facts)) {
        const nx::Pixels x = inner.x + (nx::narrow<std::int32_t>(index) * column);
        std::ignore = paint.write(BuyMenuFont::Caption, fact.first, x, y, kMenuInkFaint);
        std::ignore = paint.write(BuyMenuFont::Strong, fact.second, x, y + paint.lineOf(BuyMenuFont::Caption),
                                  kMenuInk);
    }
    y += paint.lineOf(BuyMenuFont::Caption) + paint.lineOf(BuyMenuFont::Strong) + paint.px(kStatGap);
    paint.fill({.x = inner.x, .y = y, .wide = inner.wide, .tall = kMenuLineThick}, kMenuLine);
    y += paint.px(kStatGap);
    std::ignore = paint.write(BuyMenuFont::Body, gun.ammoName, inner.x, y, kMenuInkSoft);
    paint.writeEnding(BuyMenuFont::Body,
                      std::format("{} for {} rounds", dollarsText(gun.boxPrice), gun.boxRounds),
                      inner.x + inner.wide, y, kMenuInkSoft);
}

void paintDetail(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    const nx::ui::PixelRect& panel = layout.detail();
    paint.fill(panel, kMenuGlass);
    paint.outline(panel, kMenuLine);
    if (!frame.detail) {
        return;
    }
    const BuyDetail& detail = *frame.detail;
    const nx::Pixels inset = paint.px(kDetailInset);
    const nx::ui::PixelRect inner{
        .x = panel.x + inset, .y = panel.y, .wide = panel.wide - (2 * inset), .tall = panel.tall};

    nx::Pixels y = panel.y + paint.px(kDetailTop);
    const std::string kind = detail.buyers ? std::format("{} {} {}", kKindNames[detail.tab], kMiddleDot,
                                                         kBuyersNames[*detail.buyers])
                                           : std::string{kKindNames[detail.tab]};
    std::ignore = paint.write(BuyMenuFont::Caption, kind, inner.x, y, paint.accent());
    y += paint.lineOf(BuyMenuFont::Caption);
    std::ignore = paint.write(BuyMenuFont::Title, detail.entry.name, inner.x, y, kMenuInk);
    y += paint.lineOf(BuyMenuFont::Title) + paint.px(kSectionGap);

    if (detail.gun) {
        paintGunFacts(paint, inner, y, *detail.gun);
    }
    for (const std::string& row : paint.wrapped(BuyMenuFont::Body, detail.about, inner.wide)) {
        std::ignore = paint.write(BuyMenuFont::Body, row, inner.x, y, kMenuInkSoft);
        y += paint.lineOf(BuyMenuFont::Body);
    }

    const nx::ui::PixelRect button = layout.buyButton();
    const bool isForSale = detail.entry.state == BuyItemState::ForSale;
    const std::string note =
        detail.entry.state == BuyItemState::TooDear ? std::string{buyRefusalText(BuyRefusal::NotEnoughMoney)}
        : isForSale && !detail.replaces.empty()
            ? std::format("Replaces your {}. The rounds you carry stay.", detail.replaces)
            : std::string{};
    const std::vector<std::string> noteRows = paint.wrapped(BuyMenuFont::Caption, note, inner.wide);
    nx::Pixels noteY = button.y - paint.px(kNoteGap) -
                       (nx::narrow<std::int32_t>(noteRows.size()) * paint.lineOf(BuyMenuFont::Caption));
    for (const std::string& row : noteRows) {
        std::ignore = paint.write(BuyMenuFont::Caption, row, inner.x, noteY,
                                  detail.entry.state == BuyItemState::TooDear ? kDanger : kMenuInkSoft);
        noteY += paint.lineOf(BuyMenuFont::Caption);
    }

    paint.fill(button, isForSale ? paint.accent() : kMenuCell);
    const ScreenColor color = isForSale ? paint.onAccent() : kMenuInkFaint;
    const nx::Pixels textY = button.y + ((button.tall - paint.lineOf(BuyMenuFont::Strong)) / 2);
    const bool isOwned = detail.entry.state == BuyItemState::Owned;
    const std::string_view owned = std::holds_alternative<ArmorKind>(detail.entry.item) ? kWorn : kOwned;
    std::ignore = paint.write(BuyMenuFont::Strong, isOwned ? owned : kBuy, button.x + paint.px(kButtonInset),
                              textY, color);
    if (!isOwned) {
        paint.writeEnding(BuyMenuFont::Strong, dollarsText(detail.entry.price),
                          button.x + button.wide - paint.px(kButtonInset), textY, color);
    }
}

/// A tile of what the player carries: its name, what it says and its picture, when it has one.
void paintTile(Paint& paint, const nx::ui::PixelRect& tile, std::string_view name, std::string_view text,
               std::string_view picture, bool isCarried) {
    paint.fill(tile, kMenuGlass);
    paint.outline(tile, kMenuLine);
    nx::Pixels x = tile.x + paint.px(kInsetLeft);
    if (!picture.empty()) {
        const nx::Pixels tall = paint.px(kTilePictureTall);
        paint.picture(picture,
                      {.x = x,
                       .y = tile.y + ((tile.tall - tall) / 2),
                       .wide = paint.px(kTilePictureWide),
                       .tall = tall},
                      kWholePicture);
        x += paint.px(kTilePictureWide) + paint.px(kInsetRight);
    }
    const nx::Pixels top =
        tile.y + ((tile.tall - paint.lineOf(BuyMenuFont::Caption) - paint.lineOf(BuyMenuFont::Body)) / 2);
    std::ignore = paint.write(BuyMenuFont::Caption, name, x, top, kMenuInkFaint);
    std::ignore = paint.write(BuyMenuFont::Body, text, x, top + paint.lineOf(BuyMenuFont::Caption),
                              isCarried ? kMenuInk : kMenuInkFaint);
}

void paintCarried(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    const auto gunTile = [&](std::size_t index, std::string_view name,
                             const std::optional<BuyCarriedGun>& gun) {
        paintTile(paint, layout.carriedTile(index), name,
                  gun ? std::format("{} | {}", gun->clip, gun->carried) : std::string{kNone},
                  gun ? gun->picture : std::string_view{}, gun.has_value());
    };
    gunTile(0, "PRIMARY", frame.carried.primary);
    gunTile(1, "PISTOL", frame.carried.pistol);
    const BuyCarried& carried = frame.carried;
    const bool wearsArmor = carried.armorKind != ArmorKind::None;
    const std::string armor =
        !wearsArmor ? std::string{kNone}
        : carried.armorKind == ArmorKind::KevlarAndHelmet
            ? std::format("{} {} {}", armorDigits(carried.armor), kMiddleDot, kWithHelmet)
            : armorDigits(carried.armor);
    paintTile(paint, layout.carriedTile(2), "ARMOR", armor,
              wearsArmor ? carried.armorPicture : std::string_view{}, wearsArmor);

    if (frame.again) {
        const nx::ui::PixelRect tile = layout.againTile();
        paint.fill(tile, kMenuGlass);
        paint.outline(tile, kMenuLine);
        const nx::Pixels pen =
            paint.keyBadge("R", tile.x + paint.px(kInsetLeft), tile, false) + paint.px(kTextGap);
        const std::string cost = dollarsText(frame.again->cost);
        const nx::Pixels end = tile.x + tile.wide - paint.px(kInsetLeft);
        const nx::Pixels room = end - paint.widthOf(BuyMenuFont::Strong, cost) - paint.px(kTextGap) - pen;
        std::string names;
        for (const std::string_view item : frame.again->items) {
            names += names.empty() ? std::string{item} : std::format(" {} {}", kMiddleDot, item);
        }
        const std::vector<std::string> rows = paint.wrapped(BuyMenuFont::Caption, names, room);
        const nx::Pixels line = paint.lineOf(BuyMenuFont::Caption);
        nx::Pixels y =
            tile.y + ((tile.tall - ((nx::narrow<std::int32_t>(rows.size()) + 1) * line)) / kMiddleDivisor);
        std::ignore = paint.write(BuyMenuFont::Caption, "BUY AGAIN", pen, y, kMenuInkFaint);
        for (const std::string& row : rows) {
            y += line;
            std::ignore = paint.write(BuyMenuFont::Caption, row, pen, y, kMenuInk);
        }
        paint.writeEnding(BuyMenuFont::Strong, cost, end,
                          tile.y + ((tile.tall - paint.lineOf(BuyMenuFont::Strong)) / 2),
                          frame.money < frame.again->cost ? kDanger : paint.accent());
    }

    // The keys, from the right.
    const nx::ui::PixelRect& row = layout.carried();
    nx::Pixels end = row.x + row.wide;
    for (const auto& [key, what] :
         {std::pair{"B", "Close"}, std::pair{"Esc", "Back"}, std::pair{"Enter", "Buy"}}) {
        end -= paint.widthOf(BuyMenuFont::Caption, what);
        std::ignore = paint.write(BuyMenuFont::Caption, what, end,
                                  row.y + ((row.tall - paint.lineOf(BuyMenuFont::Caption)) / kMiddleDivisor),
                                  kMenuInkSoft);
        end -= paint.px(kBadgeTextGap) + paint.keyBadgeWide(key);
        std::ignore = paint.keyBadge(key, end, row, false);
        end -= paint.px(kHintGap);
    }
}

} // namespace

auto dollarsText(nx::Money money) -> std::string {
    std::string digits = moneyDigits(money);
    for (std::size_t at = digits.size(); at > kThousandDigits; at -= kThousandDigits) {
        digits.insert(at - kThousandDigits, 1, ',');
    }
    return std::format("${}", digits);
}

auto clockText(std::uint32_t seconds) -> std::string {
    return std::format("{}:{:02}", seconds / kSecondsAMinute, seconds % kSecondsAMinute);
}

auto shareText(float share) -> std::string {
    constexpr float kTenthsOfAPercent = 1000.0F;
    constexpr float kTenthsAPercent = 10.0F;
    return std::format("{:g}%", std::round(share * kTenthsOfAPercent) / kTenthsAPercent);
}

BuyMenuPanel::BuyMenuPanel(const BuyMenuContent& sold) noexcept : content{sold} {}

auto BuyMenuPanel::makeFaces(nx::ui::FontCatalog& fonts, nx::render::ScreenRenderer& screen,
                             nx::rhi::CommandList& commands, std::uint32_t screenHeight)
    -> nx::Result<void, MenuPanelError> {
    for (const BuyMenuFont font : nx::EnumArray<BuyMenuFont, MenuFontSize>::keys()) {
        auto face = makeMenuFace(fonts, screen, commands, content.look->font, kFontSizes[font], screenHeight,
                                 "buyMenu");
        if (!face) {
            return std::unexpected{std::move(face.error())};
        }
        faces[font].emplace(std::move(*face));
    }
    facesHeight = screenHeight;
    return {};
}

auto BuyMenuPanel::loadPicture(std::string_view path, const nx::assets::Vfs& vfs,
                               nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, MenuPanelError> {
    if (path.empty() || pictures.contains(path)) {
        return {};
    }
    auto& picture = pictures[std::string{path}];
    const auto file = nx::assets::VfsPath::parse(path);
    auto bytes = file ? vfs.readAll(*file) : nx::Result<std::vector<std::byte>, nx::assets::VfsError>{};
    auto image = file && bytes ? nx::assets::readTga(*bytes)
                               : nx::Result<nx::assets::RgbaImage, nx::assets::TgaError>{};
    if (!file || !bytes || !image) {
        nx::log::warning(kLog, "the buy menu shows no picture of {:?}: it cannot be read", path);
        return {};
    }
    auto loaded = screen.loadImage(commands, image->pixels, image->width, image->height, path);
    if (!loaded) {
        return nx::fail(MenuPanelError::GpuFailed, std::string{path}, std::move(loaded.error()));
    }
    picture.emplace(std::move(*loaded));
    return {};
}

auto BuyMenuPanel::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                        nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                        std::uint32_t screenHeight) -> nx::Result<void, MenuPanelError> {
    if (screenHeight != facesHeight) {
        if (auto made = makeFaces(fonts, screen, commands, screenHeight); !made) {
            return made;
        }
    }
    for (const BuyMenuArmor& armor : content.look->armor) {
        if (auto loaded = loadPicture(armor.picture, vfs, screen, commands); !loaded) {
            return loaded;
        }
    }
    for (const Weapon& weapon : content.weapons->weapons()) {
        if (weapon.purchase && weapon.purchase->menu) {
            if (auto loaded = loadPicture(weapon.purchase->menu->picture, vfs, screen, commands); !loaded) {
                return loaded;
            }
        }
    }
    return {};
}

void BuyMenuPanel::showNotice(std::string_view text, nx::Seconds now) {
    notice = Notice{.text = std::string{text}, .shownAt = now};
}

void BuyMenuPanel::appendBackdrop(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) {
    nx::ui::PanelPainter{quads, size}.fill({.wide = nx::Pixels{nx::narrow<std::int32_t>(size.width)},
                                            .tall = nx::Pixels{nx::narrow<std::int32_t>(size.height)}},
                                           kMenuBackdrop);
}

auto BuyMenuPanel::textFaces() const noexcept -> TextFaces {
    TextFaces shownFaces;
    for (const BuyMenuFont font : TextFaces::keys()) {
        shownFaces[font] = faces[font]->textFace();
    }
    return shownFaces;
}

void BuyMenuPanel::append(std::vector<nx::render::ScreenQuad>& quads, const BuyMenuFrame& frame,
                          float timeShare, nx::rhi::Extent2d size) {
    if (!isLoaded()) {
        return;
    }
    shown.append(quads, Shown{.frame = frame, .size = size},
                 [&](std::vector<nx::render::ScreenQuad>& laidOut) {
                     const BuyMenuLayout layout{size};
                     const TextFaces shownFaces = textFaces();
                     Paint paint{laidOut, size, layout, shownFaces, pictures, frame.team};
                     paintHeader(paint, frame);
                     paintRail(paint, frame);
                     paintGrid(paint, frame);
                     paintDetail(paint, frame);
                     paintCarried(paint, frame);
                 });
    // The line's fill each frame, over its track: nothing else about the menu changes as often.
    const nx::ui::PixelRect bar = BuyMenuLayout{size}.timeBar();
    const BuyBarFill fill = barFillOf(bar.wide, timeShare);
    const ScreenColor accent = kAccents[frame.team];
    nx::ui::PanelPainter painter{quads, size};
    painter.fill({.x = bar.x, .y = bar.y, .wide = fill.whole, .tall = bar.tall}, accent);
    painter.fill({.x = bar.x + fill.whole, .y = bar.y, .wide = kMenuLineThick, .tall = bar.tall},
                 fadedBy(accent, fill.nextPixel));
}

void BuyMenuPanel::appendNotice(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size,
                                nx::Seconds now, bool isMenuOpen) {
    if (!notice || !isLoaded()) {
        return;
    }
    const nx::Seconds age = now - notice->shownAt;
    if (age >= kNoticeLife) {
        notice.reset();
        return;
    }
    const float opacity = std::clamp((kNoticeLife - age) / kNoticeFade, 0.0F, 1.0F);
    const BuyMenuLayout layout{size};
    const nx::ui::GlyphAtlas& atlas = faces[BuyMenuFont::Strong]->atlas;
    const nx::Pixels wide = nx::ui::widthOf(atlas, notice->text);
    const nx::Pixels padX = layout.pixels(kNoticePadX);
    const nx::Pixels padY = layout.pixels(kNoticePadY);
    const nx::Pixels x = (nx::Pixels{nx::narrow<std::int32_t>(size.width)} - wide) / kMiddleDivisor;
    const nx::Pixels y = layout.pixels(isMenuOpen ? kNoticeOverMenu : kNoticeOverGame);
    const nx::Pixels tall = atlas.lineHeight() + (kPaddedSides * padY);
    nx::ui::PanelPainter painter{quads, size};
    painter.fill({.x = x - padX, .y = y - padY, .wide = wide + (kPaddedSides * padX), .tall = tall},
                 fadedBy(kDangerGlass, opacity));
    painter.fill({.x = x - padX, .y = y - padY, .wide = layout.pixels(kAccentBar), .tall = tall},
                 fadedBy(kDanger, opacity));
    std::ignore = painter.text(faces[BuyMenuFont::Strong]->textFace(), notice->text, {.x = x, .y = y},
                               fadedBy(kMenuInk, opacity)); // the last it draws
}

} // namespace cs
