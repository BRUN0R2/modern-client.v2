// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the parts of a map's game act on (docs/adr/0030-game-parts.md): what it is played with, its rounds
/// and their money, its world, its players and what the tick made. cs::Game holds it and is all that
/// importers see; each part (cs.server:playerPose, cs.server:playerSpawns and the others) is functions over
/// it.
export module cs.server:gameState;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.game;
import cs.shared;
import :gameCvars;
import :gameModels;

export namespace cs {

/// Where an attack a player took came from, as its HUD's damage indicator shows it (the Damage message's
/// origin): the way toward its attacker across, a unit direction, and the team of the player that made it,
/// none for an entity's body.
struct HurtFrom {
    nx::Vec3 toward{};
    std::optional<Team> team{};
};

} // namespace cs

namespace cs {

/// What the game's parts log under, as the original's server logs the rounds.
inline constexpr nx::log::Category kGameLog{"game"};

/// A player of the game, as its server keeps it (CBasePlayer): its team, its name (pev->netname), its score
/// and _cl_autowepswitch, where it
/// is and how it moves, what it has, its weapons, its feet's and its breath's voices, the harms it keeps
/// over time, its body among the world's entities, the buttons of its last command, whether the next
/// round pays it, how often it spawned in the round, what its HUD was told last of the round's time,
/// and what the tick did to it and made it make, which its client is told of; the character it plays
/// as, where it looks (pev->v_angle), how its model plays, and the seed of the command it runs, which what
/// it does draws from (random_seed); the hit group an attack last met it in (m_LastHitGroup), and the
/// share of its speed a hit slowed it down to (m_flVelocityModifier); and what its hits dealt the bodies
/// they met in the tick, which its client shows. It joins dead, and lives once it spawns.
struct Player {
    Team team;
    std::string name;
    PlayerScore score{};
    AutoSwitch autoSwitch;
    CharacterIndex character;
    MovementState movement{};
    PlayerStatus status;
    Inventory inventory;
    StepVoice stepVoice;
    Breath breath;
    TimedHarms timedHarms{};
    nx::ecs::Entity body{};
    nx::Flags<Button> lastButtons{}; ///< m_afButtonLast: the use button acts as it goes down.
    bool isPaidNextRound = true; ///< Not when the last end leaves it unpaid (m_bReceivesNoMoneyNextRound).
    std::uint32_t spawnsThisRound = 0;  ///< m_iNumSpawns.
    nx::GameTime spawnedAt{};           ///< When it last spawned, alive already or not (Spawn, RoundRespawn).
    std::optional<HurtFrom> hurtFrom{}; ///< In the tick: where the last attack it took came from.
    std::vector<DamageDealt> damageDealt{}; ///< In the tick: what its hits dealt, in their order.
    RoundTimer roundTimer{};
    std::optional<nx::EulerAngles> turnedView{}; ///< In the tick.
    bool wasPlaced = false;                      ///< In the tick.
    std::vector<SoundMade> sounds{};             ///< In the tick, but its weapons', which they keep.
    /// What its purchases sound like: it buys between ticks, as a client's commands come, heard in the next.
    std::vector<SoundMade> soundsOfPurchases{};
    nx::EulerAngles view{};
    PlayerAnimation animation{};
    CommandNumber randomSeed{};
    HitGroup lastHitGroup = HitGroup::Generic;
    float speedShare = 1.0F;
};

/// What a tick made, which every client is told of after it.
struct TickEvents {
    std::vector<Impact> impacts{};
    std::vector<EntityStep> entitySteps{};
    std::vector<BloodMark> bloodMarks{};
    std::vector<BloodSpray> bloodSprays{};
    std::vector<BloodStream> bloodStreams{};
    std::vector<StreakSplash> streakSplashes{};
    std::vector<KillNotice> killNotices{};
};

/// What a map's game is played with, and where it stands: its content and its map, which must outlive it, its
/// cvars, the models it plays, its rounds and their money, the world of its entities, its players by their
/// slots, what the tick made, and its light styles.
struct GameState {
    /// The game of `map` of `content` as it starts: the first round frozen, no player yet.
    GameState(const GameContent& gameContent, const GameMap& gameMap, GameCvars gameCvars,
              GameModels gameModels);

    const GameContent& content;
    const GameMap& map;
    GameCvars cvars;
    GameModels models; ///< What the world's hostages play: they point into it.
    Rounds rounds;
    TeamAccounts accounts;
    nx::EnumArray<Team, std::uint32_t> wins{}; ///< m_iNumTerroristWins, m_iNumCTWins.
    nx::GameTime nextPeriodicThink{};          ///< When the rules look at the restart cvars next.
    /// Of each team's, the one its last player spawned at (the original's g_pLastTerroristSpawn and
    /// g_pLastCTSpawn).
    nx::EnumArray<Team, std::optional<std::size_t>> lastSpawnPoints{};
    nx::ecs::World world;
    std::vector<std::optional<Player>> players{}; ///< By slot: none for a slot a player left.
    TickEvents events{};
    MapLightStyles lightStyles; ///< Owned by this game; copied into each client's snapshot.
};

/// The player `index` names, in the game.
[[nodiscard]] auto playerAt(GameState& game, PlayerIndex index) -> Player&;
[[nodiscard]] auto playerAt(const GameState& game, PlayerIndex index) -> const Player&;

/// Whether a slot holds a player, and the player it holds.
struct IsPlayerSlot {
    [[nodiscard]] inline auto operator()(const std::optional<Player>& slot) const noexcept -> bool {
        return slot.has_value();
    }
};
struct PlayerOfSlot {
    [[nodiscard]] inline auto operator()(std::optional<Player>& slot) const noexcept -> Player& {
        return *slot;
    }
    [[nodiscard]] inline auto operator()(const std::optional<Player>& slot) const noexcept -> const Player& {
        return *slot;
    }
};

/// The players of `slots`, in their slots' order, a slot a player left holding none.
[[nodiscard]] inline auto playersIn(std::vector<std::optional<Player>>& slots) {
    return slots | std::views::filter(IsPlayerSlot{}) | std::views::transform(PlayerOfSlot{});
}
[[nodiscard]] inline auto playersIn(const std::vector<std::optional<Player>>& slots) {
    return slots | std::views::filter(IsPlayerSlot{}) | std::views::transform(PlayerOfSlot{});
}

/// Where `player`'s eye is.
[[nodiscard]] auto eyeOf(const MovementState& player) -> nx::Vec3;
/// The lower center of `player`'s active box, standing or ducked.
[[nodiscard]] auto feetOf(const MovementState& player) -> nx::Vec3;

/// `player` makes `sound` at the tick, at its own volume.
void makeSound(const GameState& game, Player& player, const GameSound& sound);

} // namespace cs
