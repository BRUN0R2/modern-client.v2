// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

/// The beginning of the name in sprites/hud.txt of a picture of a kill.
constexpr std::string_view kKillPicturePrefix = "d_";
constexpr std::string_view kSkullPicture = "d_skull";
constexpr std::string_view kHeadPicture = "d_headshot";

// Where the rows go: shares of the screen's height, whole pixels (docs/12 section 3.2, measured at 800x600,
// 1024x768, 1280x720, 1280x960 and 1600x900).
constexpr std::int32_t kRowsTopShare = 15;     ///< Down to the first row's top, and as much more with the
                                               ///< player dead, under the spectator's panel.
constexpr std::int32_t kRowPitchShare = 30;    ///< From a row's top to the next one's.
constexpr std::int32_t kRightMarginShare = 40; ///< Between the victim's name and the right edge.
constexpr std::int32_t kPartGapShare = 96;     ///< Before the victim's name, and the killer's after it.
constexpr std::int32_t kDeadRowsTopFactor = 2; ///< How much further down the rows start with the player dead.

/// The color each team's names are written in.
constexpr nx::EnumArray<Team, nx::render::ScreenColor> kTeamNameColors{
    nx::EnumArray<Team, nx::render::ScreenColor>::Storage{
        nx::render::ScreenColor{.red = 255, .green = 63, .blue = 63},
        nx::render::ScreenColor{.red = 153, .green = 204, .blue = 255}}};
/// The color the pictures of a kill are added in, and of a teammate's.
constexpr nx::render::ScreenColor kKillColor{.red = 255, .green = 80, .blue = 0};
constexpr nx::render::ScreenColor kTeamKillColor{.red = 10, .green = 240, .blue = 10};

} // namespace

auto KillFeedCvars::registerIn(nx::console::Console& console)
    -> nx::Result<KillFeedCvars, nx::console::CvarError> {
    auto rowTime = console.registerCvar(
        nx::console::CvarDesc<float>{.name = "hud_deathnotice_time",
                                     .defaultValue = kKillRowTime.count(),
                                     .description = "The seconds a row of the kill feed shows."});
    if (!rowTime) {
        return std::unexpected{std::move(rowTime.error())};
    }
    return KillFeedCvars{.rowTime = std::move(*rowTime)};
}

auto KillFeedCvars::shownFor() const noexcept -> nx::Seconds {
    const float seconds = rowTime.value();
    return std::isfinite(seconds) ? nx::Seconds{std::max(seconds, 0.0F)} : nx::Seconds{};
}

auto KillPictures::of(std::span<const nx::assets::SpriteListEntry> entries, HudSprites& sprites)
    -> nx::Result<KillPictures, HudError> {
    Pictures pictures;
    for (const nx::assets::SpriteListEntry& entry : entries) {
        if (entry.resolution == kHudResolution && entry.name.starts_with(kKillPicturePrefix)) {
            pictures.try_emplace(entry.name.substr(kKillPicturePrefix.size()),
                                 HudSpritePicture{.rect = entry.rect, .image = sprites.imageOf(entry)});
        }
    }
    const auto named = [&pictures](std::string_view name) -> nx::Result<HudSpritePicture, HudError> {
        const auto found = pictures.find(name.substr(kKillPicturePrefix.size()));
        if (found == pictures.end()) {
            return nx::fail(HudError::PictureMissing, std::string{name});
        }
        return found->second;
    };
    auto skull = named(kSkullPicture);
    if (!skull) {
        return std::unexpected{std::move(skull.error())};
    }
    auto headShot = named(kHeadPicture);
    if (!headShot) {
        return std::unexpected{std::move(headShot.error())};
    }
    return KillPictures{std::move(pictures), *skull, *headShot};
}

KillPictures::KillPictures(Pictures pictures, HudSpritePicture skull, HudSpritePicture headShot) noexcept
    : weaponPictures{std::move(pictures)}, skullPicture{skull}, headPicture{headShot} {}

auto KillPictures::icon(std::optional<std::string_view> weapon) const -> const HudSpritePicture& {
    if (!weapon) {
        return skullPicture;
    }
    const auto found = weaponPictures.find(*weapon);
    return found != weaponPictures.end() ? found->second : skullPicture;
}

auto layoutKillRow(const KillRowWidths& widths, std::size_t row, bool isPlayerDead,
                   nx::rhi::Extent2d size) noexcept -> KillRowLayout {
    const nx::Pixels height{nx::narrow<std::int32_t>(size.height)};
    const nx::Pixels gap = height / kPartGapShare;
    const nx::Pixels rowsTop = (height / kRowsTopShare) * (isPlayerDead ? kDeadRowsTopFactor : 1);
    KillRowLayout layout{.y = rowsTop + ((height / kRowPitchShare) * nx::narrow<std::int32_t>(row))};
    layout.victimX =
        nx::Pixels{nx::narrow<std::int32_t>(size.width)} - (height / kRightMarginShare) - widths.victim;
    nx::Pixels x = layout.victimX - gap;
    if (widths.head) {
        x -= *widths.head;
        layout.headX = x;
    }
    x -= widths.icon;
    layout.iconX = x;
    if (widths.killer) {
        layout.killerX = x - gap - *widths.killer;
    }
    return layout;
}

