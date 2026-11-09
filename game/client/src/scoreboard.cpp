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

/// The screens the board's columns are drawn for: its shares of a screen's height are of this many units.
constexpr std::int32_t kUnitsTall = 480;
// In those units (docs/12 section 3.2, measured at 800x600, 1024x768, 1280x720, 1280x960 and 1600x900).
constexpr std::int32_t kPanelWideUnits = 520;
constexpr std::int32_t kPanelTallUnits = 340;
constexpr std::int32_t kPanelTopUnits = 70;
constexpr std::int32_t kStatusUnits = 215;
constexpr std::int32_t kHealthEndUnits = 310;
constexpr std::int32_t kMoneyEndUnits = 367;
constexpr std::int32_t kScoreEndUnits = 412;
constexpr std::int32_t kDeathsEndUnits = 458;
constexpr std::int32_t kLatencyEndUnits = 508;
constexpr std::int32_t kRowCenterUnits = 12;
constexpr std::int32_t kRowPitchUnits = 21;
constexpr std::int32_t kTitleUnits = 5;

/// Where the panel's top rows run: on screens from this tall, in the larger of the original's two sizes.
constexpr nx::Pixels kLargerFrom{768};
/// What of the panel's top does not scale, but by those two sizes, from its top left.
struct FixedParts {
    nx::Pixels separatorY{};
    nx::Pixels teamTextY{};
    nx::Pixels teamLineY{};
    nx::Pixels nameX{};
};
constexpr FixedParts kSmallerParts{.separatorY = nx::Pixels{23},
                                   .teamTextY = nx::Pixels{35},
                                   .teamLineY = nx::Pixels{51},
                                   .nameX = nx::Pixels{45}};
constexpr FixedParts kLargerParts{.separatorY = nx::Pixels{24},
                                  .teamTextY = nx::Pixels{36},
                                  .teamLineY = nx::Pixels{53},
                                  .nameX = nx::Pixels{61}};
constexpr nx::Pixels kTitleGap{1};        ///< More than its share, before the title's pen and over the heads.
constexpr nx::Pixels kLinesInset{6};      ///< From the panel's left to the team's line.
constexpr nx::Pixels kLinesRightInset{7}; ///< From the team's line's end to the panel's right.
constexpr nx::Pixels kRowTextLift{1};     ///< Over a row's line of text centered, measured.
constexpr nx::Pixels kBandUnderLine{2};   ///< From the team's line to the top of its first row's band.
constexpr nx::Pixels kBandInset{5};       ///< From either side of the panel to a band.
constexpr nx::Pixels kLineThickness{1}; ///< Of the border, the corners' steps and the lines under the heads.
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
// The columns' heads, as the original writes them.
constexpr std::string_view kHealthHead = "HP";
constexpr std::string_view kMoneyHead = "Money";
constexpr std::string_view kScoreHead = "Score";
constexpr std::string_view kDeathsHead = "Deaths";
constexpr std::string_view kLatencyHead = "Latency";

/// `units` of the board's on a screen `height` tall, cut to whole pixels (the original's YRES).
[[nodiscard]] constexpr auto unitsOf(std::int32_t units, nx::Pixels height) noexcept -> nx::Pixels {
    return nx::Pixels{units * height.value() / kUnitsTall};
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

auto scoreTeamsOf(std::span<const ScoreRow> rows, const nx::EnumArray<Team, std::uint32_t>& wins)
    -> std::vector<ScoreTeam> {
    std::vector<ScoreTeam> teams;
    for (const Team team : nx::EnumArray<Team, std::uint32_t>::keys()) {
        ScoreTeam listed{.team = team, .wins = wins[team]};
        std::ranges::copy_if(rows, std::back_inserter(listed.rows),
                             [team](const ScoreRow& row) { return row.team == team; });
        if (listed.rows.empty()) {
            continue;
        }
        std::ranges::stable_sort(listed.rows, [](const ScoreRow& left, const ScoreRow& right) {
            return left.score.frags != right.score.frags ? left.score.frags > right.score.frags
                                                         : left.score.deaths < right.score.deaths;
        });
        teams.push_back(std::move(listed));
    }
    std::ranges::stable_sort(teams, std::ranges::greater{}, &ScoreTeam::wins);
    return teams;
}

auto teamRowText(Team team, std::size_t count) -> std::string {
    return std::format("{}   -   {} {}", kTeamNames[team], count, count == 1 ? "player" : "players");
}

auto scoreboardMetricsOf(nx::rhi::Extent2d size, nx::Pixels lineHeight) noexcept -> ScoreboardMetrics {
    const nx::Pixels height{nx::narrow<std::int32_t>(size.height)};
    const FixedParts& fixed = height < kLargerFrom ? kSmallerParts : kLargerParts;
    const nx::Pixels wide = unitsOf(kPanelWideUnits, height);
    const nx::Pixels pitch = unitsOf(kRowPitchUnits, height);
    return ScoreboardMetrics{
        .panel = {.x = (nx::Pixels{nx::narrow<std::int32_t>(size.width)} - wide) / kMiddleDivisor,
                  .y = unitsOf(kPanelTopUnits, height),
                  .wide = wide,
                  .tall = unitsOf(kPanelTallUnits, height)},
        .titleX = unitsOf(kTitleUnits, height) + kTitleGap,
        .headsY = kTitleGap + unitsOf(1, height),
        .separatorY = fixed.separatorY,
        .teamTextY = fixed.teamTextY,
        .teamLineY = fixed.teamLineY,
        .nameX = fixed.nameX,
        .rowTextUnderLine = unitsOf(kRowCenterUnits, height) - (lineHeight / kMiddleDivisor) - kRowTextLift,
        .statusX = unitsOf(kStatusUnits, height),
        .rowPitch = pitch,
        .healthEnd = unitsOf(kHealthEndUnits, height),
        .moneyEnd = unitsOf(kMoneyEndUnits, height),
        .scoreEnd = unitsOf(kScoreEndUnits, height),
        .deathsEnd = unitsOf(kDeathsEndUnits, height),
        .latencyEnd = unitsOf(kLatencyEndUnits, height),
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
                       .wins = view.wins,
                       .size = size},
                 [&](std::vector<nx::render::ScreenQuad>& laidOut) { layOut(laidOut, face, view, size); });
}

