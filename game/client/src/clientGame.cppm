// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game as its player's client knows it (docs/07-simulation-and-physics.md, section 2.2;
/// docs/08-networking.md): the user commands it makes, one a tick, and what its server tells it after
/// each (PlayerSnapshot), which a frame shows between the last two ticks, by the time left over; the
/// sounds, the marks and the turns of the view the ticks brought, kept until the frame takes them; and
/// the other players, whose legs and aim it turns itself each frame, as the original's client does.
export module cs.client:clientGame;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;
import nx.render;
import cs.shared;
import :input;
import :mapModels;
import :playerModels;
import :viewModel;
import :corpses;

export namespace cs {

/// When a frame plays what the game made: what came after `from`, the moment of the game the frame
/// before showed, is heard as long after the frame as it came after that moment. A frame that ran
/// many ticks plays their sounds spread as they were made, not all at once: a weapon's cadence is
/// heard the same at any frame rate, a frame later at most.
struct Hearing {
    nx::GameTime from{};
    nx::TickRate rate{nx::kDefaultTickRate};

    /// How long after the frame what was made at `made` is heard: at once when not after `from`.
    [[nodiscard]] inline auto delayOf(nx::GameTime made) const noexcept -> nx::Seconds {
        return std::max(made.since(from, rate), nx::Seconds{});
    }
};

/// How much of the time a frame gives the game it plays: a quarter of a second at most, so that a
/// stalled frame does not run away, as a played frame does; or all of it, as a tool's still frame does.
enum class CatchUp : std::uint8_t {
    Limited,
    Whole,
};

/// The game as its player's client knows it.
class ClientGame {
public:
    /// The game whose map's studio models are `models` and whose players' are `players`, which must both
    /// outlive it, as its server told of it first, `first`.
    ClientGame(const MapModels& models, const PlayerModels& players, PlayerSnapshot first);

    /// Gives `elapsed` to the game, as `catchUp` says: how many ticks are due now, a command each
    /// (docs/07, section 2.2), the classic 100 a second.
    [[nodiscard]] auto ticksDue(nx::Seconds elapsed, CatchUp catchUp) -> std::uint32_t;
    /// The command of the next tick: `input`'s, looking along `view`, numbered after the last, at the
    /// speed the player could run at last.
    [[nodiscard]] auto nextCommand(PlayerInput& input, nx::EulerAngles view) -> UserCommand;
    /// The command of the next tick asking nothing, looking along `view`, numbered after the last: the
    /// player stands, as a tool's frame shows it.
    [[nodiscard]] auto standingCommand(nx::EulerAngles view) -> UserCommand;
    /// What the server told after the tick of the last command: what shows now; its sounds and the other
    /// players', its marks and turn of the view kept with those not yet taken.
    void receive(PlayerSnapshot snapshot);

    /// The player's eye, between where the last two ticks left it by the time left over; where the game
    /// placed it, at once.
    [[nodiscard]] auto eye() const noexcept -> nx::Vec3;
    /// Horizontal speed on the ground, for the local weapon's walking motion.
    [[nodiscard]] auto walkingSpeed() const noexcept -> nx::UnitsPerSecond;
    /// What the view model shows when the eye is drawn: none while the player holds no weapon.
    [[nodiscard]] auto viewModel() const -> std::optional<ViewModelPose>;
    /// How the next frame hears what the game makes until it (Hearing): from the moment of the game a
    /// frame shows now.
    [[nodiscard]] auto hearing() const -> Hearing;
    /// The kick the player's weapon gave its view (punchangle), which the view is drawn turned by.
    [[nodiscard]] inline auto punch() const noexcept -> nx::EulerAngles { return m_last.player.punch; }
    /// The player's team.
    [[nodiscard]] inline auto team() const noexcept -> Team { return m_last.team; }
    /// What the player has.
    [[nodiscard]] inline auto status() const noexcept -> const PlayerStatus& { return m_last.status; }
    /// Whether the player may buy now, and for how long: the time left at the moment of the game a frame
    /// shows, on the same clock as all it shows, however far apart the server tells it.
    [[nodiscard]] auto buying() const -> BuyingState;
    /// What the player had after its last purchase, which it can buy again.
    [[nodiscard]] inline auto lastPurchase() const noexcept -> const LastPurchase& {
        return m_last.lastPurchase;
    }
    /// The player's weapons.
    [[nodiscard]] inline auto inventory() const noexcept -> const Inventory& { return m_last.inventory; }
    /// The whole seconds the player's HUD shows of the round's time (RoundTimer::shownAt), at the
    /// moment of the game a frame shows.
    [[nodiscard]] auto roundTimeShown() const -> WholeSeconds;
    /// Replaces `placements` with where the game puts its brush entities now, how high their waves rise,
    /// and which textures they show. Keeps the destination's storage for the next frame.
    void writePlacements(std::vector<nx::render::ModelPlacement>& placements) const;
    /// How the game's hostages show at the moment a frame shows (cs::shownHostageOf), those whose model
    /// the client read, replacing `hostages` and keeping its storage for the next frame.
    void writeHostages(std::vector<ShownHostage>& hostages) const;
    /// How the corpses of the game's entity bodies show at the moment a frame shows, each between where the
    /// last two ticks left it, faded out after `corpseStay` and gone (corpseAmountOf), replacing `entities`
    /// and keeping its storage for the next frame. The living the GPU shows (crowd).
    void writeCorpses(const EntityCatalog& catalog, nx::Seconds corpseStay, nx::jobs::JobSystem* jobs,
                      std::vector<ShownEntity>& entities);
    /// The game's entity bodies at the moment a frame shows, valid until the next tick is received.
    [[nodiscard]] auto crowd() const -> ShownCrowd;

