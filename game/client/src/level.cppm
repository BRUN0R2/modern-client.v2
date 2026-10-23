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
    GameUnspawnable,     ///< The map's brush entities could not be made (cause).
    ViewModelUnreadable, ///< A weapon's view model could not be read (cause).
};

/// The game's files: the Counter-Strike 1.6 installation through its VFS, the classes the game makes
/// the map's entities by (data/cstrike/entities.toml), its weapons (data/cstrike/ammo.toml and
/// data/cstrike/weapons/<id>.toml), and what a player of each team spawns with
/// (data/cstrike/loadouts.toml).
struct GameContent {
    nx::assets::Vfs vfs;
    nx::game::EntityClasses entityClasses;
    WeaponCatalog weapons;
    Loadouts loadouts;
};

/// The user's own content of the game in `userFolder` (nx::platform::userDirectory): its
/// addons/cstrike/ folder, the original's cstrike_addon (docs/06-assets-and-resources.md, section
/// 2.1).
[[nodiscard]] auto addonsFolderOf(const std::filesystem::path& userFolder) -> std::filesystem::path;

/// Opens the game's content: from `data`, the project's data/ folder, the description of the
/// installation's files, the entity classes, the weapons, each in the file named after its id, and
/// the loadouts; and the installation at `install`, or the one Steam installed, below `addons`, the
/// user's own content (addonsFolderOf), when it is given and is a folder: a file there is read
/// instead of the installation's. Errors: ContentUnavailable.
[[nodiscard]] auto openGameContent(const std::filesystem::path& data,
                                   const std::optional<std::filesystem::path>& install,
                                   const std::optional<std::filesystem::path>& addons)
    -> nx::Result<GameContent, LevelError>;

/// A map as the client plays it: its world, the WADs of its textures, its sky, its hulls, the light
/// the game gives it, the sprites and studio models its entities show, the decals it shows and the mark
/// a bullet leaves on each of its brush models, and the view models of the game's weapons and the
/// sprites of their muzzle flashes, read once; the view model of a weapon made or changed as the
/// game runs read then, as no precache limits it. It stays where it is made: the views point into
/// it, and what is made of it (the light of nx::render::WorldLight) to its map.
class Level {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The map `name` of `content` (maps/<name>.bsp). A WAD, the sky, a sprite or a studio model the
    /// installation lacks is left out and logged, as the original carries on without it; its
    /// hostages are dropped to the floor, as the game drops them. Errors: MapUnreadable,
    /// HullsInvalid, GameUnspawnable, ViewModelUnreadable.
    [[nodiscard]] static auto load(const GameContent& content, std::string_view name)
        -> nx::Result<std::unique_ptr<Level>, LevelError>;

    /// Use load(). Public only so that load() can use std::make_unique.
    explicit Level(CreateToken /*token*/) noexcept {}

    Level(const Level&) = delete;
    auto operator=(const Level&) -> Level& = delete;
    Level(Level&&) = delete;
    auto operator=(Level&&) -> Level& = delete;
    ~Level() = default;

    [[nodiscard]] inline auto map() const noexcept -> const nx::assets::BspView& { return *m_map; }
    [[nodiscard]] inline auto entityClasses() const noexcept -> const nx::game::EntityClasses& {
        return m_entityClasses;
    }
    [[nodiscard]] inline auto wads() const noexcept -> std::span<const nx::assets::WadView> { return m_wads; }
    /// Its sides, in the order of nx::render::kSkySuffixes; none when no sky could be read.
    [[nodiscard]] inline auto sky() const noexcept -> std::span<const nx::assets::RgbaImage> { return m_sky; }
    [[nodiscard]] inline auto lightStyles() const noexcept -> const nx::render::LightStyles& {
        return m_lightStyles;
    }
    [[nodiscard]] inline auto skyLight() const noexcept -> const nx::render::SkyLight& { return m_skyLight; }
    /// The lights of its light entities, which give models highlights in the enhanced look.
    [[nodiscard]] inline auto lights() const noexcept -> std::span<const nx::render::StudioLight> {
        return m_lights;
    }
    [[nodiscard]] inline auto hulls() const noexcept -> const nx::collision::BrushHulls& { return *m_hulls; }
    [[nodiscard]] inline auto glowBlockers() const noexcept -> const GlowBlockers& { return *m_glowBlockers; }
    [[nodiscard]] inline auto sprites() const noexcept -> const MapSprites& { return *m_sprites; }
    [[nodiscard]] inline auto models() const noexcept -> const MapModels& { return *m_models; }
    /// The sprites of the weapons' muzzle flashes.
    [[nodiscard]] inline auto muzzleFlashes() const noexcept -> const MuzzleFlashSprites& {
        return *m_muzzleFlashes;
    }
    /// How many weapons the level read the view models of: those of the catalog as it last read it.
    [[nodiscard]] inline auto weaponCount() const noexcept -> std::size_t { return m_viewModels.size(); }
    /// The view model of `weapon`; none for one not complete, or whose view model could not be read
    /// as it was changed.
    [[nodiscard]] auto viewModel(WeaponIndex weapon) const noexcept -> const ViewModel*;
    /// Reads the view model of each weapon of `weapons` made, or whose values were set, since the
    /// level read it, from `vfs`: those weapons, in their order. One that cannot be read leaves its
    /// weapon without one, logged, and the game plays on.
    auto updateViewModels(const nx::assets::Vfs& vfs, const WeaponCatalog& weapons)
        -> std::vector<WeaponIndex>;
    /// The decals of decals.wad the level shows: those bullets leave (bulletDecals).
    [[nodiscard]] inline auto decals() const noexcept -> const LevelDecals& { return *m_decals; }
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
    std::vector<BulletMark> m_bulletMarks; ///< By brush model.
    std::vector<nx::assets::RgbaImage> m_sky;
    nx::render::LightStyles m_lightStyles;
    nx::render::SkyLight m_skyLight;
    std::vector<nx::render::StudioLight> m_lights;
    std::optional<nx::collision::BrushHulls> m_hulls;
    std::optional<GlowBlockers> m_glowBlockers;
    std::optional<MapSprites> m_sprites;
    std::optional<MapModels> m_models;
    /// A weapon's view model, as the revision of its values it was read from.
    struct WeaponViewModel {
        std::optional<ViewModel> model{};
        std::uint32_t revision = 0;
    };

    std::vector<WeaponViewModel> m_viewModels; ///< By WeaponIndex.
    std::optional<MuzzleFlashSprites> m_muzzleFlashes;
};

/// Makes in `world` the game of `map` as it starts: the components of nx.game, and the map's brush
/// entities as the game makes them by `classes`. Errors: GameUnspawnable.
[[nodiscard]] auto spawnGame(const nx::assets::BspView& map, const nx::game::EntityClasses& classes,
                             nx::ecs::World& world) -> nx::Result<void, LevelError>;

/// Where a player of `map` first stands, its eye and the way it looks: at the first
/// counter-terrorist spawn, else the first terrorist one; the origin without either.
[[nodiscard]] auto spawnView(const nx::assets::BspView& map) -> nx::render::Camera;

/// The team of the spawn spawnView stands a player at: the counter-terrorists' without either.
[[nodiscard]] auto spawnTeam(const nx::assets::BspView& map) -> Team;

} // namespace cs
