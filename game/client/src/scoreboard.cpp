// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;

namespace cs {

namespace {

/// The board's size and placement are measured in shares of a screen's height.
constexpr std::int32_t kUnitsTall = 480;
constexpr std::int32_t kPanelWideUnits = 520;
constexpr std::int32_t kPanelTallUnits = 340;
constexpr std::int32_t kPanelSideMarginUnits = 8;
constexpr std::int32_t kPanelVerticalMarginUnits = 4;
constexpr std::int32_t kPanelBottomInsetUnits = 5;
constexpr std::int32_t kTeamGapUnits = 8;
constexpr std::int32_t kRowPitchUnits = 21;
constexpr std::int32_t kTitleUnits = 5;
constexpr std::int32_t kPercentageTotal = 100;
constexpr std::int32_t kNameXPercent = 5;
constexpr std::int32_t kStatusXPercent = 42;
constexpr std::int32_t kHealthEndPercent = 54;
constexpr std::int32_t kMoneyEndPercent = 68;
constexpr std::int32_t kScoreEndPercent = 79;
constexpr std::int32_t kDeathsEndPercent = 91;

/// Where the panel's top rows run: on screens from this tall, in the larger of the original's two sizes.
constexpr nx::Pixels kLargerFrom{768};
/// What of the panel's top does not scale, but by those two sizes, from its top left.
struct FixedParts {
    nx::Pixels separatorY{};
    nx::Pixels teamTextY{};
    nx::Pixels columnHeadsY{};
    nx::Pixels teamLineY{};
};
constexpr FixedParts kSmallerParts{.separatorY = nx::Pixels{23},
                                   .teamTextY = nx::Pixels{35},
                                   .columnHeadsY = nx::Pixels{51},
                                   .teamLineY = nx::Pixels{67}};
constexpr FixedParts kLargerParts{.separatorY = nx::Pixels{24},
                                  .teamTextY = nx::Pixels{36},
                                  .columnHeadsY = nx::Pixels{52},
                                  .teamLineY = nx::Pixels{68}};
constexpr nx::Pixels kTitleGap{1};         ///< Before the title's pen, after its share over the heads.
constexpr nx::Pixels kLinesInset{6};       ///< From the panel's left to either team's line.
constexpr nx::Pixels kLinesRightInset{7};  ///< From the last team's line to the panel's right.
constexpr nx::Pixels kDividerThickness{1}; ///< Between the teams' columns.
constexpr nx::Pixels kRowTextLift{1};      ///< Slightly above the vertical center of a player's row.
constexpr nx::Pixels kBandUnderLine{2};    ///< From the team's line to the top of its first row's band.
constexpr nx::Pixels kBandInset{3};        ///< From either side of a team column to a band.
constexpr nx::Pixels kTextGap{3};          ///< Between a player's name and the status column.
constexpr nx::Pixels kLineThickness{1};    ///< Of the border, the corners' steps and the lines under heads.
/// An inset on each side of the panel: left and right, top and bottom.
constexpr std::int32_t kInsetSides = 2;
/// What a length is divided by for its middle.
constexpr std::int32_t kMiddleDivisor = 2;
/// Of each of the panel's top rows, how far in its corner rounds it.
constexpr auto kCornerInsets =
    std::to_array({nx::Pixels{5}, nx::Pixels{4}, nx::Pixels{2}, nx::Pixels{2}, nx::Pixels{1}});

// The colors, measured: a black and an orange half opaque over the scene, the heads' orange, a player's own
// row's white, the teams'.
constexpr nx::render::ScreenColor kPanelColor{.red = 0, .green = 0, .blue = 0, .alpha = 128};
constexpr nx::render::ScreenColor kBorderColor{.red = 188, .green = 112, .blue = 0, .alpha = 128};
constexpr nx::render::ScreenColor kHeadColor{.red = 255, .green = 176, .blue = 0};
constexpr nx::render::ScreenColor kBandColor{.red = 255, .green = 255, .blue = 255, .alpha = 25};
constexpr nx::EnumArray<Team, nx::render::ScreenColor> kTeamColors{
    nx::EnumArray<Team, nx::render::ScreenColor>::Storage{
        nx::render::ScreenColor{.red = 255, .green = 64, .blue = 64},
        nx::render::ScreenColor{.red = 153, .green = 204, .blue = 255}}};
/// What a team row calls each team, and how a row says its player is dead.
constexpr nx::EnumArray<Team, std::string_view> kTeamNames{
    nx::EnumArray<Team, std::string_view>::Storage{"Terrorists", "Counter-Terrorists"}};
constexpr std::string_view kDeadStatus = "Dead";
// The columns' heads, compact enough to fit either team column.
constexpr std::string_view kHealthHead = "HP";
constexpr std::string_view kMoneyHead = "Money";
constexpr std::string_view kScoreHead = "Score";
constexpr std::string_view kDeathsHead = "Deaths";
constexpr std::string_view kLatencyHead = "Ping";
constexpr std::string_view kNameHead = "Player";

/// `units` of the board's on a screen `height` tall, cut to whole pixels (the original's YRES).
[[nodiscard]] constexpr auto unitsOf(std::int32_t units, nx::Pixels height) noexcept -> nx::Pixels {
    return nx::Pixels{units * height.value() / kUnitsTall};
}

/// A share of `wide` expressed as a percentage.
[[nodiscard]] constexpr auto percentageOf(nx::Pixels wide, std::int32_t percentage) noexcept -> nx::Pixels {
    return (wide * percentage) / kPercentageTotal;
}

/// The first line of `text` that fits `wide`, with an ellipsis if the text was shortened.
[[nodiscard]] auto textThatFits(const nx::ui::GlyphAtlas& atlas, std::string_view text, nx::Pixels wide)
    -> std::string {
    if (nx::ui::widthOf(atlas, text) <= wide) {
        return std::string{text};
    }
    constexpr std::string_view kEllipsis = "…";
    const nx::Pixels ellipsisWide = nx::ui::widthOf(atlas, kEllipsis);
    if (wide <= ellipsisWide) {
        return {};
    }
    std::vector<std::string> lines = nx::ui::wrapText(atlas, text, wide - ellipsisWide);
    if (lines.empty()) {
        return std::string{kEllipsis};
    }
    lines.front() += kEllipsis;
    return std::move(lines.front());
}

/// Fills the panel `painter` draws in, of `size`, and draws its border, its corners rounded.
void paintPanel(nx::ui::PanelPainter& painter, nx::ui::PixelSize size) {
    const auto [wide, tall] = size;
    const nx::Pixels rounded{nx::narrow<std::int32_t>(kCornerInsets.size())};
    const nx::Pixels bottom = tall - kLineThickness;
    painter.fill({.y = rounded, .wide = wide, .tall = tall - (rounded * kInsetSides)}, kPanelColor);
    for (const auto& [row, inset] : std::views::enumerate(kCornerInsets)) {
        const nx::Pixels y{nx::narrow<std::int32_t>(row)};
        const nx::Pixels across = wide - (inset * kInsetSides);
        painter.fill({.x = inset, .y = y, .wide = across, .tall = kLineThickness}, kPanelColor);
        painter.fill({.x = inset, .y = bottom - y, .wide = across, .tall = kLineThickness}, kPanelColor);
    }
    const nx::Pixels top = kCornerInsets.front();
    const nx::Pixels edge = wide - (top * kInsetSides);
    const nx::Pixels side = tall - (rounded * kInsetSides);
    painter.fill({.x = top, .wide = edge, .tall = kLineThickness}, kBorderColor);
    painter.fill({.x = top, .y = bottom, .wide = edge, .tall = kLineThickness}, kBorderColor);
    painter.fill({.y = rounded, .wide = kLineThickness, .tall = side}, kBorderColor);
    painter.fill({.x = wide - kLineThickness, .y = rounded, .wide = kLineThickness, .tall = side},
                 kBorderColor);
    for (std::size_t row = 1; row < kCornerInsets.size(); ++row) {
        const nx::Pixels inset = kCornerInsets[row];
        const nx::Pixels run = std::max(kCornerInsets[row - 1] - inset, kLineThickness);
        const nx::Pixels y{nx::narrow<std::int32_t>(row)};
        for (const nx::Pixels at : {y, bottom - y}) {
            painter.fill({.x = inset, .y = at, .wide = run, .tall = kLineThickness}, kBorderColor);
            painter.fill({.x = wide - inset - run, .y = at, .wide = run, .tall = kLineThickness},
                         kBorderColor);
        }
    }
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

auto scoreTeamsOf(std::span<const ScoreRow> rows, const nx::EnumArray<Team, std::uint32_t>& wins,
                  Team ownTeam) -> std::vector<ScoreTeam> {
    std::vector<ScoreTeam> teams;
    const std::array sides{ownTeam, opponentOf(ownTeam)};
    for (const Team team : sides) {
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

auto teamRowText(Team team, std::size_t count) -> std::string {
    return std::format("{}   -   {} {}", kTeamNames[team], count, count == 1 ? "player" : "players");
}

auto scoreboardMetricsOf(nx::rhi::Extent2d size, nx::Pixels lineHeight, std::size_t rowsPerTeam) noexcept
    -> ScoreboardMetrics {
    const nx::Pixels height{nx::narrow<std::int32_t>(size.height)};
    const nx::Pixels screenWide{nx::narrow<std::int32_t>(size.width)};
    const FixedParts& fixed = height < kLargerFrom ? kSmallerParts : kLargerParts;
    const nx::Pixels sideMargin = std::min(unitsOf(kPanelSideMarginUnits, height), screenWide / kInsetSides);
    const nx::Pixels requestedWide = unitsOf(kPanelWideUnits, height);
    const nx::Pixels wide = std::min(requestedWide, screenWide - (sideMargin * kInsetSides));
    const nx::Pixels teamGap = unitsOf(kTeamGapUnits, height);
    const nx::Pixels innerWide = std::max(nx::Pixels{}, wide - kLinesInset - kLinesRightInset - teamGap);
    const nx::Pixels teamWide = innerWide / kInsetSides;
    const nx::Pixels maxPanelTall =
        std::max(nx::Pixels{}, height - (unitsOf(kPanelVerticalMarginUnits, height) * kInsetSides));
    const nx::Pixels bottomInset = unitsOf(kPanelBottomInsetUnits, height);
    const nx::Pixels rowArea = std::max(nx::Pixels{}, maxPanelTall - fixed.teamLineY - bottomInset);
    const std::int32_t rowCount = nx::narrow<std::int32_t>(rowsPerTeam);
    const nx::Pixels fittingPitch =
        rowsPerTeam > 0 ? nx::Pixels{rowArea.value() / rowCount} : unitsOf(kRowPitchUnits, height);
    const nx::Pixels pitch =
        rowsPerTeam > 0 ? std::min(unitsOf(kRowPitchUnits, height), std::max(lineHeight, fittingPitch))
                        : unitsOf(kRowPitchUnits, height);
    const nx::Pixels contentTall = fixed.teamLineY + (pitch * rowCount) + bottomInset;
    const nx::Pixels panelTall =
        std::min(maxPanelTall, std::max(unitsOf(kPanelTallUnits, height), contentTall));
    const nx::Pixels leftTeamX = kLinesInset;
    const nx::Pixels rightTeamX = leftTeamX + teamWide + teamGap;
    const nx::Pixels dividerX = leftTeamX + teamWide + (teamGap / kMiddleDivisor);
    return ScoreboardMetrics{
        .panel = {.x = (screenWide - wide) / kMiddleDivisor,
                  .y = std::max(nx::Pixels{}, (height - panelTall) / kMiddleDivisor),
                  .wide = wide,
                  .tall = panelTall},
        .titleX = unitsOf(kTitleUnits, height) + kTitleGap,
        .headsY = kTitleGap + unitsOf(1, height),
        .separatorY = fixed.separatorY,
        .teamTextY = fixed.teamTextY,
        .columnHeadsY = fixed.columnHeadsY,
        .teamLineY = fixed.teamLineY,
        .teamWide = teamWide,
        .leftTeamX = leftTeamX,
        .rightTeamX = rightTeamX,
        .dividerX = dividerX,
        .nameX = percentageOf(teamWide, kNameXPercent),
        .rowTextUnderLine = std::max(nx::Pixels{}, (pitch - lineHeight) / kMiddleDivisor - kRowTextLift),
        .statusX = percentageOf(teamWide, kStatusXPercent),
        .rowPitch = pitch,
        .healthEnd = percentageOf(teamWide, kHealthEndPercent),
        .moneyEnd = percentageOf(teamWide, kMoneyEndPercent),
        .scoreEnd = percentageOf(teamWide, kScoreEndPercent),
        .deathsEnd = percentageOf(teamWide, kDeathsEndPercent),
        .latencyEnd = teamWide,
        .bandUnderLine = kBandUnderLine,
        .bandTall = pitch,
    };
}

auto Scoreboard::open(const WindowFonts& fonts, std::uint32_t screenHeight)
    -> nx::Result<Scoreboard, WindowFontError> {
    auto font = fonts.font(kEngineFont, screenHeight);
    if (!font) {
        return std::unexpected{std::move(font.error())};
    }
    auto built = nx::ui::GlyphAtlas::build(*font, consoleCharacters());
    if (!built) {
        return nx::fail(WindowFontError::Unusable, std::string{kEngineFont}, std::move(built.error()));
    }
    return Scoreboard{std::move(*built)};
}

Scoreboard::Scoreboard(nx::ui::GlyphAtlas glyphAtlas) noexcept : atlas{std::move(glyphAtlas)} {}

auto Scoreboard::load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, nx::render::ScreenError> {
    auto image = screen.loadImage(commands, atlas.texels(), atlas.width(), atlas.height(), "scoreboard");
    if (!image) {
        return std::unexpected{std::move(image.error())};
    }
    glyphs.emplace(std::move(*image));
    return {};
}

void Scoreboard::append(std::vector<nx::render::ScreenQuad>& quads, const ScoreboardView& view,
                        nx::rhi::Extent2d size) {
    if (!glyphs) {
        return;
    }
    const nx::ui::TextFace face{.atlas = &atlas, .image = &*glyphs};
    shown.append(quads,
                 Shown{.hostname = std::string{view.hostname},
                       .rows = {view.rows.begin(), view.rows.end()},
                       .self = view.self,
                       .ownTeam = view.ownTeam,
                       .wins = view.wins,
                       .size = size},
                 [&](std::vector<nx::render::ScreenQuad>& laidOut) { layOut(laidOut, face, view, size); });
}

void Scoreboard::layOut(std::vector<nx::render::ScreenQuad>& quads, const nx::ui::TextFace& face,
                        const ScoreboardView& view, nx::rhi::Extent2d size) const {
    const std::vector<ScoreTeam> teams = scoreTeamsOf(view.rows, view.wins, view.ownTeam);
    const std::size_t rowsPerTeam = std::ranges::max(
        teams | std::views::transform([](const ScoreTeam& team) { return team.rows.size(); }));
    const ScoreboardMetrics metrics = scoreboardMetricsOf(size, atlas.lineHeight(), rowsPerTeam);
    nx::ui::PanelPainter screen{quads, size};
    nx::ui::PanelPainter painter = screen.inside(metrics.panel);
    paintPanel(painter, {.wide = metrics.panel.wide, .tall = metrics.panel.tall});
    const auto write = [&](std::string_view text, nx::Pixels x, nx::Pixels y, nx::render::ScreenColor color) {
        std::ignore = painter.text(face, text, {.x = x, .y = y}, color); // where its pen ends is no matter
    };
    // Right-aligned so that its last pixel is about `end`: its pen ends a pixel after it.
    const auto writeEndingAt = [&](std::string_view text, nx::Pixels end, nx::Pixels y,
                                   nx::render::ScreenColor color) {
        write(text, end + kLineThickness - nx::ui::widthOf(atlas, text), y, color);
    };
    const nx::Pixels lineWide = metrics.panel.wide - kLinesInset - kLinesRightInset;

    const std::string hostname{textThatFits(atlas, view.hostname, lineWide - metrics.titleX)};
    write(hostname, metrics.titleX, metrics.headsY, kHeadColor);
    painter.fill({.x = kLinesInset, .y = metrics.separatorY, .wide = lineWide, .tall = kLineThickness},
                 kBorderColor);
    painter.fill({.x = metrics.dividerX,
                  .y = metrics.separatorY + kLineThickness,
                  .wide = kDividerThickness,
                  .tall = std::max(nx::Pixels{}, metrics.panel.tall - metrics.separatorY - kLineThickness)},
                 kBorderColor);

    for (const auto& [teamIndex, team] : std::views::enumerate(teams)) {
        const nx::render::ScreenColor color = kTeamColors[team.team];
        const nx::Pixels teamX = teamIndex == 0 ? metrics.leftTeamX : metrics.rightTeamX;
        const nx::Pixels teamRight = teamX + metrics.latencyEnd;
        const nx::Pixels winsWide = nx::ui::widthOf(atlas, std::to_string(team.wins));
        const nx::Pixels teamNameWide =
            std::max(nx::Pixels{}, teamRight - metrics.nameX - winsWide - kTextGap);
        const std::string teamName =
            textThatFits(atlas, teamRowText(team.team, team.rows.size()), teamNameWide);
        write(teamName, teamX + metrics.nameX, metrics.teamTextY, color);
        writeEndingAt(std::to_string(team.wins), teamX + metrics.latencyEnd, metrics.teamTextY, color);

        const auto writeColumnHead = [&](std::string_view label, nx::Pixels start, nx::Pixels end) {
            const std::string fitted = textThatFits(atlas, label, end - start);
            writeEndingAt(fitted, teamX + end, metrics.columnHeadsY, kHeadColor);
        };
        const nx::Pixels nameEnd = metrics.statusX - kTextGap;
        writeColumnHead(kNameHead, metrics.nameX, nameEnd);
        writeColumnHead(kHealthHead, metrics.statusX, metrics.healthEnd);
        writeColumnHead(kMoneyHead, metrics.healthEnd, metrics.moneyEnd);
        writeColumnHead(kScoreHead, metrics.moneyEnd, metrics.scoreEnd);
        writeColumnHead(kDeathsHead, metrics.scoreEnd, metrics.deathsEnd);
        writeColumnHead(kLatencyHead, metrics.deathsEnd, metrics.latencyEnd);
        painter.fill({.x = teamX, .y = metrics.teamLineY, .wide = metrics.teamWide, .tall = kLineThickness},
                     color);

        nx::Pixels rowDown = metrics.teamLineY;
        for (const ScoreRow& row : team.rows) {
            if (row.player == view.self) {
                painter.fill({.x = teamX + kBandInset,
                              .y = rowDown + metrics.bandUnderLine,
                              .wide = metrics.teamWide - (kBandInset * kInsetSides),
                              .tall = metrics.bandTall},
                             kBandColor);
            }
            const nx::Pixels rowY = rowDown + metrics.rowTextUnderLine;
            const nx::Pixels nameWide = std::max(nx::Pixels{}, metrics.statusX - metrics.nameX - kTextGap);
            const std::string name = textThatFits(atlas, row.name, nameWide);
            write(name, teamX + metrics.nameX, rowY, color);
            if (row.isDead) {
                write(kDeadStatus, teamX + metrics.statusX, rowY, color);
            } else if (row.health) {
                writeEndingAt(healthDigits(*row.health), teamX + metrics.healthEnd, rowY, color);
            }
            if (row.money) {
                writeEndingAt(std::format("${}", moneyDigits(*row.money)), teamX + metrics.moneyEnd, rowY,
                              color);
            }
            writeEndingAt(std::to_string(row.score.frags), teamX + metrics.scoreEnd, rowY, color);
            writeEndingAt(std::to_string(row.score.deaths), teamX + metrics.deathsEnd, rowY, color);
            rowDown += metrics.rowPitch;
        }
    }
}

} // namespace cs