auto killLineOf(const KillNotice& notice, std::optional<std::string_view> weapon) -> std::string {
    if (!notice.killer) {
        return std::format("{} died", notice.victim.name);
    }
    if (notice.isTeamKill()) {
        return std::format("{} killed his teammate {}", notice.killer->name, notice.victim.name);
    }
    return weapon ? std::format("{} killed {} with {}", notice.killer->name, notice.victim.name, *weapon)
                  : std::format("{} killed {}", notice.killer->name, notice.victim.name);
}

auto KillFeed::open(const WindowFonts& fonts, std::uint32_t screenHeight)
    -> nx::Result<KillFeed, WindowFontError> {
    auto font = fonts.font(kEngineFont, screenHeight);
    if (!font) {
        return std::unexpected{std::move(font.error())};
    }
    auto built = nx::ui::GlyphAtlas::build(*font, consoleCharacters());
    if (!built) {
        return nx::fail(WindowFontError::Unusable, std::string{kEngineFont}, std::move(built.error()));
    }
    return KillFeed{std::move(*built)};
}

KillFeed::KillFeed(nx::ui::GlyphAtlas glyphAtlas) noexcept : atlas{std::move(glyphAtlas)} {}

auto KillFeed::load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
                    nx::rhi::CommandList& commands) -> nx::Result<void, HudError> {
    auto entries = readHudList(vfs);
    if (!entries) {
        return std::unexpected{std::move(entries.error())};
    }
    HudSprites sprites;
    auto pictures = KillPictures::of(*entries, sprites);
    if (!pictures) {
        return std::unexpected{std::move(pictures.error())};
    }
    auto images = sprites.load(vfs, screen, commands);
    if (!images) {
        return std::unexpected{std::move(images.error())};
    }
    auto glyphs = screen.loadImage(commands, atlas.texels(), atlas.width(), atlas.height(), "killFeed");
    if (!glyphs) {
        return nx::fail(HudError::GpuFailed, std::string{"the kill feed's glyphs"},
                        std::move(glyphs.error()));
    }
    loaded.emplace(
        Loaded{.glyphs = std::move(*glyphs), .images = std::move(*images), .pictures = std::move(*pictures)});
    return {};
}

void KillFeed::show(std::span<const KillNotice> notices, const WeaponCatalog& catalog, nx::Seconds now) {
    for (const KillNotice& notice : notices) {
        rows.push_back(
            Row{.killer = notice.killer,
                .victim = notice.victim,
                .weapon = notice.weapon ? std::optional{catalog.weapon(*notice.weapon).id} : std::nullopt,
                .isHead = notice.isHead,
                .isTeamKill = notice.isTeamKill(),
                .shownAt = now});
        ++rowChanges;
    }
    while (rows.size() > kMostKillRows) {
        rows.pop_front();
    }
}

void KillFeed::clear() noexcept {
    rows.clear();
    ++rowChanges;
}

void KillFeed::append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size, nx::Seconds now,
                      nx::Seconds rowTime, bool isPlayerDead) {
    if (std::erase_if(rows, [now, rowTime](const Row& row) { return now - row.shownAt > rowTime; }) > 0) {
        ++rowChanges;
    }
    if (!loaded) {
        return;
    }
    shown.append(
        quads, Shown{.rowChanges = rowChanges, .size = size, .isPlayerDead = isPlayerDead},
        [&](std::vector<nx::render::ScreenQuad>& laidOut) { layOut(laidOut, *loaded, size, isPlayerDead); });
}

void KillFeed::layOut(std::vector<nx::render::ScreenQuad>& quads, const Loaded& onGpu, nx::rhi::Extent2d size,
                      bool isPlayerDead) const {
    const auto widthOf = [this](std::string_view name) {
        return nx::ui::widthOf(atlas, name);
    };
    const auto writeName = [&](const NoticedPlayer& player, nx::Pixels x, nx::Pixels y) {
        std::ignore =
            nx::ui::appendText(quads, atlas, onGpu.glyphs, player.name, static_cast<float>(x.value()),
                               static_cast<float>(y.value()), kTeamNameColors[player.team]);
    };
    for (const auto& [index, row] : std::views::enumerate(rows)) {
        const nx::render::ScreenColor color = row.isTeamKill ? kTeamKillColor : kKillColor;
        const auto addPicture = [&](const HudSpritePicture& picture, nx::Pixels x, nx::Pixels y) {
            quads.push_back(nx::render::spriteQuad(
                onGpu.images[picture.image], picture.rect, static_cast<float>(x.value()),
                static_cast<float>(y.value()), color, nx::render::ScreenBlend::Additive));
        };
        const auto pictureWidth = [](const HudSpritePicture& picture) {
            return nx::Pixels{nx::narrow<std::int32_t>(picture.rect.width)};
        };
        const HudSpritePicture& icon = onGpu.pictures.icon(row.weapon);
        const HudSpritePicture& head = onGpu.pictures.head();
        const KillRowLayout layout =
            layoutKillRow({.killer = row.killer ? std::optional{widthOf(row.killer->name)} : std::nullopt,
                           .icon = pictureWidth(icon),
                           .head = row.isHead ? std::optional{pictureWidth(head)} : std::nullopt,
                           .victim = widthOf(row.victim.name)},
                          nx::narrow<std::size_t>(index), isPlayerDead, size);
        if (row.killer && layout.killerX) {
            writeName(*row.killer, *layout.killerX, layout.y);
        }
        addPicture(icon, layout.iconX, layout.y);
        if (layout.headX) {
            addPicture(head, *layout.headX, layout.y);
        }
        writeName(row.victim, layout.victimX, layout.y);
    }
}

} // namespace cs
