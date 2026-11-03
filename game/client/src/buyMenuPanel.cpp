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

// The colors: warm white text, dark glass over the scene, and each team's accent.
constexpr ScreenColor kInk{.red = 244, .green = 240, .blue = 231};
constexpr ScreenColor kInkSoft{.red = 244, .green = 240, .blue = 231, .alpha = 168};
constexpr ScreenColor kInkFaint{.red = 244, .green = 240, .blue = 231, .alpha = 102};
constexpr ScreenColor kLine{.red = 255, .green = 255, .blue = 255, .alpha = 31};
constexpr ScreenColor kGlass{.red = 13, .green = 14, .blue = 16, .alpha = 199};
constexpr ScreenColor kCell{.red = 255, .green = 255, .blue = 255, .alpha = 13};
constexpr ScreenColor kCellLit{.red = 255, .green = 255, .blue = 255, .alpha = 28};
constexpr ScreenColor kBackdrop{.red = 6, .green = 7, .blue = 9, .alpha = 184};
constexpr ScreenColor kDanger{.red = 244, .green = 101, .blue = 79};
constexpr ScreenColor kDangerGlass{.red = 60, .green = 22, .blue = 18, .alpha = 216};
constexpr nx::EnumArray<Team, ScreenColor> kAccents{nx::EnumArray<Team, ScreenColor>::Storage{
    ScreenColor{.red = 242, .green = 179, .blue = 61}, ScreenColor{.red = 103, .green = 184, .blue = 246}}};
constexpr nx::EnumArray<Team, ScreenColor> kOnAccents{nx::EnumArray<Team, ScreenColor>::Storage{
    ScreenColor{.red = 27, .green = 19, .blue = 5}, ScreenColor{.red = 4, .green = 19, .blue = 31}}};
constexpr std::uint8_t kWholePicture = 255;
constexpr std::uint8_t kTooDearPicture = 107;
constexpr std::uint8_t kTabPicture = 128;

