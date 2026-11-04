// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A native test mod over generic entities and agents, outside Counter-Strike's rules (ADR-0028).
export module mod.zombies;
import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;
import nx.ecs;
import nx.assets;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

export namespace zombies {
/// Why the zombies' settings could not be read, or a horde made, spawned or cleared.
enum class ZombieError : std::uint8_t {
    InvalidData, ///< The settings break their schema or their budgets.
    SpawnFailed, ///< A body could not be placed, registered or removed.
};

/// data/mods/zombies.toml: the zombies' body, sight, how far they hear each kind of noise, memory,
/// navigation, how one finds itself stuck and gets out, and spawning budgets.
struct Settings {
    cs::EntityDefinition body{};
    nx::Seconds thinkInterval{};
    nx::HammerUnits sightRange{};
    nx::Degrees fieldOfView{};
    nx::Seconds memoryLifetime{};
    nx::HammerUnits arrivalDistance{};
    nx::HammerUnits replanDistance{};
    float replanShare = 0.0F;
    float braking = 0.0F;
    nx::UnitsPerSecond stopSpeed{};
    float acceleration = 0.0F;
    nx::Seconds shortTermMemory{};
    nx::Seconds threatRecency{};
    nx::HammerUnits threatMargin{};
    nx::agents::StuckRules stuck{};
    cs::HearingRules hearing{};
    std::uint32_t memoryLimit = 0;
    std::uint32_t perceptionBudget = 0;
    std::uint32_t spawnBatchLimit = 0;
    nx::HammerUnits spawnRadius{};
    nx::HammerUnits spawnSpacing{};
};

/// Parses the mod settings and validates perception/spawn budgets, the stuck rules (nx::agents::isValid)
/// and the hearing (cs::isValid).
/// Errors: InvalidData. The body's
/// stats and model are validated separately by EntityCatalog::add before creating a horde.
[[nodiscard]] auto readSettings(std::string_view text, std::string_view source)
    -> nx::Result<Settings, ZombieError>;
/// `base` with what `text`, the file `source` of a user, says over it, validated as readSettings does: it
/// names only what it changes, so that what the project's file gains later reaches it
/// (nx::data::parseTomlOver). Errors: InvalidData, a key that is no setting among them.
[[nodiscard]] auto readSettingsOver(const Settings& base, std::string_view text, std::string_view source)
    -> nx::Result<Settings, ZombieError>;

/// The zombies of one map, their bodies of one type driven by brains along the standing body's mesh.
class Horde {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The horde walks `mesh`, the mesh of the game's map for cs::standingPlayerProfile: this adapter
    /// cannot crouch, so it leaves out crouch-only passages. Its brains decide together in jobs of
    /// `jobs`. The game, its content, the mesh and the jobs must outlive the horde; destroying the horde
    /// removes its bodies while their mesh is there.
    /// Requires validated settings and a matching type issued by the game's catalog.
    /// Errors: SpawnFailed (component registration).
    [[nodiscard]] static auto create(cs::Game& game, const nx::navigation::NavigationMesh& mesh,
                                     nx::jobs::JobSystem& jobs, cs::EntityTypeIndex type, Settings settings)
        -> nx::Result<std::unique_ptr<Horde>, ZombieError>;
    Horde(CreateToken token, cs::EntityTypeIndex type, Settings settings,
          const nx::navigation::NavigationMesh& mesh, nx::jobs::JobSystem& jobs, cs::Game& game);
    ~Horde();
    Horde(const Horde&) = delete;
    auto operator=(const Horde&) -> Horde& = delete;
    Horde(Horde&&) = delete;
    auto operator=(Horde&&) -> Horde& = delete;

    /// Searches the base's spawn points and unique polygon corners/centers, nearest the base first.
    /// Bodies stay within spawnRadius of a projected base point, reachable from its nearest point,
    /// and keep spawnSpacing from live entity bodies horizontally. They face the nearest point's yaw.
    /// All placements succeed or this call is rolled back. Errors: SpawnFailed for an invalid count,
    /// an invalid query or insufficient clear reachable positions. Occupied candidates are excluded.
    [[nodiscard]] auto spawn(std::span<const cs::SpawnPoint> base, std::uint32_t count)
        -> nx::Result<void, ZombieError>;
    /// Tracks `bodies`, made already and alive, as the bodies spawn makes are.
    void adopt(std::span<const nx::ecs::Entity> bodies);
    /// What the horde asks of its bodies, immediately before the owner's next tick, valid until it is asked
    /// again. The bodies whose turn it is, one Settings::thinkInterval after their last, or a few away from
    /// every player, perceive, then their brains decide together, in batches in jobs (nx::agents::Deciders);
    /// what each decided holds until its next turn, the way it is driven worked out from how it moves now, so
    /// that it does not drift off its way meanwhile.
    [[nodiscard]] auto commands() -> std::span<const cs::EntityCommand>;
    /// Removes tracked bodies and their corridors. Errors: SpawnFailed, retaining a removal's cause.
    [[nodiscard]] auto clear() -> nx::Result<void, ZombieError>;
    /// How many bodies the horde tracks.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return members.size(); }

private:
    /// A body the horde tracks, and what it decided at its last turn: its view and buttons; and its walk, the
    /// velocity it steered to if it did, the way it goes, the axes of its view, across, and its speed.
    struct Member {
        nx::ecs::Entity entity{};
        cs::UserCommand command{};
        std::uint32_t turnsWaited = 0; ///< Since it last thought: more of them away from every player.
        bool walks = false;
        std::optional<nx::Vec3> steered{};
        nx::Vec3 way{};
        nx::Vec3 forward{};
        nx::Vec3 right{};
        nx::UnitsPerSecond speed{};
    };

