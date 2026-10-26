// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game's content and a map of it as the client plays it (docs/06-assets-and-resources.md): the
/// Counter-Strike 1.6 installation through its VFS, the classes the game makes entities by, and a
/// map read once with what its world and entities show, which the views point into.
export module cs.client:level;

import std;
import nx.core;
import nx.assets;
import nx.render;
import nx.collision;
import nx.ecs;
import nx.game;
import cs.shared;
import :mapLight;
import :mapSprites;
import :mapModels;
import :viewModel;
import :muzzleFlash;
import :levelDecals;
import :bulletMarks;

export namespace cs {

/// Why the game's content or a map of it could not be read.
enum class LevelError : std::uint8_t {
    ContentUnavailable,  ///< The project's data or the installation could not be read (cause).
    MapUnreadable,       ///< maps/<name>.bsp is missing or invalid (cause).
    HullsInvalid,        ///< The map's hulls could not be built (cause).
    GameUnspawnable,     ///< The map's entities could not be made (cause).
    ViewModelUnreadable, ///< A weapon's view model could not be read (cause).
};

/// The game's files: the Counter-Strike 1.6 installation through its VFS, the classes the game makes
/// the map's entities by (data/cstrike/entities.toml), its weapons (data/cstrike/ammo.toml and
/// data/cstrike/weapons/<id>.toml), what a player of each team spawns with
/// (data/cstrike/loadouts.toml), the light styles it sets as a map starts
/// (data/cstrike/lightStyles.toml), the money of its rounds (data/cstrike/economy.toml), the sounds of
/// a player's feet (data/cstrike/footsteps.toml), and what the textures are made of (the
/// installation's sound/materials.txt; none tagged without it).
struct GameContent {
    nx::assets::Vfs vfs;
    nx::game::EntityClasses entityClasses;
    WeaponCatalog weapons;
    Loadouts loadouts;
    WorldLightStyles lightStyles;
    EconomyRules economy;
    FootstepRules footsteps;
    TextureMaterials materials;
};

/// The user's own content of the game in `userFolder` (nx::platform::userDirectory): its
/// addons/cstrike/ folder, the original's cstrike_addon (docs/06-assets-and-resources.md, section
/// 2.1).
[[nodiscard]] auto addonsFolderOf(const std::filesystem::path& userFolder) -> std::filesystem::path;

/// A place a player of a team spawns at, where it stands and the way it faces.
struct SpawnPoint {
    nx::Vec3 origin{};
    nx::Degrees yaw{};

    [[nodiscard]] inline auto operator==(const SpawnPoint&) const noexcept -> bool = default;
};

/// The spawn points of `team` on `map`, in the order of its entities: its class's
/// (`info_player_deathmatch` for the terrorists, `info_player_start` for the counter-terrorists)
/// with an origin other than the world's, as SelectSpawnSpot takes them.
[[nodiscard]] auto spawnPointsOf(const nx::assets::BspView& map, Team team) -> std::vector<SpawnPoint>;

/// What `map` is played for: the objectives the classes of its entities give it (objectiveOfClass).
[[nodiscard]] auto mapObjectivesOf(const nx::assets::BspView& map) -> MapObjectives;

/// How many hostages `map` holds as it starts: its entities of kHostageClass.
[[nodiscard]] auto hostagesOf(const nx::assets::BspView& map) -> std::uint32_t;

/// Opens the game's content: from `data`, the project's data/ folder, the description of the
/// installation's files, the entity classes, the weapons, each in the file named after its id, the
/// loadouts, the light styles, the money of the rounds and the footsteps; what the textures are made
/// of, from the installation; and the installation at `install`, or the one
/// Steam installed, below `addons`, the user's own content (addonsFolderOf), when it is given and is a
/// folder: a file there is read instead of the installation's. Errors: ContentUnavailable.
[[nodiscard]] auto openGameContent(const std::filesystem::path& data,
                                   const std::optional<std::filesystem::path>& install,
                                   const std::optional<std::filesystem::path>& addons)
    -> nx::Result<GameContent, LevelError>;

/// A map as the client plays it: its world, the WADs of its textures, its sky, its hulls, the light
/// the game gives it, the sprites and studio models its entities show, the decals it shows, those it
/// stamps as it starts and the mark a bullet leaves on each of its brush models, and the view models of
/// the game's weapons and the sprites of their muzzle flashes, read once; the view model of a weapon
/// made or changed as the game runs read then, with the sprites of its flashes the level lacks, as no
/// precache limits them. It stays where it is made: the views point into it, and what is made of it
/// (the light of nx::render::WorldLight) to its map.
class Level {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The map `name` of `content` (maps/<name>.bsp). A WAD, the sky, a sprite or a studio model the
    /// installation lacks is left out and logged, as the original carries on without it. Errors:
    /// MapUnreadable, HullsInvalid, GameUnspawnable, ViewModelUnreadable.
    [[nodiscard]] static auto load(const GameContent& content, std::string_view name)
        -> nx::Result<std::unique_ptr<Level>, LevelError>;

