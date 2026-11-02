// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A map as the game plays it (docs/06-assets-and-resources.md, section 5): its BSP, read once, the hulls
/// its traces go through, the spawn points of each team, what its rounds are played for, its hostages,
/// where and whether its players buy, and what its textures are made of; shared by the server and the client
/// of one process, which only read it.
export module cs.shared:gameMap;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.collision;
import nx.ecs;
import nx.game;
import :player;
import :rounds;
import :materials;

export namespace cs {

/// Why a map could not be played.
enum class GameMapError : std::uint8_t {
    Unreadable,   ///< maps/<name>.bsp is missing or invalid (cause).
    HullsInvalid, ///< The map's hulls could not be built (cause).
    Unspawnable,  ///< The map's entities could not be made (cause).
};

/// A place a player of a team spawns at, where it stands and the way it faces.
struct SpawnPoint {
    nx::Vec3 origin{};
    nx::Degrees yaw{};

    [[nodiscard]] inline auto operator==(const SpawnPoint&) const noexcept -> bool = default;
};

/// A spawn point and the team that spawns at it.
struct TeamSpawn {
    SpawnPoint point{};
    Team team = Team::CounterTerrorists;
};

/// The spawn points of `team` on `map`, in the order of its entities: its class's
/// (`info_player_deathmatch` for the terrorists, `info_player_start` for the counter-terrorists)
/// with an origin other than the world's, as SelectSpawnSpot takes them.
[[nodiscard]] auto spawnPointsOf(const nx::assets::BspView& map, Team team) -> std::vector<SpawnPoint>;

/// The first spawn point of `map` and its team: a counter-terrorist one, else a terrorist one; none.
[[nodiscard]] auto firstSpawnOf(const nx::assets::BspView& map) -> std::optional<TeamSpawn>;

/// The team of the spawn point a player first stands at on `map` (firstSpawnOf): the
/// counter-terrorists' without either.
[[nodiscard]] auto spawnTeam(const nx::assets::BspView& map) -> Team;

/// What `map` is played for: the objectives the classes of its entities give it (objectiveOfClass).
[[nodiscard]] auto mapObjectivesOf(const nx::assets::BspView& map) -> MapObjectives;

/// A buy zone of a map (func_buyzone): its brush model, where it is, and the team that buys in it, none for
/// both (CBuyZone::Spawn, BuyTouch).
struct BuyZone {
    nx::assets::ModelIndex model{};
    nx::Vec3 origin{};
    std::optional<Team> team{};

    [[nodiscard]] inline auto operator==(const BuyZone&) const noexcept -> bool = default;
};

/// The buy zones of `map`, in the order of its entities: those with a brush model.
[[nodiscard]] auto buyZonesOf(const nx::assets::BspView& map) -> std::vector<BuyZone>;

/// Whether `map` lets the players of `team` buy: as its info_map_parameters' "buying" says, all of them
/// without one (CMapInfo::CheckMapInfo).
[[nodiscard]] auto mapLetsBuy(const nx::assets::BspView& map, Team team) -> bool;

/// How many hostages `map` holds as it starts: its entities of kHostageClass.
[[nodiscard]] auto hostagesOf(const nx::assets::BspView& map) -> std::uint32_t;

/// Makes in `world` the game of `map` as it starts: the components of nx.game, and the map's brush
/// entities as the game makes them by `classes`. Errors: Unspawnable.
[[nodiscard]] auto spawnGame(const nx::assets::BspView& map, const nx::game::EntityClasses& classes,
                             nx::ecs::World& world) -> nx::Result<void, GameMapError>;

/// A map as the game plays it, read once. It stays where it is made: what plays it points into it.
class GameMap {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The map `name` of `vfs` (maps/<name>.bsp). Errors: Unreadable (a buy zone of a brush model it lacks
    /// too), HullsInvalid.
    [[nodiscard]] static auto load(const nx::assets::Vfs& vfs, std::string_view name)
        -> nx::Result<std::unique_ptr<GameMap>, GameMapError>;
    /// The map of `bytes`, a BSP file, `name` naming it in errors. Errors: as load's.
    [[nodiscard]] static auto read(std::vector<std::byte> bytes, std::string_view name)
        -> nx::Result<std::unique_ptr<GameMap>, GameMapError>;

    /// Use load() or read(). Public only so that they can use std::make_unique.
    inline explicit GameMap(CreateToken /*token*/) noexcept {}

    GameMap(const GameMap&) = delete;
    auto operator=(const GameMap&) -> GameMap& = delete;
    GameMap(GameMap&&) = delete;
    auto operator=(GameMap&&) -> GameMap& = delete;
    ~GameMap() = default;

    /// Its name, de_dust2.
    [[nodiscard]] inline auto name() const noexcept -> std::string_view { return m_name; }
    /// Its BSP, read whole.
    [[nodiscard]] inline auto map() const noexcept -> const nx::assets::BspView& { return *m_map; }
    /// The hulls of its brush models, which its traces go through.
    [[nodiscard]] inline auto hulls() const noexcept -> const nx::collision::BrushHulls& { return *m_hulls; }
    /// The spawn points of `team` (spawnPointsOf).
    [[nodiscard]] inline auto spawnPoints(Team team) const noexcept -> std::span<const SpawnPoint> {
        return m_spawnPoints[team];
    }
    /// What its rounds are played for (mapObjectivesOf).
    [[nodiscard]] inline auto objectives() const noexcept -> MapObjectives { return m_objectives; }
    /// How many hostages it holds as it starts (hostagesOf).
    [[nodiscard]] inline auto hostages() const noexcept -> std::uint32_t { return m_hostages; }
    /// Whether it lets the players of `team` buy (mapLetsBuy).
    [[nodiscard]] inline auto letsBuy(Team team) const noexcept -> bool { return m_letsBuy[team]; }
    /// Whether a player of `team`, `body` at `origin`, is where its team buys (SIGNAL_BUY): in a buy zone of
    /// its team or of both (CBuyZone::BuyTouch); on a map without any, within `spawnReach` of a spawn point
    /// of its team (OLD_CheckBuyZone).
    [[nodiscard]] auto isBuyingPlace(Team team, const nx::game::Body& body, nx::Vec3 origin,
                                     nx::HammerUnits spawnReach) const -> bool;
    /// What the texture of the face of brush model `model` the segment from `start` to `end`, in the
    /// model's own space, first meets (nx::assets::surfaceAt) is made of, by `materials`: concrete when
    /// it meets none (PM_TraceTexture).
    [[nodiscard]] auto materialAlong(const TextureMaterials& materials, nx::assets::ModelIndex model,
                                     nx::Vec3 start, nx::Vec3 end) const -> TextureMaterial;

private:
    std::string m_name;
    std::vector<std::byte> m_bytes;
    std::optional<nx::assets::BspView> m_map;
    std::optional<nx::collision::BrushHulls> m_hulls;
    nx::EnumArray<Team, std::vector<SpawnPoint>> m_spawnPoints;
    MapObjectives m_objectives;
    std::uint32_t m_hostages = 0;
    std::vector<BuyZone> m_buyZones;
    nx::EnumArray<Team, bool> m_letsBuy{};
    std::vector<std::optional<nx::assets::SurfaceExtent>> m_surfaceExtents; ///< By face.
};

} // namespace cs
