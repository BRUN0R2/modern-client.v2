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
using TextFaces = nx::EnumArray<TeamMenuFont, nx::ui::TextFace>;

constexpr nx::log::Category kLog{"client"};

// As the buy menu's: what warns, and each team's accent, the terrorists' the menus' gold.
constexpr ScreenColor kDanger{.red = 244, .green = 101, .blue = 79};
constexpr nx::EnumArray<Team, ScreenColor> kAccents{
    nx::EnumArray<Team, ScreenColor>::Storage{kMenuGold, ScreenColor{.red = 103, .green = 184, .blue = 246}}};
constexpr nx::EnumArray<Team, ScreenColor> kOnAccents{
    nx::EnumArray<Team, ScreenColor>::Storage{kOnMenuGold, ScreenColor{.red = 4, .green = 19, .blue = 31}}};
constexpr std::uint8_t kWholePicture = 255;
constexpr std::uint8_t kRowPicture = 128;

constexpr nx::EnumArray<TeamMenuFont, MenuFontSize> kFontSizes{
    nx::EnumArray<TeamMenuFont, MenuFontSize>::Storage{MenuFontSize{.line = MenuUnits{56}, .isStrong = true},
                                                       MenuFontSize{.line = MenuUnits{32}, .isStrong = false},
                                                       MenuFontSize{.line = MenuUnits{16}, .isStrong = false}}};

// In the design's units: what the layout leaves to what draws in it.
constexpr MenuUnits kAccentBar{3};
constexpr MenuUnits kKeyBadge{30};
constexpr MenuUnits kBadgeTextGap{7};
constexpr MenuUnits kInsetLeft{14};
constexpr MenuUnits kInsetRight{10};
constexpr MenuUnits kTextGap{14};
constexpr MenuUnits kBaselineOverRule{14};
constexpr MenuUnits kRowPictureWide{96};
constexpr MenuUnits kRowPictureTall{68};
constexpr MenuUnits kDetailInset{24};
/// What the room left is divided by to put a thing halfway into it.
constexpr std::int32_t kMiddleDivisor = 2;

/// Draws the menu's parts: rectangles, text and pictures of the screen, by the layout's design.
class Paint {
public:
    Paint(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size, const TeamMenuLayout& layout,
          const TextFaces& faces,
          const std::map<std::string, std::optional<nx::render::ScreenImage>, std::less<>>& pictures)
        : output{&quads}, painter{quads, size}, design{&layout}, textFaces{&faces}, loaded{&pictures} {}

    [[nodiscard]] inline auto layout() const noexcept -> const TeamMenuLayout& { return *design; }
    [[nodiscard]] inline auto px(MenuUnits units) const noexcept -> nx::Pixels { return design->pixels(units); }

    void fill(const nx::ui::PixelRect& rect, ScreenColor color) { painter.fill(rect, color); }

    [[nodiscard]] auto lineOf(TeamMenuFont font) const noexcept -> nx::Pixels {
        return (*textFaces)[font].atlas->lineHeight();
    }
    [[nodiscard]] auto ascentOf(TeamMenuFont font) const noexcept -> nx::Pixels {
        return (*textFaces)[font].atlas->ascent();
    }
    [[nodiscard]] auto widthOf(TeamMenuFont font, std::string_view text) const -> nx::Pixels {
        return nx::ui::widthOf(*(*textFaces)[font].atlas, text);
    }

    /// `text` with its line's top at `y` and its pen from `x`. Returns where the pen ends.
    auto write(TeamMenuFont font, std::string_view text, nx::Pixels x, nx::Pixels y, ScreenColor color)
        -> nx::Pixels {
        return painter.text((*textFaces)[font], text, {.x = x, .y = y}, color);
    }

    /// `rect` filled with `color`, a bar in `accent` on its left when `isLit`.
    void cell(const nx::ui::PixelRect& rect, ScreenColor color, bool isLit, ScreenColor accent) {
        fill(rect, color);
        if (isLit) {
            painter.fill({.x = rect.x, .y = rect.y, .wide = px(kAccentBar), .tall = rect.tall}, accent);
        }
    }

