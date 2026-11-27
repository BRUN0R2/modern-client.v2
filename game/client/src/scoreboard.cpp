// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.assets;
import nx.platform;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

/// The sizes of text, on the menus' design 1080 lines tall.
constexpr nx::EnumArray<ScoreboardFont, MenuFontSize> kFontSizes{
    nx::EnumArray<ScoreboardFont, MenuFontSize>::Storage{
        MenuFontSize{.line = MenuUnits{34}, .isStrong = true},  // Title
        MenuFontSize{.line = MenuUnits{22}, .isStrong = false}, // Subtitle
        MenuFontSize{.line = MenuUnits{34}, .isStrong = true},  // Team
        MenuFontSize{.line = MenuUnits{76}, .isStrong = true},  // Wins
        MenuFontSize{.line = MenuUnits{19}, .isStrong = true},  // Head
        MenuFontSize{.line = MenuUnits{25}, .isStrong = false}, // Row
        MenuFontSize{.line = MenuUnits{15}, .isStrong = true}}}; // Tag

// Where its parts go, in the design's units.
constexpr MenuUnits kPanelWide{1480};    ///< At most; less on a narrower screen.
constexpr MenuUnits kScreenMargin{32};   ///< Least, at either side of the screen.
constexpr MenuUnits kPanelTop{72};
constexpr MenuUnits kBottomMargin{24};
constexpr MenuUnits kHeaderTall{90};
constexpr MenuUnits kGap{12};            ///< Between the header, the teams' banners and their tables.
constexpr MenuUnits kHalvesGap{14};      ///< Between the teams.
constexpr MenuUnits kBannerTall{112};
constexpr MenuUnits kHeadTall{42};
constexpr MenuUnits kRowPitch{50};       ///< At most; less when the rows would not fit.
constexpr MenuUnits kLeastRowPitch{26};
constexpr MenuUnits kRowGap{2};
constexpr MenuUnits kInset{24};          ///< Inside the header and the banners.
constexpr MenuUnits kTitleTop{12};
constexpr MenuUnits kBannerTextTop{20};
constexpr MenuUnits kBannerLinesGap{4};
constexpr MenuUnits kEmblemInset{16};    ///< Over and under a team's emblem in its banner.
constexpr MenuUnits kEmblemGap{18};      ///< After it, before the team's name.
constexpr MenuUnits kAccentWide{4};
constexpr MenuUnits kAccentTall{2};      ///< Of the line under a banner.
constexpr MenuUnits kCellPad{16};        ///< Inside a table, at its sides.
constexpr MenuUnits kAvatar{36};
constexpr MenuUnits kAvatarInset{4};     ///< Least, over and under an avatar in its row.
constexpr MenuUnits kAvatarGap{14};
constexpr MenuUnits kDeathsWide{60};
constexpr MenuUnits kKillsWide{64};
constexpr MenuUnits kMoneyWide{120};
constexpr MenuUnits kHealthWide{110};
constexpr MenuUnits kColumnGap{8};
constexpr MenuUnits kBarWide{90};
constexpr MenuUnits kBarTall{5};
constexpr MenuUnits kBarGap{3};
constexpr MenuUnits kTagPad{8};
constexpr MenuUnits kTagTall{24};
constexpr MenuUnits kTagGap{12};
constexpr MenuUnits kChipPad{16};
constexpr MenuUnits kChipTall{40};

/// What a length is divided by for its middle; how many sides a thing is padded on.
constexpr std::int32_t kHalf = 2;
constexpr std::int32_t kBothSides = 2;
/// The health a full bar shows, and from how little it is low.
constexpr float kFullHealth = 100.0F;
constexpr float kLowHealth = 25.0F;
constexpr std::string_view kEllipsis = "...";
constexpr nx::log::Category kLog{"client"};
/// What an emblem is tinted.
constexpr nx::render::ScreenColor kEmblemColor{.red = 238, .green = 241, .blue = 245, .alpha = 235};

