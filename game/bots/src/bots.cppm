// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The bots of the server's game (docs/10-counter-strike-gameplay.md, section 11, ADR-0024): players
/// that join it as a client does and play by the commands their brains make, with no shortcut of the
/// game's; what they know of the others is what they see. As ReGameDLL_CS's reconstruction of Condition
/// Zero's bot joins, keeps its number, sees and fires (CCSBotManager::BotAddCommand, MaintainBotQuota,
/// UTIL_KickBotFromTeam, CCSBot::IsVisible, TogglePrimaryAttack; GPL, consulted: docs/20 section 3).
///
/// Today: they hunt places of the mesh drawn at random, chase and fire at the enemies they see, aiming
/// at the first spot of them they see, and look for where they lost them; each plays with a level of
/// skill of bot_difficulty, which tells how soon it reacts, how well it aims and how it fires; their
/// names, their buying, the objectives and the radio come next (TODO.md).
export module cs.bots:bots;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;
import :botRules;
import :botAim;
import :botTrigger;

export namespace cs {

/// What drives a bot's player besides its brain, a component of its body: the number of its last
/// command, its level of skill (of BotRules::skillLevels), whether it was alive at its last command, how
/// its view turns, how its aim errs, its trigger, and the streams its hunt and its aim draw from.
struct BotDriver {
    CommandNumber lastCommand{};
    std::size_t skillLevel = 0;
    bool wasAlive = false;
    BotViewTurn turn{};
    BotAimError aimError{};
    BotTrigger trigger{};
    nx::Random hunt;
    nx::Random aim;
};

/// The bots of one game, which must outlive them: each a player of it whose body is an agent
/// (nx::agents::Agent, ADR-0024) with its driver (BotDriver), on a mesh of a standing player's profile,
/// built once.
class Bots {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The bots of `game`, none yet, on the mesh of `navigation`'s player profile built in `jobs` of the
    /// game's map, a standing player's: it does not duck through what is only passed ducked; the
    /// bodies' components registered in the game. Errors: NavigationUnavailable (no player profile, or
    /// one that does not match a standing player, or the mesh's build failed: its cause), Unregistered
    /// (a component: its cause).
    [[nodiscard]] static auto create(nx::jobs::JobSystem& jobs, Game& game,
                                     const nx::game::NavigationData& navigation, BotRules rules)
        -> nx::Result<std::unique_ptr<Bots>, BotError>;

    /// Use create(). Public only so that create() can use std::make_unique.
    Bots(CreateToken token, Game& game, BotRules rules, nx::navigation::NavigationMesh mesh);
    /// Each bot leaves the game, its body and its brain's corridor with it, before the mesh the corridor
    /// borrows goes.
    ~Bots();
    Bots(const Bots&) = delete;
    auto operator=(const Bots&) -> Bots& = delete;
    Bots(Bots&&) = delete;
    auto operator=(Bots&&) -> Bots& = delete;

    /// A bot joins the game in `team`, or in the game's default team for none (BotAddCommand with
    /// bot_join_team "any": SelectDefaultTeam), drawing the heavier weapon it is given, with a level of
    /// skill of `difficulty` drawn at random (GetRandomProfile, among the levels as the named profiles
    /// come with the bots' names). Returns its player.
    auto add(std::optional<Team> team, BotDifficulty difficulty) -> PlayerIndex;
    /// Every bot leaves the game (bot_kick).
    void kickAll();
    /// Adds a bot of `difficulty` when there are fewer than `quota`, or makes one leave when there are
    /// more, once each BotRules::quotaInterval of the game (MaintainBotQuota): of the team of more
    /// players, else of one drawn at random, a dead one first (UTIL_KickBotFromTeam); of the other team
    /// when that one has none. The original then weighs the teams' wins, which come with the scores
    /// (TODO.md).
    void keepQuota(std::uint32_t quota, BotDifficulty difficulty);
    /// The commands of the bots for the game's next tick, one for each, in the order they joined: each
    /// alive looks at the enemies its budget lets it look at, in turn, and remembers those it sees, each
    /// known to it its level's reaction time after it first saw it; thinks (nx::agents::Brain::decide);
    /// turns its view by its spring (turnView) towards where it aims at what it attacks, ahead of it
    /// above BotAimRules::leadingSkill (leadSpot) and off by its aim's error (settleAimError), or at what
    /// it looks at, or along its way; moves along its corridor at its full speed; and presses its attack
    /// as its trigger lets it (pullTrigger), its level's attack delay after it took its enemy on. With
    /// nothing to pursue, it hunts a place drawn on the mesh within BotRules::huntRadius. Spawning again,
    /// it starts with a new brain, its view still, its aim and its trigger anew.
    [[nodiscard]] auto commands() -> std::vector<PlayerCommand>;

    /// How many bots are in the game.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return bots.size(); }
    /// The players the bots play, in the order they joined.
    [[nodiscard]] inline auto players() const noexcept -> std::span<const PlayerIndex> { return bots; }
    /// The level of skill the bot `player` plays with.
    [[nodiscard]] auto skillLevelOf(PlayerIndex player) -> const BotSkillLevel&;

private:
    /// The command of the bot `player` for the next tick, knowing `targets`.
    [[nodiscard]] auto commandOf(PlayerIndex player, std::span<const PerceptionTarget> targets)
        -> UserCommand;
    /// What the bot `player`, its agent `agent`, alive as `self`, sees of `targets` this tick, within its
    /// budget.
    [[nodiscard]] auto perceive(PlayerIndex player, nx::agents::Agent& agent, const PlayerOwnState& self,
                                std::span<const PerceptionTarget> targets)
        -> std::vector<nx::agents::Observation>;
    /// Gives `agent`, with `driver`, a place of the mesh within BotRules::huntRadius of `feet` to hunt when
    /// `decision` leaves it nothing to do, another when its way to the last is blocked.
    void hunt(nx::agents::Agent& agent, BotDriver& driver, const nx::agents::Decision& decision,
              nx::Vec3 feet);
    /// Where a bot with `driver`, alive as `self`, aims at the enemy `decision` attacks, among `targets`,
    /// as its command of `duration` moves its aim's error.
    [[nodiscard]] auto aimSpotOf(BotDriver& driver, const PlayerOwnState& self,
                                 const nx::agents::Decision& decision,
                                 std::span<const PerceptionTarget> targets, nx::Seconds duration) -> nx::Vec3;
    /// Makes a bot of `team` leave, a dead one first; returns whether one did.
    auto kickFrom(Team team) -> bool;
    /// Makes the bot `bot` leave the game.
    void kick(std::vector<PlayerIndex>::const_iterator bot);

    Game* game;
    BotRules rules;
    nx::navigation::NavigationMesh mesh;
    nx::navigation::NavigationQuery query; ///< Of mesh.
    std::vector<PlayerIndex> bots;         ///< Their bodies' corridors borrow mesh: they leave first.
    nx::GameTime nextQuotaCheck{};
};

} // namespace cs