    /// A key's badge at `x`, in the middle of `rect`'s height: lit, filled with `accent`. Returns where it
    /// ends.
    auto keyBadge(std::string_view key, nx::Pixels x, const nx::ui::PixelRect& rect, bool isLit,
                  ScreenColor accent, ScreenColor onAccent) -> nx::Pixels {
        constexpr TeamMenuFont kFont = TeamMenuFont::Caption;
        const nx::Pixels tall = px(kKeyBadge);
        const nx::Pixels textWide = widthOf(kFont, key);
        const nx::Pixels wide = std::max(tall, textWide + px(kBadgeTextGap));
        const nx::ui::PixelRect badge{
            .x = x, .y = rect.y + ((rect.tall - tall) / kMiddleDivisor), .wide = wide, .tall = tall};
        if (isLit) {
            fill(badge, accent);
        } else {
            paintMenuOutline(painter, badge, kMenuLine);
        }
        std::ignore = write(kFont, key, x + ((wide - textWide) / kMiddleDivisor),
                            badge.y + ((tall - lineOf(kFont)) / kMiddleDivisor), isLit ? onAccent : kMenuInk);
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
        const auto wide = static_cast<float>(image.width());
        const auto tall = static_cast<float>(image.height());
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
    const TeamMenuLayout* design;
    const TextFaces* textFaces;
    const std::map<std::string, std::optional<nx::render::ScreenImage>, std::less<>>* loaded;
};

void paintHeader(Paint& paint, const TeamMenuFrame& frame) {
    const nx::ui::PixelRect rule = paint.layout().rule();
    const nx::Pixels baseline = rule.y - paint.px(kBaselineOverRule);
    std::ignore = paint.write(TeamMenuFont::Headline, frame.title, paint.layout().header().x,
                              baseline - paint.ascentOf(TeamMenuFont::Headline), kMenuInk);
    paint.fill(rule, kMenuLine);
}

void paintRows(Paint& paint, const TeamMenuFrame& frame) {
    for (const auto& [index, row] : std::views::enumerate(frame.rows)) {
        const nx::ui::PixelRect rect = paint.layout().row(nx::narrow<std::size_t>(index));
        const bool isSelected = frame.selected == nx::narrow<std::size_t>(index);
        const ScreenColor accent = kAccents[row.accent];
        paint.cell(rect, isSelected ? kMenuCellLit : kMenuCell, isSelected, accent);
        const nx::Pixels pen = paint.keyBadge(std::to_string(row.key), rect.x + paint.px(kInsetLeft), rect,
                                              isSelected, accent, kOnAccents[row.accent]);
        std::ignore = paint.write(TeamMenuFont::Title, row.label, pen + paint.px(kTextGap),
                                  rect.y + ((rect.tall - paint.lineOf(TeamMenuFont::Title)) / kMiddleDivisor),
                                  isSelected ? kMenuInk : kMenuInkSoft);
        if (!row.picture.empty()) {
            const nx::Pixels wide = paint.px(kRowPictureWide);
            const nx::Pixels tall = paint.px(kRowPictureTall);
            paint.picture(row.picture,
                          {.x = rect.x + rect.wide - paint.px(kInsetRight) - wide,
                           .y = rect.y + ((rect.tall - tall) / kMiddleDivisor),
                           .wide = wide,
                           .tall = tall},
                          isSelected ? kWholePicture : kRowPicture);
        }
    }
    if (!frame.refusal.empty()) {
        const nx::ui::PixelRect rect = paint.layout().refusal();
        std::ignore = paint.write(TeamMenuFont::Title, frame.refusal, rect.x,
                                  rect.y + ((rect.tall - paint.lineOf(TeamMenuFont::Title)) / kMiddleDivisor),
                                  kDanger);
    }
}

void paintDetail(Paint& paint, const TeamMenuFrame& frame) {
    const nx::ui::PixelRect& detail = paint.layout().detail();
    paint.fill(detail, kMenuCell);
    if (!frame.selected) {
        return;
    }
    const TeamMenuRow& row = frame.rows[*frame.selected];
    const nx::Pixels inset = paint.px(kDetailInset);
    const nx::Pixels nameTall = paint.lineOf(TeamMenuFont::Headline);
    std::ignore = paint.write(TeamMenuFont::Headline, row.label, detail.x + inset,
                              detail.y + detail.tall - inset - nameTall, kAccents[row.accent]);
    if (!row.picture.empty()) {
        paint.picture(row.picture,
                      {.x = detail.x + inset,
                       .y = detail.y + inset,
                       .wide = detail.wide - (kMiddleDivisor * inset),
                       .tall = detail.tall - (kMiddleDivisor * inset) - nameTall - inset},
                      kWholePicture);
    }
}

} // namespace