    /// The brightness of the server's light styles at `time` since the map started, including every
    /// unset style. No pattern is inferred from local content.
    [[nodiscard]] auto lightStylesAt(nx::Seconds time) const noexcept -> nx::render::LightStyleValues;

    /// How the other players show in a frame of `frameTime`, at the moment it shows: each between where
    /// the last two ticks left it; its legs and its aim turned by the frame (processGait), from its legs
    /// facing its body as it stands when it is first seen; its torso's frame where the client estimates it
    /// from what the server told; those whose model the client read, a dead one's corpse faded out after
    /// `corpseStay` and gone (corpseAmountOf). Replaces `players`, keeping its storage for the next frame.
    /// Previous origins are indexed once per received snapshot.
    void updatePlayers(nx::Seconds frameTime, nx::Seconds corpseStay, std::vector<ShownPlayer>& players);
    /// Where each other player shows at the moment a frame shows, by PlayerIndex, between where the last
    /// two ticks left it, as updatePlayers places it; none for one the server did not tell of last.
    /// Replaces `origins`, keeping its storage for the next frame.
    void writePlayerOrigins(std::vector<std::optional<nx::Vec3>>& origins) const;

    /// The way the game last turned the player's view since the last call, as the original's fixangle
    /// turns it: the way the spawn point it last spawned at faces; none.
    [[nodiscard]] inline auto takeTurnedView() -> std::optional<nx::EulerAngles> {
        return std::exchange(m_turnedView, std::nullopt);
    }
    /// The sounds the player made since the last call, in the order of their ticks, each with its
    /// moment. What a call gave is read until the next one.
    [[nodiscard]] inline auto takeSounds() -> std::span<const SoundMade> { return m_sounds.handToConsumer(); }
    /// The sounds the other players made since the last call, in the order of their ticks, each with its
    /// moment and where it was made. What a call gave is read until the next one.
    [[nodiscard]] inline auto takePlayerSounds() -> std::span<const PlayerSound> {
        return playerSounds.handToConsumer();
    }
    /// The shot paths received since the last call, in the order they were fired. What a call gave is
    /// read until the next one.
    [[nodiscard]] inline auto takeBulletTracers() -> std::span<const BulletTracer> {
        return bulletTracers.handToConsumer();
    }
    /// Where the shots met the world and the brush entities since the last call, in their order. What a
    /// call gave is read until the next one.
    [[nodiscard]] inline auto takeImpacts() -> std::span<const Impact> { return m_impacts.handToConsumer(); }
    /// Where the blood the hits shed met the world and the brush entities since the last call, in their
    /// order: where it leaves its decal. What a call gave is read until the next one.
    [[nodiscard]] inline auto takeBloodMarks() -> std::span<const BloodMark> {
        return bloodMarks.handToConsumer();
    }
    /// The blood the hits sprayed since the last call, in their order. What a call gave is read until
    /// the next one.
    [[nodiscard]] inline auto takeBloodSprays() -> std::span<const BloodSpray> {
        return bloodSprays.handToConsumer();
    }
    /// The streams of blood the hits in the head made since the last call, in their order. What a call
    /// gave is read until the next one.
    [[nodiscard]] inline auto takeBloodStreams() -> std::span<const BloodStream> {
        return bloodStreams.handToConsumer();
    }
    /// The splashes of streaks since the last call, in their order: the helmets' sparks. What a call gave
    /// is read until the next one.
    [[nodiscard]] inline auto takeStreakSplashes() -> std::span<const StreakSplash> {
        return streakSplashes.handToConsumer();
    }
    /// What the player's hits dealt since the last call, in their order: the numbers its client shows.
    /// What a call gave is read until the next one.
    [[nodiscard]] inline auto takeDamageDealt() -> std::span<const DamageDealt> {
        return m_damageDealt.handToConsumer();
    }
    /// The steps the entities' bodies took since the last call, in their order: the marks its client
    /// shows. What a call gave is read until the next one.
    [[nodiscard]] inline auto takeEntitySteps() -> std::span<const EntityStep> {
        return m_entitySteps.handToConsumer();
    }
    /// Whose game it is.
    [[nodiscard]] inline auto self() const noexcept -> PlayerIndex { return m_last.self; }
    /// Who plays, as the server told last, for the scoreboard.
    [[nodiscard]] inline auto scores() const noexcept -> std::span<const ScoreRow> { return m_last.scores; }
    /// The rounds each team won, as the server told last.
    [[nodiscard]] inline auto teamWins() const noexcept -> const nx::EnumArray<Team, std::uint32_t>& {
        return m_last.teamWins;
    }
    /// The players' deaths the server told of since the last call, in their order: the kill feed's. What a
    /// call gave is read until the next one. Defined in the implementation unit: a KillNotice is not
    /// trivially destructible, and Clang 23 crashes on the importers that destroy one without naming it
    /// once an inline function instantiates their vector here.
    [[nodiscard]] auto takeKillNotices() -> std::span<const KillNotice>;

private:
    /// The moment of the game a frame shows: a tick behind the game, by the time pending, as the eye is
    /// drawn.
    [[nodiscard]] auto shownTime() const -> nx::GameTime;
    /// The command of the next tick, `command` numbered after the last.
    [[nodiscard]] auto numbered(UserCommand command) -> UserCommand;
    /// Where `state`'s player shows at the moment a frame shows: between where the last two ticks left it,
    /// where the last did when the one before had none of it.
    [[nodiscard]] auto originShown(const PlayerEntityState& state) const -> nx::Vec3;