/// A size of text: its line in the design's units, 1080 lines tall, and its weight.
struct FontSize {
    float units = 0.0F;
    std::int32_t weight = 0;
};
constexpr nx::EnumArray<BuyMenuFont, FontSize> kFontSizes{nx::EnumArray<BuyMenuFont, FontSize>::Storage{
    FontSize{.units = 56.0F, .weight = 600}, FontSize{.units = 32.0F, .weight = 500},
    FontSize{.units = 25.0F, .weight = 600}, FontSize{.units = 21.0F, .weight = 400},
    FontSize{.units = 16.0F, .weight = 400}}};

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
constexpr std::int32_t kTrackTall = 2;
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
    [[nodiscard]] inline auto px(std::int32_t units) const noexcept -> std::int32_t {
        return design->pixels(units);
    }

    void fill(const BuyRect& rect, ScreenColor color) {
        painter.fill(rect.x, rect.y, rect.wide, rect.tall, color);
    }

    /// `rect`'s edges, a pixel thick, in `color`.
    void outline(const BuyRect& rect, ScreenColor color) {
        painter.fill(rect.x, rect.y, rect.wide, 1, color);
        painter.fill(rect.x, rect.y + rect.tall - 1, rect.wide, 1, color);
        painter.fill(rect.x, rect.y + 1, 1, rect.tall - 2, color);
        painter.fill(rect.x + rect.wide - 1, rect.y + 1, 1, rect.tall - 2, color);
    }

    /// `rect` filled with `color`, a bar in the team's accent on its left when `isLit`.
    void cell(const BuyRect& rect, ScreenColor color, bool isLit) {
        fill(rect, color);
        if (isLit) {
            painter.fill(rect.x, rect.y, px(kAccentBar), rect.tall, teamAccent);
        }
    }

    [[nodiscard]] auto lineOf(BuyMenuFont font) const noexcept -> std::int32_t {
        return (*textFaces)[font].atlas->metrics().height();
    }
    [[nodiscard]] auto ascentOf(BuyMenuFont font) const noexcept -> std::int32_t {
        return (*textFaces)[font].atlas->metrics().ascent;
    }
    [[nodiscard]] auto widthOf(BuyMenuFont font, std::string_view text) const -> std::int32_t {
        return nx::ui::widthOf(*(*textFaces)[font].atlas, text);
    }
    [[nodiscard]] auto wrapped(BuyMenuFont font, std::string_view text, std::int32_t wide) const
        -> std::vector<std::string> {
        return nx::ui::wrapText(*(*textFaces)[font].atlas, text, wide);
    }

    /// `text` with its line's top at `y` and its pen from `x`. Returns where the pen ends.
    auto write(BuyMenuFont font, std::string_view text, std::int32_t x, std::int32_t y, ScreenColor color)
        -> std::int32_t {
        return painter.text((*textFaces)[font], text, x, y, color);
    }
    /// `text` ending at `end`.
    void writeEnding(BuyMenuFont font, std::string_view text, std::int32_t end, std::int32_t y,
                     ScreenColor color) {
        std::ignore = write(font, text, end - widthOf(font, text), y, color);
    }

    /// How wide the badge of `key` is: square, or wider for a key of a long name.
    [[nodiscard]] auto keyBadgeWide(std::string_view key) const -> std::int32_t {
        return std::max(px(kKeyBadge), widthOf(BuyMenuFont::Caption, key) + px(kBadgeTextGap));
    }

    /// A key's badge at `x`, in the middle of `rect`'s height: lit, it is filled with the team's accent.
    /// Returns where it ends.
    auto keyBadge(std::string_view key, std::int32_t x, const BuyRect& rect, bool isLit) -> std::int32_t {
        constexpr BuyMenuFont kFont = BuyMenuFont::Caption;
        const std::int32_t tall = px(kKeyBadge);
        const std::int32_t textWide = widthOf(kFont, key);
        const std::int32_t wide = keyBadgeWide(key);
        const BuyRect badge{.x = x, .y = rect.y + ((rect.tall - tall) / 2), .wide = wide, .tall = tall};
        if (isLit) {
            fill(badge, teamAccent);
        } else {
            outline(badge, kLine);
        }
        std::ignore = write(kFont, key, x + ((wide - textWide) / 2), badge.y + ((tall - lineOf(kFont)) / 2),
                            isLit ? onTeamAccent : kInk);
        return x + wide;
    }

    /// The picture of the file `path`, whole and as large as fits in the middle of `box`, as opaque as
    /// `alpha`; nothing for one that could not be read.
    void picture(std::string_view path, const BuyRect& box, std::uint8_t alpha) {
        const auto found = loaded->find(path);
        if (found == loaded->end() || !found->second) {
            return;
        }
        const nx::render::ScreenImage& image = *found->second;
        const float wide = static_cast<float>(image.width());
        const float tall = static_cast<float>(image.height());
        const float fit = std::min(static_cast<float>(box.wide) / wide, static_cast<float>(box.tall) / tall);
        output->push_back({.image = &image,
                           .source = {.x = 0, .y = 0, .width = image.width(), .height = image.height()},
                           .target = {.x = static_cast<float>(box.x) +
                                           ((static_cast<float>(box.wide) - (wide * fit)) / 2.0F),
                                      .y = static_cast<float>(box.y) +
                                           ((static_cast<float>(box.tall) - (tall * fit)) / 2.0F),
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
    const BuyRect& header = paint.layout().header();
    const std::int32_t trackY = header.y + header.tall - paint.px(kTrackTall);
    const std::int32_t baseline = trackY - paint.px(kBaselineOverTrack);
    const auto topFor = [&](BuyMenuFont font) {
        return baseline - paint.ascentOf(font);
    };

    std::int32_t pen =
        paint.write(BuyMenuFont::Headline, kMenuName, header.x, topFor(BuyMenuFont::Headline), kInk);
    pen = paint.write(BuyMenuFont::Body, kTeamNames[frame.team], pen + paint.px(kHeaderTextGap),
                      topFor(BuyMenuFont::Body), paint.accent());
    pen = paint.write(BuyMenuFont::Body, "Time to buy ", pen + paint.px(kHeaderTextGap),
                      topFor(BuyMenuFont::Body), kInkSoft);
    std::ignore =
        paint.write(BuyMenuFont::Body, clockText(frame.secondsLeft), pen, topFor(BuyMenuFont::Body), kInk);

    const std::int32_t end = header.x + header.wide;
    const std::int32_t moneyTop = topFor(BuyMenuFont::Headline);
    paint.writeEnding(BuyMenuFont::Headline, dollarsText(frame.money), end, moneyTop, paint.accent());
    const bool isShort = frame.detail && frame.detail->entry.state == BuyItemState::TooDear;
    paint.writeEnding(BuyMenuFont::Body, moneyAfterText(frame), end,
                      moneyTop - paint.lineOf(BuyMenuFont::Body), isShort ? kDanger : kInkSoft);

    paint.fill({.x = header.x, .y = trackY, .wide = header.wide, .tall = paint.px(kTrackTall)}, kLine);
    const float share = frame.secondsInAll == 0
                            ? 0.0F
                            : static_cast<float>(frame.secondsLeft) / static_cast<float>(frame.secondsInAll);
    paint.fill({.x = header.x,
                .y = trackY,
                .wide = static_cast<std::int32_t>(static_cast<float>(header.wide) * std::min(share, 1.0F)),
                .tall = paint.px(kTrackTall)},
               paint.accent());
}

void paintRail(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    for (const auto& [index, listed] : std::views::enumerate(frame.tabs)) {
        const BuyRect rect = layout.tab(nx::narrow<std::size_t>(index));
        const bool isShownTab = listed.tab == frame.tab;
        paint.cell(rect, isShownTab ? kCellLit : kCell, isShownTab);
        const std::int32_t pen =
            paint.keyBadge(std::to_string(kBuyTabKeys[listed.tab]), rect.x + paint.px(kInsetLeft), rect,
                           isShownTab && frame.isArmed);
        std::ignore = paint.write(BuyMenuFont::Body, kTabNames[listed.tab], pen + paint.px(kTextGap),
                                  rect.y + ((rect.tall - paint.lineOf(BuyMenuFont::Body)) / 2),
                                  isShownTab ? kInk : kInkSoft);
        const std::int32_t wide = paint.px(kTabPictureWide);
        const std::int32_t tall = paint.px(kTabPictureTall);
        paint.picture(listed.picture,
                      {.x = rect.x + rect.wide - paint.px(kInsetRight) - wide,
                       .y = rect.y + ((rect.tall - tall) / 2),
                       .wide = wide,
                       .tall = tall},
                      isShownTab ? kWholePicture : kTabPicture);
    }
    for (const auto& [index, row] : std::views::enumerate(frame.ammo)) {
        const BuyRect rect = layout.ammoRow(nx::narrow<std::size_t>(index), frame.tabs.size());
        paint.cell(rect, kCell, false);
        const std::int32_t pen =
            paint.keyBadge(std::to_string(row.key), rect.x + paint.px(kInsetLeft), rect, false) +
            paint.px(kTextGap);
        const std::int32_t top =
            rect.y + ((rect.tall - paint.lineOf(BuyMenuFont::Body) - paint.lineOf(BuyMenuFont::Caption)) / 2);
        std::ignore = paint.write(
            BuyMenuFont::Body, row.key == kPrimaryAmmoKey ? "Primary ammo" : "Pistol ammo", pen, top, kInk);
        const std::string facts =
            !row.ammo ? std::string{"No weapon"}
            : row.state == BuyItemState::Owned
                ? std::format("{} {}/{} {} full", row.ammo->name, row.ammo->carried, row.ammo->most,
                              kMiddleDot)
                : std::format("{} {}/{} {} +{} {}", row.ammo->name, row.ammo->carried, row.ammo->most,
                              kMiddleDot, row.ammo->boxRounds, dollarsText(row.ammo->boxPrice));
        std::ignore = paint.write(BuyMenuFont::Caption, facts, pen, top + paint.lineOf(BuyMenuFont::Body),
                                  row.state == BuyItemState::TooDear ? kDanger : kInkSoft);
    }
}

void paintGrid(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    const bool hasDetail = frame.detail.has_value();
    for (const auto& [index, entry] : std::views::enumerate(frame.entries)) {
        const BuyRect rect = layout.cell(nx::narrow<std::size_t>(index), frame.entries.size());
        const bool isSelected = hasDetail && frame.detail->entry.item == entry.item;
        paint.cell(rect, isSelected ? kCellLit : kCell, isSelected);

        const std::int32_t inset = paint.px(kCellInset);
        const std::int32_t room = rect.wide - (2 * inset);
        const bool isOwned = entry.state == BuyItemState::Owned;
        const BuyMenuFont priceFont = isOwned ? BuyMenuFont::Caption : BuyMenuFont::Body;
        const std::string price = !isOwned                                        ? dollarsText(entry.price)
                                  : std::holds_alternative<ArmorKind>(entry.item) ? std::string{kWorn}
                                                                                  : std::string{kOwned};
        const std::vector<std::string> nameRows = paint.wrapped(BuyMenuFont::Body, entry.name, room);
        const bool isApart = isPriceApart(nameRows.size(), paint.widthOf(BuyMenuFont::Body, entry.name),
                                          paint.widthOf(priceFont, price), paint.px(kTextGap), room);
        const std::int32_t line = paint.lineOf(BuyMenuFont::Body);
        const auto rows = nx::narrow<std::int32_t>(nameRows.size()) + (isApart ? 1 : 0);
        const std::int32_t rowY = rect.y + rect.tall - inset - (rows * line);
        const bool isTooDear = entry.state == BuyItemState::TooDear;
        paint.picture(entry.picture,
                      {.x = rect.x + inset,
                       .y = rect.y + inset,
                       .wide = rect.wide - (2 * inset),
                       .tall = rowY - rect.y - inset},
                      isTooDear ? kTooDearPicture : kWholePicture);

        const BuyMenuFont digitFont = frame.isArmed ? BuyMenuFont::Strong : BuyMenuFont::Caption;
        std::ignore = paint.write(digitFont, std::to_string(entry.key), rect.x + inset, rect.y + (inset / 2),
                                  frame.isArmed ? paint.accent() : kInkFaint);

        std::int32_t y = rowY;
        for (const std::string& row : nameRows) {
            std::ignore =
                paint.write(BuyMenuFont::Body, row, rect.x + inset, y, isTooDear ? kInkFaint : kInk);
            y += line;
        }
        // On the name's last row, or a row of its own: at the foot of the cell either way.
        const std::int32_t priceY = rect.y + rect.tall - inset - paint.lineOf(priceFont);
        paint.writeEnding(priceFont, price, rect.x + rect.wide - inset, priceY,
                          isOwned     ? paint.accent()
                          : isTooDear ? kDanger
                                      : kInkSoft);
    }
}

/// A stat of the item selected at `y`: its name, its value and a bar of it. Returns where it ends.
auto paintStat(Paint& paint, const BuyRect& inner, std::int32_t y, std::string_view name,
               std::string_view value, float share) -> std::int32_t {
    std::ignore = paint.write(BuyMenuFont::Body, name, inner.x, y, kInkSoft);
    paint.writeEnding(BuyMenuFont::Body, value, inner.x + inner.wide, y, kInk);
    const std::int32_t barY = y + paint.lineOf(BuyMenuFont::Body) + paint.px(kStatBarGap);
    const std::int32_t tall = paint.px(kStatBarTall);
    paint.fill({.x = inner.x, .y = barY, .wide = inner.wide, .tall = tall}, kLine);
    paint.fill(
        {.x = inner.x,
         .y = barY,
         .wide = static_cast<std::int32_t>(static_cast<float>(inner.wide) * std::clamp(share, 0.0F, 1.0F)),
         .tall = tall},
        kInk);
    return barY + tall + paint.px(kStatGap);
}

/// What a gun tells of itself, from `y`.
void paintGunFacts(Paint& paint, const BuyRect& inner, std::int32_t y, const BuyGunFacts& gun) {
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
    const std::int32_t column = inner.wide / nx::narrow<std::int32_t>(facts.size());
    for (const auto& [index, fact] : std::views::enumerate(facts)) {
        const std::int32_t x = inner.x + (nx::narrow<std::int32_t>(index) * column);
        std::ignore = paint.write(BuyMenuFont::Caption, fact.first, x, y, kInkFaint);
        std::ignore =
            paint.write(BuyMenuFont::Strong, fact.second, x, y + paint.lineOf(BuyMenuFont::Caption), kInk);
    }
    y += paint.lineOf(BuyMenuFont::Caption) + paint.lineOf(BuyMenuFont::Strong) + paint.px(kStatGap);
    paint.fill({.x = inner.x, .y = y, .wide = inner.wide, .tall = 1}, kLine);
    y += paint.px(kStatGap);
    std::ignore = paint.write(BuyMenuFont::Body, gun.ammoName, inner.x, y, kInkSoft);
    paint.writeEnding(BuyMenuFont::Body,
                      std::format("{} for {} rounds", dollarsText(gun.boxPrice), gun.boxRounds),
                      inner.x + inner.wide, y, kInkSoft);
}

void paintDetail(Paint& paint, const BuyMenuFrame& frame) {
    const BuyMenuLayout& layout = paint.layout();
    const BuyRect& panel = layout.detail();
    paint.fill(panel, kGlass);
    paint.outline(panel, kLine);
    if (!frame.detail) {
        return;
    }
    const BuyDetail& detail = *frame.detail;
    const std::int32_t inset = paint.px(kDetailInset);
    const BuyRect inner{
        .x = panel.x + inset, .y = panel.y, .wide = panel.wide - (2 * inset), .tall = panel.tall};

    std::int32_t y = panel.y + paint.px(kDetailTop);
    const std::string kind = detail.buyers ? std::format("{} {} {}", kKindNames[detail.tab], kMiddleDot,
                                                         kBuyersNames[*detail.buyers])
                                           : std::string{kKindNames[detail.tab]};
    std::ignore = paint.write(BuyMenuFont::Caption, kind, inner.x, y, paint.accent());
    y += paint.lineOf(BuyMenuFont::Caption);
    std::ignore = paint.write(BuyMenuFont::Title, detail.entry.name, inner.x, y, kInk);
    y += paint.lineOf(BuyMenuFont::Title) + paint.px(kSectionGap);

    if (detail.gun) {
        paintGunFacts(paint, inner, y, *detail.gun);
    }
    for (const std::string& row : paint.wrapped(BuyMenuFont::Body, detail.about, inner.wide)) {
        std::ignore = paint.write(BuyMenuFont::Body, row, inner.x, y, kInkSoft);
        y += paint.lineOf(BuyMenuFont::Body);
    }

    const BuyRect button = layout.buyButton();
    const bool isForSale = detail.entry.state == BuyItemState::ForSale;
    const std::string note =
        detail.entry.state == BuyItemState::TooDear ? std::string{buyRefusalText(BuyRefusal::NotEnoughMoney)}
        : isForSale && !detail.replaces.empty()
            ? std::format("Replaces your {}. The rounds you carry stay.", detail.replaces)
            : std::string{};
    const std::vector<std::string> noteRows = paint.wrapped(BuyMenuFont::Caption, note, inner.wide);
    std::int32_t noteY = button.y - paint.px(kNoteGap) -
                         (nx::narrow<std::int32_t>(noteRows.size()) * paint.lineOf(BuyMenuFont::Caption));
    for (const std::string& row : noteRows) {
        std::ignore = paint.write(BuyMenuFont::Caption, row, inner.x, noteY,
                                  detail.entry.state == BuyItemState::TooDear ? kDanger : kInkSoft);
        noteY += paint.lineOf(BuyMenuFont::Caption);
    }

    paint.fill(button, isForSale ? paint.accent() : kCell);
    const ScreenColor color = isForSale ? paint.onAccent() : kInkFaint;
    const std::int32_t textY = button.y + ((button.tall - paint.lineOf(BuyMenuFont::Strong)) / 2);
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
void paintTile(Paint& paint, const BuyRect& tile, std::string_view name, std::string_view text,
               std::string_view picture, bool isCarried) {
    paint.fill(tile, kGlass);
    paint.outline(tile, kLine);
    std::int32_t x = tile.x + paint.px(kInsetLeft);
    if (!picture.empty()) {
        const std::int32_t tall = paint.px(kTilePictureTall);
        paint.picture(picture,
                      {.x = x,
                       .y = tile.y + ((tile.tall - tall) / 2),
                       .wide = paint.px(kTilePictureWide),
                       .tall = tall},
                      kWholePicture);
        x += paint.px(kTilePictureWide) + paint.px(kInsetRight);
    }
    const std::int32_t top =
        tile.y + ((tile.tall - paint.lineOf(BuyMenuFont::Caption) - paint.lineOf(BuyMenuFont::Body)) / 2);
    std::ignore = paint.write(BuyMenuFont::Caption, name, x, top, kInkFaint);
    std::ignore = paint.write(BuyMenuFont::Body, text, x, top + paint.lineOf(BuyMenuFont::Caption),
                              isCarried ? kInk : kInkFaint);
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
        const BuyRect tile = layout.againTile();
        paint.fill(tile, kGlass);
        paint.outline(tile, kLine);
        const std::int32_t pen =
            paint.keyBadge("R", tile.x + paint.px(kInsetLeft), tile, false) + paint.px(kTextGap);
        const std::string cost = dollarsText(frame.again->cost);
        const std::int32_t end = tile.x + tile.wide - paint.px(kInsetLeft);
        const std::int32_t room = end - paint.widthOf(BuyMenuFont::Strong, cost) - paint.px(kTextGap) - pen;
        std::string names;
        for (const std::string_view item : frame.again->items) {
            names += names.empty() ? std::string{item} : std::format(" {} {}", kMiddleDot, item);
        }
        const std::vector<std::string> rows = paint.wrapped(BuyMenuFont::Caption, names, room);
        const std::int32_t line = paint.lineOf(BuyMenuFont::Caption);
        std::int32_t y = tile.y + ((tile.tall - ((nx::narrow<std::int32_t>(rows.size()) + 1) * line)) / 2);
        std::ignore = paint.write(BuyMenuFont::Caption, "BUY AGAIN", pen, y, kInkFaint);
        for (const std::string& row : rows) {
            y += line;
            std::ignore = paint.write(BuyMenuFont::Caption, row, pen, y, kInk);
        }
        paint.writeEnding(BuyMenuFont::Strong, cost, end,
                          tile.y + ((tile.tall - paint.lineOf(BuyMenuFont::Strong)) / 2),
                          frame.money < frame.again->cost ? kDanger : paint.accent());
    }

    // The keys, from the right.
    const BuyRect& row = layout.carried();
    std::int32_t end = row.x + row.wide;
    for (const auto& [key, what] :
         {std::pair{"B", "Close"}, std::pair{"Esc", "Back"}, std::pair{"Enter", "Buy"}}) {
        end -= paint.widthOf(BuyMenuFont::Caption, what);
        std::ignore = paint.write(BuyMenuFont::Caption, what, end,
                                  row.y + ((row.tall - paint.lineOf(BuyMenuFont::Caption)) / 2), kInkSoft);
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
    -> nx::Result<void, BuyMenuPanelError> {
    const auto family = std::ranges::find_if(content.look->fonts, [&fonts](const std::string& name) {
        return fonts.choose({.family = name, .weight = kFontSizes[BuyMenuFont::Body].weight}).has_value();
    });
    if (family == content.look->fonts.end()) {
        return nx::fail(BuyMenuPanelError::NoFont,
                        std::format("none of {} among the system's fonts", content.look->fonts));
    }
    const std::u32string characters = consoleCharacters();
    for (const BuyMenuFont font : nx::EnumArray<BuyMenuFont, FontSize>::keys()) {
        const FontSize size = kFontSizes[font];
        const std::optional<nx::ui::FontChoice> choice =
            fonts.choose({.family = *family, .weight = size.weight});
        if (!choice) {
            return nx::fail(BuyMenuPanelError::NoFont, *family);
        }
        const auto cellHeight = static_cast<std::int32_t>(
            std::lround(size.units * static_cast<float>(screenHeight) / kBuyMenuDesignHeight));
        auto opened = fonts.open(*choice, {.cellHeight = std::max(cellHeight, 1),
                                           .smoothing = nx::ui::FontSmoothing::Antialiased});
        if (!opened) {
            return nx::fail(BuyMenuPanelError::Unusable, *family, std::move(opened.error()));
        }
        auto atlas = nx::ui::GlyphAtlas::build(*opened, characters);
        if (!atlas) {
            return nx::fail(BuyMenuPanelError::Unusable, *family, std::move(atlas.error()));
        }
        auto image = screen.loadImage(commands, atlas->texels(), atlas->width(), atlas->height(), "buyMenu");
        if (!image) {
            return nx::fail(BuyMenuPanelError::GpuFailed, *family, std::move(image.error()));
        }
        faces[font].emplace(Face{.atlas = std::move(*atlas), .image = std::move(*image)});
    }
    facesHeight = screenHeight;
    return {};
}

auto BuyMenuPanel::loadPicture(std::string_view path, const nx::assets::Vfs& vfs,
                               nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, BuyMenuPanelError> {
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
        return nx::fail(BuyMenuPanelError::GpuFailed, std::string{path}, std::move(loaded.error()));
    }
    picture.emplace(std::move(*loaded));
    return {};
}

auto BuyMenuPanel::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                        nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                        std::uint32_t screenHeight) -> nx::Result<void, BuyMenuPanelError> {
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
    nx::ui::PanelPainter{quads, size}.fill(0, 0, nx::narrow<std::int32_t>(size.width),
                                           nx::narrow<std::int32_t>(size.height), kBackdrop);
}

auto BuyMenuPanel::textFaces() const noexcept -> TextFaces {
    TextFaces shownFaces;
    for (const BuyMenuFont font : TextFaces::keys()) {
        shownFaces[font] = {.atlas = &faces[font]->atlas, .image = &faces[font]->image};
    }
    return shownFaces;
}

void BuyMenuPanel::append(std::vector<nx::render::ScreenQuad>& quads, const BuyMenuFrame& frame,
                          nx::rhi::Extent2d size) {
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
    const std::int32_t wide = nx::ui::widthOf(atlas, notice->text);
    const std::int32_t padX = layout.pixels(kNoticePadX);
    const std::int32_t padY = layout.pixels(kNoticePadY);
    const std::int32_t x = (nx::narrow<std::int32_t>(size.width) - wide) / 2;
    const std::int32_t y = layout.pixels(isMenuOpen ? kNoticeOverMenu : kNoticeOverGame);
    nx::ui::PanelPainter painter{quads, size};
    painter.fill(x - padX, y - padY, wide + (2 * padX), atlas.metrics().height() + (2 * padY),
                 fadedBy(kDangerGlass, opacity));
    painter.fill(x - padX, y - padY, layout.pixels(kAccentBar), atlas.metrics().height() + (2 * padY),
                 fadedBy(kDanger, opacity));
    std::ignore = painter.text({.atlas = &atlas, .image = &faces[BuyMenuFont::Strong]->image}, notice->text,
                               x, y, fadedBy(kInk, opacity));
}

} // namespace cs