    /// Use load(). Public only so that load() can use std::make_unique.
    inline explicit Level(CreateToken /*token*/) noexcept {}

    Level(const Level&) = delete;
    auto operator=(const Level&) -> Level& = delete;
    Level(Level&&) = delete;
    auto operator=(Level&&) -> Level& = delete;
    ~Level() = default;

    /// Its BSP, read whole.
    [[nodiscard]] inline auto map() const noexcept -> const nx::assets::BspView& { return *m_map; }
    /// The classes the game makes its entities by.
    [[nodiscard]] inline auto entityClasses() const noexcept -> const nx::game::EntityClasses& {
        return m_entityClasses;
    }
    /// The WADs its textures are in, those the installation has.
    [[nodiscard]] inline auto wads() const noexcept -> std::span<const nx::assets::WadView> { return m_wads; }
    /// Its sides, in the order of nx::render::kSkySuffixes; none when no sky could be read.
    [[nodiscard]] inline auto sky() const noexcept -> std::span<const nx::assets::RgbaImage> { return m_sky; }
    /// The patterns its light styles play.
    [[nodiscard]] inline auto lightStyles() const noexcept -> const nx::render::LightStyles& {
        return m_lightStyles;
    }
    /// The sky's light, its light_environment's.
    [[nodiscard]] inline auto skyLight() const noexcept -> const nx::render::SkyLight& { return m_skyLight; }
    /// Where its reflection probes go (probePositionsOf), in the enhanced look.
    [[nodiscard]] inline auto probePositions() const noexcept -> std::span<const nx::Vec3> {
        return m_probePositions;
    }
    /// The lights of its light entities, which give models highlights in the enhanced look.
    [[nodiscard]] inline auto lights() const noexcept -> std::span<const nx::render::StudioLight> {
        return m_lights;
    }
    /// The hulls of its brush models, which its traces go through.
    [[nodiscard]] inline auto hulls() const noexcept -> const nx::collision::BrushHulls& { return *m_hulls; }
    /// What hides a glow from the eye.
    [[nodiscard]] inline auto glowBlockers() const noexcept -> const GlowBlockers& { return *m_glowBlockers; }
    /// The sprites its entities show.
    [[nodiscard]] inline auto sprites() const noexcept -> const MapSprites& { return *m_sprites; }
    /// The studio models its entities show.
    [[nodiscard]] inline auto models() const noexcept -> const MapModels& { return *m_models; }
    /// The sprites of the weapons' muzzle flashes: those their view models' events show, as their
    /// weapons' data styles them.
    [[nodiscard]] inline auto muzzleFlashes() const noexcept -> const MuzzleFlashSprites& {
        return m_muzzleFlashes;
    }
    /// The weapons the level read the view models of: those of the catalog as it last read it.
    [[nodiscard]] auto weapons() const -> std::vector<WeaponIndex>;
    /// The view model of `weapon`; none for one not complete, or whose view model could not be read
    /// as it was changed.
    [[nodiscard]] auto viewModel(WeaponIndex weapon) const noexcept -> const ViewModel*;
    /// Reads the view model of each weapon of `weapons` made, or whose values were set, since the
    /// level read it, from `vfs`, and the sprites of its muzzle flashes the level lacks: those weapons,
    /// in their order. One that cannot be read leaves its weapon without one, logged, and the game
    /// plays on; a sprite likewise its flashes.
    auto updateViewModels(const nx::assets::Vfs& vfs, const WeaponCatalog& weapons)
        -> std::vector<WeaponIndex>;
    /// The decals of decals.wad the level shows: those bullets leave (bulletDecals), and those its map
    /// stamps as its game starts.
    [[nodiscard]] inline auto decals() const noexcept -> const LevelDecals& { return *m_decals; }
    /// The decals its map stamps for good as its game starts (nx::game::staticDecalsOf), in their
    /// order: those of decals.wad.
    [[nodiscard]] inline auto mapDecals() const noexcept -> std::span<const MapDecal> { return m_mapDecals; }
    /// The spawn points of `team` (spawnPointsOf).
    [[nodiscard]] inline auto spawnPoints(Team team) const noexcept -> std::span<const SpawnPoint> {
        return m_spawnPoints[team];
    }
    /// What its rounds are played for (mapObjectivesOf).
    [[nodiscard]] inline auto objectives() const noexcept -> MapObjectives { return m_objectives; }
    /// How many hostages it holds as it starts (hostagesOf).
    [[nodiscard]] inline auto hostages() const noexcept -> std::uint32_t { return m_hostages; }
    /// What the texture of the face of brush model `model` the segment from `start` to `end`, in the
    /// model's own space, first meets (nx::assets::surfaceAt) is made of, by `materials`: concrete when
    /// it meets none (PM_TraceTexture).
    [[nodiscard]] auto materialAlong(const TextureMaterials& materials, nx::assets::ModelIndex model,
                                     nx::Vec3 start, nx::Vec3 end) const -> TextureMaterial;
    /// The mark a bullet leaves on brush model `model` of the map, one it has: Gunshot on the world,
    /// what bulletMarkOf says of its entity on a brush entity's (Gunshot without one).
    [[nodiscard]] auto bulletMarkOn(nx::assets::ModelIndex model) const noexcept -> BulletMark;

private:
    std::vector<std::byte> m_mapBytes;
    std::optional<nx::assets::BspView> m_map;
    nx::game::EntityClasses m_entityClasses;
    std::vector<std::vector<std::byte>> m_wadBytes;
    std::vector<nx::assets::WadView> m_wads;
    std::optional<LevelDecals> m_decals;
    std::vector<MapDecal> m_mapDecals;
    std::vector<BulletMark> m_bulletMarks; ///< By brush model.
    nx::EnumArray<Team, std::vector<SpawnPoint>> m_spawnPoints;
    MapObjectives m_objectives;
    std::uint32_t m_hostages = 0;
    std::vector<std::optional<nx::assets::SurfaceExtent>> m_surfaceExtents; ///< By face.
    std::vector<nx::assets::RgbaImage> m_sky;
    nx::render::LightStyles m_lightStyles;
    nx::render::SkyLight m_skyLight;
    std::vector<nx::render::StudioLight> m_lights;
    std::vector<nx::Vec3> m_probePositions;
    std::optional<nx::collision::BrushHulls> m_hulls;
    std::optional<GlowBlockers> m_glowBlockers;
    std::optional<MapSprites> m_sprites;
    std::optional<MapModels> m_models;
    /// A weapon's view model, as the revision of its values it was read from.
    struct WeaponViewModel {
        std::optional<ViewModel> model{};
        std::uint32_t revision = 0;
    };

    /// Reads the sprites the muzzle flashes of `viewModel` show that the level lacks, from `vfs`; logs
    /// those it cannot.
    void readMuzzleFlashes(const nx::assets::Vfs& vfs, const ViewModel& viewModel);

    std::vector<WeaponViewModel> m_viewModels; ///< By WeaponIndex.
    MuzzleFlashSprites m_muzzleFlashes;
};

/// Makes in `world` the game of `map` as it starts: the components of nx.game, and the map's brush
/// entities as the game makes them by `classes`. Errors: GameUnspawnable.
[[nodiscard]] auto spawnGame(const nx::assets::BspView& map, const nx::game::EntityClasses& classes,
                             nx::ecs::World& world) -> nx::Result<void, LevelError>;

/// The view of a player standing at the first spawn point of `map`, its eye and the way it looks: a
/// counter-terrorist one, else a terrorist one; the origin without either.
[[nodiscard]] auto spawnView(const nx::assets::BspView& map) -> nx::render::Camera;

/// The team of the spawn point spawnView stands a player at: the counter-terrorists' without either.
[[nodiscard]] auto spawnTeam(const nx::assets::BspView& map) -> Team;

} // namespace cs