    const MapModels* m_models;
    const PlayerModels* m_players;
    nx::render::LightStyles lightStyles; ///< The last snapshot's patterns, owned here.
    PlayerSnapshot m_last;               ///< What the server told last.
    /// Previous snapshot's origins by PlayerIndex; none where that snapshot had no player.
    std::vector<std::optional<nx::Vec3>> previousPlayerOrigins;
    EntityStates previousEntities{}; ///< The tick before the last, by entity as the last's are.
    std::vector<std::uint32_t>
        deadEntities{}; ///< Of the last tick's, by their place, once writeCorpses found them.
    bool areDeadFound = false;
    std::vector<std::optional<Gait>> m_gaits;      ///< By PlayerIndex: none for one not seen yet.
    CorpseClock<std::uint32_t> playerCorpses;      ///< When the dead players died, by PlayerIndex.
    CorpseClock<nx::ecs::Entity> entityCorpses;    ///< When the dead entity bodies died.
    nx::Vec3 m_previousEye{};                      ///< Before the last tick.
    nx::Seconds m_pending{};                       ///< Of the time given to the game, what no tick took.
    CommandNumber m_lastCommand{};                 ///< The number of the last command the player made.
    std::optional<nx::EulerAngles> m_turnedView{}; ///< Since takeTurnedView.
    /// What the ticks made since the last frame took it, in a buffer of the take's own, so that what a
    /// frame reads is handed over rather than copied out: each keeps its storage from take to take.
    nx::BufferHandover<SoundMade> m_sounds;          ///< Since takeSounds.
    nx::BufferHandover<PlayerSound> playerSounds;    ///< Since takePlayerSounds.
    nx::BufferHandover<BulletTracer> bulletTracers;  ///< Since takeBulletTracers.
    nx::BufferHandover<Impact> m_impacts;            ///< Since takeImpacts.
    nx::BufferHandover<BloodMark> bloodMarks;        ///< Since takeBloodMarks.
    nx::BufferHandover<BloodSpray> bloodSprays;      ///< Since takeBloodSprays.
    nx::BufferHandover<BloodStream> bloodStreams;    ///< Since takeBloodStreams.
    nx::BufferHandover<StreakSplash> streakSplashes; ///< Since takeStreakSplashes.
    nx::BufferHandover<DamageDealt> m_damageDealt;   ///< Since takeDamageDealt.
    nx::BufferHandover<EntityStep> m_entitySteps;    ///< Since takeEntitySteps.
    nx::BufferHandover<KillNotice> killNotices;      ///< Since takeKillNotices.
};

} // namespace cs