// The colors: a dark glass over the scene, each team's accent, white ink.
constexpr nx::render::ScreenColor kHeaderColor{.red = 16, .green = 20, .blue = 26, .alpha = 232};
constexpr nx::render::ScreenColor kTableColor{.red = 14, .green = 18, .blue = 24, .alpha = 218};
constexpr nx::render::ScreenColor kHeadRowColor{.red = 8, .green = 10, .blue = 14, .alpha = 150};
constexpr nx::render::ScreenColor kRowColor{.red = 255, .green = 255, .blue = 255, .alpha = 8};
constexpr nx::render::ScreenColor kRowOtherColor{.red = 255, .green = 255, .blue = 255, .alpha = 14};
constexpr nx::render::ScreenColor kSelfRowColor{.red = 255, .green = 255, .blue = 255, .alpha = 30};
constexpr nx::render::ScreenColor kLineColor{.red = 255, .green = 255, .blue = 255, .alpha = 40};
constexpr nx::render::ScreenColor kInk{.red = 238, .green = 241, .blue = 245};
constexpr nx::render::ScreenColor kInkSoft{.red = 182, .green = 190, .blue = 200};
constexpr nx::render::ScreenColor kInkFaint{.red = 122, .green = 130, .blue = 142};
constexpr nx::render::ScreenColor kMoneyColor{.red = 140, .green = 206, .blue = 120};
constexpr nx::render::ScreenColor kLowHealthColor{.red = 236, .green = 86, .blue = 72};
constexpr nx::render::ScreenColor kBarGround{.red = 255, .green = 255, .blue = 255, .alpha = 30};
constexpr nx::render::ScreenColor kDeadTile{.red = 44, .green = 48, .blue = 56, .alpha = 230};

/// A team's look: its accent, its banner and its players' tiles.
struct TeamLook {
    nx::render::ScreenColor accent{};
    nx::render::ScreenColor banner{};
    nx::render::ScreenColor tile{};
};
constexpr nx::EnumArray<Team, TeamLook> kTeamLooks{nx::EnumArray<Team, TeamLook>::Storage{
    TeamLook{.accent = {.red = 226, .green = 150, .blue = 72},
             .banner = {.red = 62, .green = 42, .blue = 26, .alpha = 236},
             .tile = {.red = 112, .green = 72, .blue = 40, .alpha = 240}},
    TeamLook{.accent = {.red = 98, .green = 162, .blue = 226},
             .banner = {.red = 26, .green = 46, .blue = 70, .alpha = 236},
             .tile = {.red = 40, .green = 74, .blue = 112, .alpha = 240}}}};

/// The bytes of the UTF-8 character `text` starts with.
[[nodiscard]] auto firstCharacterOf(std::string_view text) noexcept -> std::string_view {
    constexpr unsigned char kContinuationMask = 0xC0U;
    constexpr unsigned char kContinuation = 0x80U;
    std::size_t length = text.empty() ? 0 : 1;
    while (length < text.size() &&
           (static_cast<unsigned char>(text[length]) & kContinuationMask) == kContinuation) {
        ++length;
    }
    return text.substr(0, length);
}

/// What a player's tile shows: the first character of its name, a letter in capitals.
[[nodiscard]] auto initialOf(std::string_view name) -> std::string {
    std::string initial{firstCharacterOf(name)};
    if (initial.empty()) {
        return "?";
    }
    if (initial.size() == 1) {
        initial.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(initial.front())));
    }
    return initial;
}

/// The name of `team` in `texts`.
[[nodiscard]] auto teamNameOf(Team team, const ScoreboardTexts& texts) -> const std::string& {
    return team == Team::CounterTerrorists ? texts.counterTerrorists : texts.terrorists;
}

} // namespace

