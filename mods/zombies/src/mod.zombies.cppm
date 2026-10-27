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
import nx.jobs;
import nx.assets;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

export namespace zombies {
enum class ZombieError : std::uint8_t { InvalidData, NavigationUnavailable, SpawnFailed };

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
    nx::HammerUnits spawnDistance{};
    nx::HammerUnits spawnSpacing{};
};

[[nodiscard]] auto readSettings(std::string_view text, std::string_view source)
    -> nx::Result<Settings, ZombieError>;

class Horde {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// Builds the standing-body mesh in jobs. The game and its content must outlive the horde;
    /// destroying the horde removes its bodies before releasing their mesh.
    [[nodiscard]] static auto create(nx::jobs::JobSystem& jobs, cs::Game& game,
                                     const nx::game::NavigationData& navigation, cs::EntityTypeIndex type,
                                     Settings settings) -> nx::Result<std::unique_ptr<Horde>, ZombieError>;
    Horde(CreateToken token, cs::EntityTypeIndex type, Settings settings, nx::navigation::NavigationMesh mesh,
          cs::Game& game);
    ~Horde();
    Horde(const Horde&) = delete;
    auto operator=(const Horde&) -> Horde& = delete;
    Horde(Horde&&) = delete;
    auto operator=(Horde&&) -> Horde& = delete;

    /// All placements succeed or the bodies created by this call are rolled back.
    [[nodiscard]] auto spawn(nx::Vec3 origin, nx::Degrees yaw, std::uint32_t count)
        -> nx::Result<void, ZombieError>;
    [[nodiscard]] auto commands() -> std::vector<cs::EntityCommand>;
    [[nodiscard]] auto clear() -> nx::Result<void, ZombieError>;
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return agents.size(); }

private:
    cs::EntityTypeIndex type;
    Settings settings;
    nx::navigation::NavigationMesh mesh;
    nx::navigation::NavigationQuery query;
    std::vector<nx::ecs::Entity> agents;
    cs::Game* const owner;
    static constexpr auto transitionLogInterval = std::chrono::seconds{1};
    nx::log::RateLimit transitionLogs{transitionLogInterval};
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
        nx::meta::field<&zombies::Settings::spawnDistance>("spawnDistance"),
        nx::meta::field<&zombies::Settings::spawnSpacing>("spawnSpacing"),
    };
};
