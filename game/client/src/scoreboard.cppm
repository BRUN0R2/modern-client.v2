// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The scoreboard (docs/12-ui-hud-and-console.md, section 3.2), shown while +showscores is held: a look of
/// the project's own (ADR-0020), in the menus' fonts (cs.client:menuDesign). A header with what heads it, the
/// map and how many play; under it the counter-terrorists on the left and the terrorists on the right, each
/// with its wins and how many of it live, and a table of its players: each by its name, its health and its
/// money (its teammates' alone, cs::ScoreRow), whether it is dead, its kills and its deaths. Each team's
/// banner shows its emblem, the project's own pictures (kScoreboardEmblems). No avatar picture and no
/// latency yet.
export module cs.client:scoreboard;

import std;
import nx.core;
import nx.console;
import nx.rhi;
import nx.render;
import nx.assets;
import nx.ui;
import cs.shared;
import :menuDesign;

export namespace cs {

/// The name the board is headed by until a server is given one: `hostname`'s default.
inline constexpr std::string_view kDefaultHostname = "Counter-Strike 1.6";

/// The emblem of each team, tinted as drawn (white shows the picture's own colors): TGA files at these paths
/// under the player's folder, where a player puts pictures of their own, else under the project's data/
/// folder, which has the project's.
inline constexpr nx::EnumArray<Team, std::string_view> kScoreboardEmblems{
    nx::EnumArray<Team, std::string_view>::Storage{"cstrike/scoreboardTerrorists.tga",
                                                   "cstrike/scoreboardCounterTerrorists.tga"}};

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

    /// How many of its players live.
    [[nodiscard]] auto aliveCount() const noexcept -> std::size_t;

    [[nodiscard]] inline auto operator==(const ScoreTeam&) const noexcept -> bool = default;
};

/// The teams of `rows` as the board lists them, side by side: the counter-terrorists on the left, the
/// terrorists on the right, either of no player too; each team's players from the most frags, of as many the
/// fewest deaths, of as many by their slots.
[[nodiscard]] auto scoreTeamsOf(std::span<const ScoreRow> rows,
                                const nx::EnumArray<Team, std::uint32_t>& wins) -> std::vector<ScoreTeam>;

/// `text` cut so that it is at most `wide` in `atlas`, "..." ending it when cut; whole UTF-8 characters.
[[nodiscard]] auto elidedTo(const nx::ui::GlyphAtlas& atlas, std::string_view text, nx::Pixels wide)
    -> std::string;

/// What the board shows: what heads it, the map, the players, whose its own player is, the teams' wins,
/// and its texts in the player's language.
struct ScoreboardView {
    std::string_view hostname{};
    std::string_view map{};
    std::span<const ScoreRow> rows{};
    PlayerIndex self{};
    nx::EnumArray<Team, std::uint32_t> wins{};
    const ScoreboardTexts* texts = nullptr;
};

/// The sizes of text the board writes in.
enum class ScoreboardFont : std::uint8_t {
    Title,    ///< What heads it.
    Subtitle, ///< The map, and how many play.
    Team,     ///< A team's name.
    Wins,     ///< A team's wins, large.
    Head,     ///< The columns' heads, and a player's initial.
    Row,      ///< A player's row, and how many of a team live.
    Tag,      ///< "Dead".
    Count,
};

/// The board: its fonts, made for a screen's height of the menus' families, put on the GPU with the
/// screen's pictures.
class Scoreboard {
public:
    /// The board for a screen `screenHeight` pixels tall, in `families` among `fonts`, each team's emblem
    /// read from the first of `folders` that has it (kScoreboardEmblems): one none has, or that cannot be
    /// read, is logged, and its team's banner shows none. Errors: those of makeMenuAtlas.
    [[nodiscard]] static auto open(nx::ui::FontCatalog& fonts, const MenuFontFamilies& families,
                                   std::uint32_t screenHeight, std::span<const std::filesystem::path> folders)
        -> nx::Result<Scoreboard, MenuPanelError>;

    /// Puts its glyphs on the GPU with `screen`, its uploads recorded in `commands`. Errors: those of
    /// nx::render::ScreenRenderer::loadImage.
    [[nodiscard]] auto load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, nx::render::ScreenError>;

    /// Appends to `quads` the board of `view` on a screen of `size`, once loaded; nothing before, nor
    /// without texts. It is laid out again only when what it shows changes: the quads of the last call are
    /// appended again until then.
    void append(std::vector<nx::render::ScreenQuad>& quads, const ScoreboardView& view,
                nx::rhi::Extent2d size);

private:
    /// What append shows: what it lays out.
    struct Shown {
        std::string hostname{};
        std::string map{};
        std::vector<ScoreRow> rows{};
        PlayerIndex self{};
        nx::EnumArray<Team, std::uint32_t> wins{};
        ScoreboardTexts texts{};
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };
    using Atlases = nx::EnumArray<ScoreboardFont, std::optional<nx::ui::GlyphAtlas>>;
    using Faces = nx::EnumArray<ScoreboardFont, nx::ui::TextFace>;

    using Emblems = nx::EnumArray<Team, std::optional<nx::assets::RgbaImage>>;

    Scoreboard(Atlases made, Emblems read) noexcept;

    /// Appends to `quads` the board of `view` on a screen of `size`, written with `faces`.
    void layOut(std::vector<nx::render::ScreenQuad>& quads, const Faces& faces, const ScoreboardView& view,
                nx::rhi::Extent2d size) const;

    Atlases atlases;
    nx::EnumArray<ScoreboardFont, std::optional<nx::render::ScreenImage>> images; ///< Once loaded.
    Emblems emblems;                                                          ///< None unread.
    nx::EnumArray<Team, std::optional<nx::render::ScreenImage>> emblemImages; ///< Once loaded.
    nx::render::RetainedQuads<Shown> shown;
};

} // namespace cs