auto ScoreboardControls::registerIn(nx::console::Console& console)
    -> nx::Result<ScoreboardControls, ScoreboardError> {
    auto shown =
        console.registerButton({.name = "showscores", .description = "Shows the scoreboard while held."});
    if (!shown) {
        return nx::fail(ScoreboardError::RegistrationFailed, std::move(shown.error()));
    }
    auto hostname = console.registerCvar(
        nx::console::CvarDesc<std::string>{.name = "hostname",
                                           .defaultValue = std::string{kDefaultHostname},
                                           .description = "The server's name, which heads the scoreboard."});
    if (!hostname) {
        return nx::fail(ScoreboardError::RegistrationFailed, std::move(hostname.error()));
    }
    return ScoreboardControls{.shown = *shown, .hostname = *hostname};
}

auto ScoreTeam::aliveCount() const noexcept -> std::size_t {
    return nx::narrow<std::size_t>(std::ranges::count(rows, false, &ScoreRow::isDead));
}

auto scoreTeamsOf(std::span<const ScoreRow> rows, const nx::EnumArray<Team, std::uint32_t>& wins)
    -> std::vector<ScoreTeam> {
    std::vector<ScoreTeam> teams;
    for (const Team team : {Team::CounterTerrorists, Team::Terrorists}) {
        ScoreTeam listed{.team = team, .wins = wins[team]};
        std::ranges::copy_if(rows, std::back_inserter(listed.rows),
                             [team](const ScoreRow& row) { return row.team == team; });
        std::ranges::stable_sort(listed.rows, [](const ScoreRow& left, const ScoreRow& right) {
            return left.score.frags != right.score.frags ? left.score.frags > right.score.frags
                                                         : left.score.deaths < right.score.deaths;
        });
        teams.push_back(std::move(listed));
    }
    return teams;
}

auto elidedTo(const nx::ui::GlyphAtlas& atlas, std::string_view text, nx::Pixels wide) -> std::string {
    if (nx::ui::widthOf(atlas, text) <= wide) {
        return std::string{text};
    }
    constexpr unsigned char kContinuationMask = 0xC0U;
    constexpr unsigned char kContinuation = 0x80U;
    std::string cut{text};
    while (!cut.empty()) {
        // Its last character: its continuation bytes, then the byte that leads them.
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & kContinuationMask) == kContinuation) {
            cut.pop_back();
        }
        if (!cut.empty()) {
            cut.pop_back();
        }
        std::string shown = cut + std::string{kEllipsis};
        if (nx::ui::widthOf(atlas, shown) <= wide) {
            return shown;
        }
    }
    return {};
}

auto Scoreboard::open(nx::ui::FontCatalog& fonts, const MenuFontFamilies& families, std::uint32_t screenHeight,
                      std::span<const std::filesystem::path> folders) -> nx::Result<Scoreboard, MenuPanelError> {
    Atlases made;
    for (const ScoreboardFont font : Atlases::keys()) {
        auto atlas = makeMenuAtlas(fonts, families, kFontSizes[font], screenHeight);
        if (!atlas) {
            return std::unexpected{std::move(atlas.error())};
        }
        made[font].emplace(std::move(*atlas));
    }
    Emblems read;
    for (const Team team : Emblems::keys()) {
        const auto found = std::ranges::find_if(folders, [team](const std::filesystem::path& folder) {
            std::error_code error;
            return std::filesystem::is_regular_file(folder / kScoreboardEmblems[team], error);
        });
        if (found == folders.end()) {
            nx::log::warning(kLog, "the scoreboard shows no emblem of a team: no {}", kScoreboardEmblems[team]);
            continue;
        }
        const std::filesystem::path path = *found / kScoreboardEmblems[team];
        auto bytes = nx::platform::readFile(path);
        if (!bytes) {
            nx::log::warning(kLog, "the scoreboard shows no emblem of a team: {}", nx::describe(bytes.error()));
            continue;
        }
        auto picture = nx::assets::readTga(*bytes);
        if (!picture) {
            nx::log::warning(kLog, "the scoreboard shows no emblem of a team: {}: {}", nx::platform::toUtf8(path),
                             nx::describe(picture.error()));
            continue;
        }
        read[team].emplace(std::move(*picture));
    }
    return Scoreboard{std::move(made), std::move(read)};
}

