// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The scoreboard (docs/12-ui-hud-and-console.md, section 3.2), shown while +showscores is held: the players
/// of each team under its name, its count and its wins, each by its name, whether it is dead, its health and
/// its money, its teammates' alone (cs::ScoreRow), its score and its deaths; where Counter-Strike 1.6's
/// client draws them, measured on it. Drawn as panels draw (nx::ui::PanelPainter), its layout the project's
/// own (ADR-0020), in kEngineFont where the original has bitmap fonts of its own (TODO.md); no avatar and no
/// latency yet.
export module cs.client:scoreboard;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import :windowFonts;

export namespace cs {

/// The name the board is headed by until a server is given one: `hostname`'s default.
inline constexpr std::string_view kDefaultHostname = "Counter-Strike 1.6";

/// Why the board's controls cannot be had.
enum class ScoreboardError : std::uint8_t {
    RegistrationFailed, ///< The console refused a name of theirs (cause).
};

/// `+showscores`, whether the board shows, and `hostname`, what heads it.
struct ScoreboardControls {
    nx::console::Button shown;
    nx::console::Cvar<std::string> hostname;

    /// Registers them in `console`. Errors: RegistrationFailed.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<ScoreboardControls, ScoreboardError>;
};

/// A team's part of the board: its wins, and its players as the board lists them.
struct ScoreTeam {
    Team team{};
    std::uint32_t wins = 0;
    std::vector<ScoreRow> rows{};

    [[nodiscard]] inline auto operator==(const ScoreTeam&) const noexcept -> bool = default;
};

/// The teams of `rows` as the board lists them, those of no player left out: the one of more wins first, the
/// terrorists of as many; each team's players from the most frags, of as many the fewest deaths, of as many
/// by their slots.
[[nodiscard]] auto scoreTeamsOf(std::span<const ScoreRow> rows,
                                const nx::EnumArray<Team, std::uint32_t>& wins) -> std::vector<ScoreTeam>;

/// What the row of `team`, of `count` players, says: "Terrorists   -   1 player".
[[nodiscard]] auto teamRowText(Team team, std::size_t count) -> std::string;

/// Where the board's parts go on a screen, in pixels, those inside the panel from its top left: the panel;
/// the title's line and the columns' heads' top; the line under them; a team's row of text and the line
/// under it, and the pen of a team's and of a player's name; where a row's line of text starts under the
/// team's line, a row's status starts, and how far apart rows are; where each right-aligned column ends, its
/// last pixel; and the band of a player's own row, under the team's line.
struct ScoreboardMetrics {
    nx::ui::PixelRect panel{};
    nx::Pixels titleX{};
    nx::Pixels headsY{};
    nx::Pixels separatorY{};
    nx::Pixels teamTextY{};
    nx::Pixels teamLineY{};
    nx::Pixels nameX{};
    nx::Pixels rowTextUnderLine{};
    nx::Pixels statusX{};
    nx::Pixels rowPitch{};
    nx::Pixels healthEnd{};
    nx::Pixels moneyEnd{};
    nx::Pixels scoreEnd{};
    nx::Pixels deathsEnd{};
    nx::Pixels latencyEnd{};
    nx::Pixels bandUnderLine{};
    nx::Pixels bandTall{};

    [[nodiscard]] inline auto operator==(const ScoreboardMetrics&) const noexcept -> bool = default;
};

/// The board's parts on a screen of `size`, written in a font of lines `lineHeight` tall (docs/12
/// section 3.2).
[[nodiscard]] auto scoreboardMetricsOf(nx::rhi::Extent2d size, nx::Pixels lineHeight) noexcept
    -> ScoreboardMetrics;

/// What the board shows: what heads it, the players, whose its own player is, and the teams' wins.
struct ScoreboardView {
    std::string_view hostname{};
    std::span<const ScoreRow> rows{};
    PlayerIndex self{};
    nx::EnumArray<Team, std::uint32_t> wins{};
};

/// The board: the glyphs it writes, in kEngineFont made for a screen's height, put on the GPU with the
/// screen's pictures.
class Scoreboard {
public:
    /// The board for a screen `screenHeight` pixels tall, in kEngineFont of `fonts`. Errors: those of
    /// WindowFonts::font, and Unusable (its glyphs cannot be drawn).
    [[nodiscard]] static auto open(const WindowFonts& fonts, std::uint32_t screenHeight)
        -> nx::Result<Scoreboard, WindowFontError>;

    /// Puts its glyphs on the GPU with `screen`, its uploads recorded in `commands`. Errors: those of
    /// nx::render::ScreenRenderer::loadImage.
    [[nodiscard]] auto load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, nx::render::ScreenError>;

    /// Appends to `quads` the board of `view` on a screen of `size`, once loaded; nothing before. It is laid
    /// out again only when what it shows changes: the quads of the last call are appended again until then.
    void append(std::vector<nx::render::ScreenQuad>& quads, const ScoreboardView& view,
                nx::rhi::Extent2d size);

private:
    /// What append shows: what it lays out.
    struct Shown {
        std::string hostname{};
        std::vector<ScoreRow> rows{};
        PlayerIndex self{};
        nx::EnumArray<Team, std::uint32_t> wins{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };

    explicit Scoreboard(nx::ui::GlyphAtlas glyphAtlas) noexcept;

    /// Appends to `quads` the board of `view` on a screen of `size`, drawn with `face`.
    void layOut(std::vector<nx::render::ScreenQuad>& quads, const nx::ui::TextFace& face,
                const ScoreboardView& view, nx::rhi::Extent2d size) const;

    nx::ui::GlyphAtlas atlas;
    std::optional<nx::render::ScreenImage> glyphs; ///< The atlas on the GPU, once loaded.
    nx::render::RetainedQuads<Shown> shown;
};

} // namespace cs