TeamMenuPanel::TeamMenuPanel(const BuyMenuLook& looks) noexcept : look{&looks} {}

auto TeamMenuPanel::loadPicture(std::string_view path, const nx::assets::Vfs& vfs,
                                nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, MenuPanelError> {
    if (pictures.contains(path)) {
        return {};
    }
    auto& picture = pictures[std::string{path}];
    const auto file = nx::assets::VfsPath::parse(path);
    auto bytes = file ? vfs.readAll(*file) : nx::Result<std::vector<std::byte>, nx::assets::VfsError>{};
    auto image = file && bytes ? nx::assets::readTga(*bytes)
                               : nx::Result<nx::assets::RgbaImage, nx::assets::TgaError>{};
    if (!file || !bytes || !image) {
        nx::log::warning(kLog, "the team menu shows no picture of {:?}: it cannot be read", path);
        return {};
    }
    auto loaded = screen.loadImage(commands, image->pixels, image->width, image->height, path);
    if (!loaded) {
        return nx::fail(MenuPanelError::GpuFailed, std::string{path}, std::move(loaded.error()));
    }
    picture.emplace(std::move(*loaded));
    return {};
}

auto TeamMenuPanel::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                         nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands,
                         const Characters& characters, std::uint32_t screenHeight)
    -> nx::Result<void, MenuPanelError> {
    if (screenHeight != facesHeight) {
        for (const TeamMenuFont font : nx::EnumArray<TeamMenuFont, MenuFontSize>::keys()) {
            auto face =
                makeMenuFace(fonts, screen, commands, look->font, kFontSizes[font], screenHeight, "teamMenu");
            if (!face) {
                return std::unexpected{std::move(face.error())};
            }
            faces[font].emplace(std::move(*face));
        }
        facesHeight = screenHeight;
        shown.forget();
    }
    std::vector<std::string> paths{"gfx/vgui/t_random.tga", "gfx/vgui/ct_random.tga"};
    for (const Character& character : characters.characters) {
        paths.push_back(std::format("gfx/vgui/{}.tga", character.name));
    }
    for (const std::string& path : paths) {
        if (auto loaded = loadPicture(path, vfs, screen, commands); !loaded) {
            return loaded;
        }
    }
    return {};
}

void TeamMenuPanel::append(std::vector<nx::render::ScreenQuad>& quads, const TeamMenuFrame& frame,
                           nx::rhi::Extent2d size) {
    if (!isLoadedFor(size.height)) {
        return;
    }
    shown.append(quads, Shown{.frame = frame, .size = size}, [&](std::vector<nx::render::ScreenQuad>& laidOut) {
        nx::ui::PanelPainter{laidOut, size}.fill({.wide = nx::Pixels{nx::narrow<std::int32_t>(size.width)},
                                                  .tall = nx::Pixels{nx::narrow<std::int32_t>(size.height)}},
                                                 kMenuBackdrop);
        const TeamMenuLayout layout{size};
        TextFaces shownFaces;
        for (const TeamMenuFont font : TextFaces::keys()) {
            shownFaces[font] = faces[font]->textFace();
        }
        Paint paint{laidOut, size, layout, shownFaces, pictures};
        paintHeader(paint, frame);
        paintRows(paint, frame);
        paintDetail(paint, frame);
    });
}

} // namespace cs