Scoreboard::Scoreboard(Atlases made, Emblems read) noexcept
    : atlases{std::move(made)}, emblems{std::move(read)} {}

auto Scoreboard::load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, nx::render::ScreenError> {
    for (const ScoreboardFont font : Atlases::keys()) {
        const nx::ui::GlyphAtlas& atlas = *atlases[font];
        auto image = screen.loadImage(commands, atlas.texels(), atlas.width(), atlas.height(), "scoreboard");
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }
        images[font].emplace(std::move(*image));
    }
    for (const Team team : Emblems::keys()) {
        if (const std::optional<nx::assets::RgbaImage>& emblem = emblems[team]) {
            auto image = screen.loadImage(commands, emblem->pixels, emblem->width, emblem->height,
                                          "scoreboard.emblem");
            if (!image) {
                return std::unexpected{std::move(image.error())};
            }
            emblemImages[team].emplace(std::move(*image));
        }
    }
    shown.forget(); // its quads were of the images before
    return {};
}

void Scoreboard::append(std::vector<nx::render::ScreenQuad>& quads, const ScoreboardView& view,
                        nx::rhi::Extent2d size) {
    if (view.texts == nullptr ||
        std::ranges::any_of(Atlases::keys(), [&](ScoreboardFont font) { return !images[font].has_value(); })) {
        return;
    }
    Faces faces;
    for (const ScoreboardFont font : Atlases::keys()) {
        faces[font] = {.atlas = &*atlases[font], .image = &*images[font]};
    }
    shown.append(quads,
                 Shown{.hostname = std::string{view.hostname},
                       .map = std::string{view.map},
                       .rows = {view.rows.begin(), view.rows.end()},
                       .self = view.self,
                       .wins = view.wins,
                       .texts = *view.texts,
                       .size = size},
                 [&](std::vector<nx::render::ScreenQuad>& laidOut) { layOut(laidOut, faces, view, size); });
}

