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

/// Opens the game's content: from `data`, the project's data/ folder, the description of the
/// installation's files, the entity classes, the weapons, each in the file named after its id, and
/// the loadouts; and the installation at `install`, or the one Steam installed. Errors:
/// ContentUnavailable.
[[nodiscard]] auto openGameContent(const std::filesystem::path& data,
                                   const std::optional<std::filesystem::path>& install)
    -> nx::Result<GameContent, LevelError>;

/// A map as the client plays it: its world, the WADs of its textures, its sky, its hulls, the light
/// the game gives it, the sprites and studio models its entities show, and the view models of the
/// game's weapons and the sprites of their muzzle flashes, read once. It stays where it is made: the views
/// point into it, and what is made of it (the light of nx::render::WorldLight) to its map.
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
    [[nodiscard]] inline auto hulls() const noexcept -> const nx::collision::BrushHulls& { return *m_hulls; }
    [[nodiscard]] inline auto glowBlockers() const noexcept -> const GlowBlockers& { return *m_glowBlockers; }
    [[nodiscard]] inline auto sprites() const noexcept -> const MapSprites& { return *m_sprites; }
    [[nodiscard]] inline auto models() const noexcept -> const MapModels& { return *m_models; }
    /// The sprites of the weapons' muzzle flashes.
    [[nodiscard]] inline auto muzzleFlashes() const noexcept -> const MuzzleFlashSprites& {
        return *m_muzzleFlashes;
    }
    /// The view models of the game's weapons, by their WeaponIndex.
    [[nodiscard]] inline auto viewModels() const noexcept -> std::span<const ViewModel> {
        return m_viewModels;
    }
    /// The decals a bullet leaves on the world, of decals.wad: those of kGunshotDecals it has.
    [[nodiscard]] inline auto gunshotDecals() const noexcept -> std::span<const nx::assets::MipTexture> {
        return m_gunshotDecals;
    }

private:
    std::vector<std::byte> m_mapBytes;
    std::optional<nx::assets::BspView> m_map;
    nx::game::EntityClasses m_entityClasses;
    std::vector<std::vector<std::byte>> m_wadBytes;
    std::vector<nx::assets::WadView> m_wads;
    std::vector<std::byte> m_decalWadBytes;
    std::vector<nx::assets::MipTexture> m_gunshotDecals; ///< Views of m_decalWadBytes.
    std::vector<nx::assets::RgbaImage> m_sky;
    nx::render::LightStyles m_lightStyles;
    nx::render::SkyLight m_skyLight;
    std::optional<nx::collision::BrushHulls> m_hulls;
    std::optional<GlowBlockers> m_glowBlockers;
    std::optional<MapSprites> m_sprites;
    std::optional<MapModels> m_models;
    std::vector<ViewModel> m_viewModels;
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