void Scoreboard::layOut(std::vector<nx::render::ScreenQuad>& quads, const nx::ui::TextFace& face,
                        const ScoreboardView& view, nx::rhi::Extent2d size) const {
    const ScoreboardMetrics metrics = scoreboardMetricsOf(size, atlas.lineHeight());
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

    write(view.hostname, metrics.titleX, metrics.headsY, kHeadColor);
    writeEndingAt(kHealthHead, metrics.healthEnd, metrics.headsY, kHeadColor);
    writeEndingAt(kMoneyHead, metrics.moneyEnd, metrics.headsY, kHeadColor);
    writeEndingAt(kScoreHead, metrics.scoreEnd, metrics.headsY, kHeadColor);
    writeEndingAt(kDeathsHead, metrics.deathsEnd, metrics.headsY, kHeadColor);
    writeEndingAt(kLatencyHead, metrics.latencyEnd, metrics.headsY, kHeadColor);
    painter.fill({.x = kLinesInset, .y = metrics.separatorY, .wide = lineWide, .tall = kLineThickness},
                 kBorderColor);

    // Each team's rows under its own, one row's room between the teams.
    nx::Pixels down{};
    for (const ScoreTeam& team : scoreTeamsOf(view.rows, view.wins)) {
        const nx::render::ScreenColor color = kTeamColors[team.team];
        const nx::Pixels textY = metrics.teamTextY + down;
        const nx::Pixels lineY = metrics.teamLineY + down;
        write(teamRowText(team.team, team.rows.size()), metrics.nameX, textY, color);
        writeEndingAt(std::to_string(team.wins), metrics.scoreEnd, textY, color);
        painter.fill({.x = kLinesInset, .y = lineY, .wide = lineWide, .tall = kLineThickness}, color);
        nx::Pixels rowDown = lineY;
        for (const ScoreRow& row : team.rows) {
            if (row.player == view.self) {
                painter.fill({.x = kBandInset,
                              .y = rowDown + metrics.bandUnderLine,
                              .wide = metrics.panel.wide - (kBandInset * kInsetSides),
                              .tall = metrics.bandTall},
                             kBandColor);
            }
            const nx::Pixels rowY = rowDown + metrics.rowTextUnderLine;
            write(row.name, metrics.nameX, rowY, color);
            if (row.isDead) {
                write(kDeadStatus, metrics.statusX, rowY, color);
            } else if (row.health) {
                writeEndingAt(healthDigits(*row.health), metrics.healthEnd, rowY, color);
            }
            if (row.money) {
                writeEndingAt(std::format("${}", moneyDigits(*row.money)), metrics.moneyEnd, rowY, color);
            }
            writeEndingAt(std::to_string(row.score.frags), metrics.scoreEnd, rowY, color);
            writeEndingAt(std::to_string(row.score.deaths), metrics.deathsEnd, rowY, color);
            rowDown += metrics.rowPitch;
        }
        down = rowDown + metrics.rowPitch - metrics.teamLineY;
    }
}

} // namespace cs
