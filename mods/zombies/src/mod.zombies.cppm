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

/// data/mods/zombies.toml: the zombies' body, sight, memory, navigation and spawning budgets.
struct Settings {
    cs::EntityDefinition body{};
    nx::HammerUnits sightRange{};
    nx::Degrees fieldOfView{};
    nx::Seconds memoryLifetime{};
    nx::HammerUnits arrivalDistance{};
    nx::HammerUnits replanDistance{};
    std::uint32_t memoryLimit = 0;
    std::uint32_t perceptionBudget = 0;
    std::uint32_t spawnBatchLimit = 0;
    nx::HammerUnits spawnRadius{};
    nx::HammerUnits spawnSpacing{};
};

/// Parses the mod settings and validates perception/spawn budgets. Errors: InvalidData. The body's
/// stats and model are validated separately by EntityCatalog::add before creating a horde.
[[nodiscard]] auto readSettings(std::string_view text, std::string_view source)
    -> nx::Result<Settings, ZombieError>;

/// The zombies of one map, their bodies of one type driven by brains along the standing body's mesh.
class Horde {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The horde walks `mesh`, the mesh of the game's map for cs::standingPlayerProfile: this adapter
    /// cannot crouch, so it leaves out crouch-only passages. The game, its content and the mesh must
    /// outlive the horde; destroying the horde removes its bodies while their mesh is there.
    /// Requires validated settings and a matching type issued by the game's catalog.
    /// Errors: SpawnFailed (component registration).
    [[nodiscard]] static auto create(cs::Game& game, const nx::navigation::NavigationMesh& mesh,
                                     cs::EntityTypeIndex type, Settings settings)
        -> nx::Result<std::unique_ptr<Horde>, ZombieError>;
    Horde(CreateToken token, cs::EntityTypeIndex type, Settings settings,
          const nx::navigation::NavigationMesh& mesh, cs::Game& game);
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
    /// Samples sight and advances brains once, immediately before the owner's next tick.
    [[nodiscard]] auto commands() -> std::vector<cs::EntityCommand>;
    /// Removes tracked bodies and their corridors. Errors: SpawnFailed, retaining a removal's cause.
    [[nodiscard]] auto clear() -> nx::Result<void, ZombieError>;
    /// How many bodies the horde tracks.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return agents.size(); }

private:
    cs::EntityTypeIndex type;
    Settings settings;
    nx::navigation::NavigationQuery query; ///< Of the mesh the horde borrows.
    std::vector<nx::ecs::Entity> agents;
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
        nx::meta::field<&zombies::Settings::sightRange>("sightRange"),
        nx::meta::field<&zombies::Settings::fieldOfView>("fieldOfView"),
        nx::meta::field<&zombies::Settings::memoryLifetime>("memoryLifetime"),
        nx::meta::field<&zombies::Settings::arrivalDistance>("arrivalDistance"),
        nx::meta::field<&zombies::Settings::replanDistance>("replanDistance"),
        nx::meta::field<&zombies::Settings::memoryLimit>("memoryLimit"),
        nx::meta::field<&zombies::Settings::perceptionBudget>("perceptionBudget"),
        nx::meta::field<&zombies::Settings::spawnBatchLimit>("spawnBatchLimit"),
        nx::meta::field<&zombies::Settings::spawnRadius>("spawnRadius"),
        nx::meta::field<&zombies::Settings::spawnSpacing>("spawnSpacing"),
    };
};