void Scoreboard::layOut(std::vector<nx::render::ScreenQuad>& quads, const Faces& faces,
                        const ScoreboardView& view, nx::rhi::Extent2d size) const {
    const ScoreboardTexts& texts = *view.texts;
    const MenuScale scale{size.height};
    const auto px = [&](MenuUnits units) { return scale.pixels(units); };
    const nx::Pixels screenWide{nx::narrow<std::int32_t>(size.width)};
    const nx::Pixels screenTall{nx::narrow<std::int32_t>(size.height)};
    const nx::Pixels wide = std::min(px(kPanelWide), screenWide - (px(kScreenMargin) * kBothSides));
    const nx::Pixels top = px(kPanelTop);
    const nx::Pixels panelX = (screenWide - wide) / kHalf;
    nx::ui::PanelPainter screen{quads, size};
    nx::ui::PanelPainter painter = screen.inside({.x = panelX, .y = top, .wide = wide, .tall = screenTall - top});

    const auto lineOf = [&](ScoreboardFont font) { return faces[font].atlas->lineHeight(); };
    const auto widthIn = [&](ScoreboardFont font, std::string_view text) {
        return nx::ui::widthOf(*faces[font].atlas, text);
    };
    const auto write = [&](ScoreboardFont font, std::string_view text, nx::Pixels x, nx::Pixels y,
                           nx::render::ScreenColor color) {
        std::ignore = painter.text(faces[font], text, {.x = x, .y = y}, color); // where its pen ends is no matter
    };
    const auto writeEndingAt = [&](ScoreboardFont font, std::string_view text, nx::Pixels end, nx::Pixels y,
                                   nx::render::ScreenColor color) {
        write(font, text, end - widthIn(font, text), y, color);
    };
    const auto writeAround = [&](ScoreboardFont font, std::string_view text, nx::Pixels middle, nx::Pixels y,
                                 nx::render::ScreenColor color) {
        write(font, text, middle - (widthIn(font, text) / kHalf), y, color);
    };
    // The top of a line of `font` in the middle of what is `tall` from `y`.
    const auto middleOf = [&](ScoreboardFont font, nx::Pixels y, nx::Pixels tall) {
        return y + ((tall - lineOf(font)) / kHalf);
    };

    // The header: what heads the board, the map under it, and how many play.
    const nx::Pixels headerTall = px(kHeaderTall);
    painter.fill({.wide = wide, .tall = headerTall}, kHeaderColor);
    painter.fill({.wide = px(kAccentWide), .tall = headerTall}, kInkSoft);
    const nx::Pixels titleX = px(kInset);
    if (view.map.empty()) {
        write(ScoreboardFont::Title, view.hostname, titleX, middleOf(ScoreboardFont::Title, {}, headerTall), kInk);
    } else {
        write(ScoreboardFont::Title, view.hostname, titleX, px(kTitleTop), kInk);
        write(ScoreboardFont::Subtitle, view.map, titleX, px(kTitleTop) + lineOf(ScoreboardFont::Title),
              kInkSoft);
    }
    const std::string playing =
        std::format("{} {}", view.rows.size(), view.rows.size() == 1 ? texts.onePlayer : texts.players);
    const nx::Pixels chipWide = widthIn(ScoreboardFont::Subtitle, playing) + (px(kChipPad) * kBothSides);
    const nx::ui::PixelRect chip{.x = wide - px(kInset) - chipWide,
                                 .y = (headerTall - px(kChipTall)) / kHalf,
                                 .wide = chipWide,
                                 .tall = px(kChipTall)};
    paintMenuOutline(painter, chip, kLineColor);
    writeAround(ScoreboardFont::Subtitle, playing, chip.x + (chip.wide / kHalf),
                middleOf(ScoreboardFont::Subtitle, chip.y, chip.tall), kInk);

    // The teams, side by side; their rows as close as they must be to fit on the screen.
    const std::vector<ScoreTeam> teams = scoreTeamsOf(view.rows, view.wins);
    const nx::Pixels halfWide = (wide - px(kHalvesGap)) / kHalf;
    const nx::Pixels bannerY = headerTall + px(kGap);
    const nx::Pixels bannerTall = px(kBannerTall);
    const nx::Pixels tableY = bannerY + bannerTall + px(kGap);
    const nx::Pixels headTall = px(kHeadTall);
    const nx::Pixels rowsY = tableY + headTall;
    const std::size_t mostRows =
        std::max<std::size_t>(std::ranges::max(teams, {}, [](const ScoreTeam& team) { return team.rows.size(); })
                                  .rows.size(),
                              1);
    const nx::Pixels room = screenTall - top - rowsY - px(kBottomMargin);
    const nx::Pixels pitch =
        std::clamp(room / nx::narrow<std::int32_t>(mostRows), px(kLeastRowPitch), px(kRowPitch));
    const nx::Pixels rowTall = pitch - px(kRowGap);

    for (const auto& [index, team] : std::views::enumerate(teams)) {
        const TeamLook& look = kTeamLooks[team.team];
        const nx::Pixels left = index == 0 ? nx::Pixels{} : wide - halfWide;
        const nx::Pixels right = left + halfWide;

        // Its banner: its name, how many of it live, its wins.
        painter.fill({.x = left, .y = bannerY, .wide = halfWide, .tall = bannerTall}, look.banner);
        painter.fill({.x = left, .y = bannerY, .wide = px(kAccentWide), .tall = bannerTall}, look.accent);
        painter.fill({.x = left, .y = bannerY + bannerTall - px(kAccentTall), .wide = halfWide, .tall = px(kAccentTall)},
                     look.accent);
        nx::Pixels bannerX = left + px(kAccentWide) + px(kInset);
        if (const std::optional<nx::render::ScreenImage>& emblem = emblemImages[team.team]) {
            // As tall as the banner leaves it, as wide as its picture is for that, smoothed.
            const nx::Pixels emblemTall = bannerTall - (px(kEmblemInset) * kBothSides);
            const auto emblemWide = static_cast<float>(emblemTall.value()) * static_cast<float>(emblem->width()) /
                                    static_cast<float>(emblem->height());
            quads.push_back({.image = &*emblem,
                             .source = {.width = emblem->width(), .height = emblem->height()},
                             .target = {.x = static_cast<float>((panelX + bannerX).value()),
                                        .y = static_cast<float>((top + bannerY + px(kEmblemInset)).value()),
                                        .width = emblemWide,
                                        .height = static_cast<float>(emblemTall.value())},
                             .color = kEmblemColor,
                             .filter = nx::render::ScreenFilter::Linear});
            bannerX += nx::Pixels{nx::narrow<std::int32_t>(std::lround(emblemWide))} + px(kEmblemGap);
        }
        const nx::Pixels nameY = bannerY + px(kBannerTextTop);
        write(ScoreboardFont::Team, teamNameOf(team.team, texts), bannerX, nameY, kInk);
        write(ScoreboardFont::Row, std::format("{}/{} {}", team.aliveCount(), team.rows.size(), texts.alive),
              bannerX, nameY + lineOf(ScoreboardFont::Team) + px(kBannerLinesGap), kInkSoft);
        writeEndingAt(ScoreboardFont::Wins, std::to_string(team.wins), right - px(kInset),
                      middleOf(ScoreboardFont::Wins, bannerY, bannerTall), kInk);

        // Its table: the heads, then a row of each player; health and money where the board knows them.
        const bool hasEconomy = std::ranges::any_of(
            team.rows, [](const ScoreRow& row) { return row.health.has_value() || row.money.has_value(); });
        const nx::Pixels deathsMiddle = right - px(kCellPad) - (px(kDeathsWide) / kHalf);
        const nx::Pixels killsMiddle = deathsMiddle - ((px(kDeathsWide) + px(kKillsWide)) / kHalf);
        const nx::Pixels killsStart = killsMiddle - (px(kKillsWide) / kHalf);
        const nx::Pixels moneyEnd = killsStart - px(kColumnGap);
        const nx::Pixels healthMiddle = moneyEnd - px(kMoneyWide) - (px(kHealthWide) / kHalf);
        const nx::Pixels nameX = left + px(kCellPad) + px(kAvatar) + px(kAvatarGap);
        const nx::Pixels nameEnd =
            (hasEconomy ? healthMiddle - (px(kHealthWide) / kHalf) : killsStart) - px(kColumnGap);

        const nx::Pixels shownRows = pitch * nx::narrow<std::int32_t>(std::max<std::size_t>(team.rows.size(), 1));
        painter.fill({.x = left, .y = tableY, .wide = halfWide, .tall = headTall + shownRows}, kTableColor);
        painter.fill({.x = left, .y = tableY, .wide = halfWide, .tall = headTall}, kHeadRowColor);
        const nx::Pixels headY = middleOf(ScoreboardFont::Head, tableY, headTall);
        write(ScoreboardFont::Head, texts.player, nameX, headY, kInkSoft);
        if (hasEconomy) {
            writeAround(ScoreboardFont::Head, texts.health, healthMiddle, headY, kInkSoft);
            writeEndingAt(ScoreboardFont::Head, texts.money, moneyEnd, headY, kInkSoft);
        }
        writeAround(ScoreboardFont::Head, texts.kills, killsMiddle, headY, kInkSoft);
        writeAround(ScoreboardFont::Head, texts.deaths, deathsMiddle, headY, kInkSoft);

        for (const auto& [place, row] : std::views::enumerate(team.rows)) {
            const nx::Pixels rowY = rowsY + (pitch * nx::narrow<std::int32_t>(place));
            const bool isSelf = row.player == view.self;
            painter.fill({.x = left, .y = rowY, .wide = halfWide, .tall = rowTall},
                         isSelf ? kSelfRowColor : (place % kHalf == 0 ? kRowColor : kRowOtherColor));
            if (isSelf) {
                painter.fill({.x = left, .y = rowY, .wide = px(kAccentWide), .tall = rowTall}, look.accent);
            }
            const nx::Pixels textY = middleOf(ScoreboardFont::Row, rowY, rowTall);

            // Its tile, the initial of its name on its team's color.
            const nx::Pixels tile = std::min(px(kAvatar), rowTall - (px(kAvatarInset) * kBothSides));
            const nx::ui::PixelRect avatar{
                .x = left + px(kCellPad), .y = rowY + ((rowTall - tile) / kHalf), .wide = tile, .tall = tile};
            painter.fill(avatar, row.isDead ? kDeadTile : look.tile);
            writeAround(ScoreboardFont::Head, initialOf(row.name), avatar.x + (tile / kHalf),
                        middleOf(ScoreboardFont::Head, avatar.y, tile), row.isDead ? kInkFaint : kInk);

            // Its name, and whether it is dead: in the health's column, or after the name without it.
            const nx::Pixels tagWide = widthIn(ScoreboardFont::Tag, texts.dead) + (px(kTagPad) * kBothSides);
            const nx::Pixels nameRoom =
                nameEnd - nameX - (row.isDead && !hasEconomy ? tagWide + px(kTagGap) : nx::Pixels{});
            const std::string name = elidedTo(*faces[ScoreboardFont::Row].atlas, row.name, nameRoom);
            write(ScoreboardFont::Row, name, nameX, textY, row.isDead ? kInkFaint : kInk);
            if (row.isDead) {
                const nx::Pixels tagX = hasEconomy ? healthMiddle - (tagWide / kHalf)
                                                   : nameX + widthIn(ScoreboardFont::Row, name) + px(kTagGap);
                const nx::ui::PixelRect tag{
                    .x = tagX, .y = rowY + ((rowTall - px(kTagTall)) / kHalf), .wide = tagWide, .tall = px(kTagTall)};
                painter.fill(tag, kHeadRowColor);
                paintMenuOutline(painter, tag, kLineColor);
                writeAround(ScoreboardFont::Tag, texts.dead, tag.x + (tagWide / kHalf),
                            middleOf(ScoreboardFont::Tag, tag.y, tag.tall), kInkSoft);
            } else if (row.health) {
                // The number over a bar of it, red when low.
                const float health = row.health->value();
                const bool isLow = health <= kLowHealth;
                const nx::Pixels barTall = px(kBarTall);
                const nx::Pixels stackTop =
                    rowY + ((rowTall - (lineOf(ScoreboardFont::Row) + px(kBarGap) + barTall)) / kHalf);
                writeAround(ScoreboardFont::Row, healthDigits(*row.health), healthMiddle, stackTop,
                            isLow ? kLowHealthColor : kInk);
                const nx::Pixels barWide = px(kBarWide);
                const nx::ui::PixelRect bar{.x = healthMiddle - (barWide / kHalf),
                                            .y = stackTop + lineOf(ScoreboardFont::Row) + px(kBarGap),
                                            .wide = barWide,
                                            .tall = barTall};
                painter.fill(bar, kBarGround);
                const float share = std::clamp(health / kFullHealth, 0.0F, 1.0F);
                const nx::Pixels filled{
                    nx::narrow<std::int32_t>(std::lround(share * static_cast<float>(barWide.value())))};
                if (filled > nx::Pixels{}) {
                    painter.fill({.x = bar.x, .y = bar.y, .wide = filled, .tall = barTall},
                                 isLow ? kLowHealthColor : look.accent);
                }
            }
            if (row.money) {
                writeEndingAt(ScoreboardFont::Row, std::format("${}", moneyDigits(*row.money)), moneyEnd, textY,
                              kMoneyColor);
            }
            writeAround(ScoreboardFont::Row, std::to_string(row.score.frags), killsMiddle, textY, kInk);
            writeAround(ScoreboardFont::Row, std::to_string(row.score.deaths), deathsMiddle, textY, kInkSoft);
        }
    }
}

} // namespace cs