    /// A body whose turn it is, its agent and its eye, between what it perceives and what it decides.
    struct Turn {
        Member* member = nullptr;
        const cs::EntityState* body = nullptr;
        nx::agents::Agent* agent = nullptr;
        nx::Vec3 eye{};
    };

    /// Writes to `turns` and `requests` those of `bodies` whose turn it is at `tick`, alive, in jobs: the
    /// farther from `targets`, at fewer of their turns.
    void findTurns(const cs::EntityStates& bodies, std::span<const cs::PerceptionTarget> targets,
                   std::uint64_t tick, nx::Seconds elapsed);
    /// `turns` perceive, in jobs.
    void perceive(std::span<const cs::PerceptionTarget> targets,
                  std::span<const nx::game::SolidBrushEntity> solids);
    /// What the brains of `turns` decided becomes what their bodies hold to, in jobs.
    void holdDecisions();
    /// Writes to `steeredAround` the bodies the horde steers around, players' and those alive of `bodies`, in
    /// jobs.
    void writeSteeredAround(const cs::EntityStates& bodies);
    /// Writes to `asked` what the members ask this tick, at their velocities in `bodies`, in jobs.
    void writeAsked(const cs::EntityStates& bodies);

    cs::EntityTypeIndex type;
    Settings settings;
    nx::jobs::JobSystem* jobs;
    nx::navigation::NavigationQuery query; ///< Of the mesh the horde borrows.
    std::vector<nx::navigation::NavigationQuery>
        queries;                   ///< One for each batch that perceives, as they need them.
    nx::agents::Deciders deciders; ///< Of the mesh the horde borrows.
    std::vector<Member> members;   ///< By entity.
    std::vector<std::vector<std::uint32_t>> membersOfTurn; ///< In `members`, by the tick of each one's turn.
    std::vector<cs::EntityCommand> asked;        ///< Of the members, what commands gives; kept for its room.
    std::vector<nx::agents::Body> steeredAround; ///< Kept for its room.
    std::vector<nx::agents::Agent*> agentOfSlot; ///< By the index of an entity; kept for its room.
    std::vector<Turn> turns;                     ///< Those of a tick; kept for its room.
    std::vector<nx::agents::DecisionRequest> requests; ///< Of `turns`; kept for its room.
    std::vector<nx::agents::Decision> decisions;       ///< Of `turns`; kept for its room.
    std::vector<cs::GameNoise> noisesHeard; ///< The noises of the last think interval, which each hears once.
    std::vector<std::uint32_t> noisesOfTick; ///< How many of them each of its ticks made, the oldest first.
    cs::Game* const owner;
    static constexpr auto kTransitionLogInterval = std::chrono::seconds{1};
    nx::log::RateLimit transitionLogs{kTransitionLogInterval};
};
} // namespace zombies

template <>
struct nx::meta::Description<zombies::Settings> {
    static constexpr std::string_view kName = "zombies::Settings";
    static constexpr std::tuple kFields{
        nx::meta::field<&zombies::Settings::body>("body"),
        nx::meta::field<&zombies::Settings::thinkInterval>("thinkInterval"),
        nx::meta::field<&zombies::Settings::sightRange>("sightRange"),
        nx::meta::field<&zombies::Settings::fieldOfView>("fieldOfView"),
        nx::meta::field<&zombies::Settings::memoryLifetime>("memoryLifetime"),
        nx::meta::field<&zombies::Settings::arrivalDistance>("arrivalDistance"),
        nx::meta::field<&zombies::Settings::replanDistance>("replanDistance"),
        nx::meta::field<&zombies::Settings::replanShare>("replanShare", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::braking>("braking", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::stopSpeed>("stopSpeed", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::acceleration>("acceleration", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::shortTermMemory>("shortTermMemory",
                                                             nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::threatRecency>("threatRecency", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::threatMargin>("threatMargin", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&zombies::Settings::stuck>("stuck"),
        nx::meta::field<&zombies::Settings::hearing>("hearing"),
        nx::meta::field<&zombies::Settings::memoryLimit>("memoryLimit"),
        nx::meta::field<&zombies::Settings::perceptionBudget>("perceptionBudget"),
        nx::meta::field<&zombies::Settings::spawnBatchLimit>("spawnBatchLimit"),
        nx::meta::field<&zombies::Settings::spawnRadius>("spawnRadius"),
        nx::meta::field<&zombies::Settings::spawnSpacing>("spawnSpacing"),
    };
};
