// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A map as the client draws it (docs/06-assets-and-resources.md): the map as the game plays it
/// (cs::GameMap), and what its world and entities show, read once, which the views point into.
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

/// Why the client's content or a map of it could not be read.
enum class LevelError : std::uint8_t {
    GameUnspawnable,     ///< The map's entities could not be made (cause).
    ViewModelUnreadable, ///< A weapon's view model could not be read (cause).
};

/// A map as the client draws it: the map as the game plays it, the WADs of its textures, its sky, the
/// light the game gives it, the sprites and studio models its entities show, the decals it shows, those
/// it stamps as it starts and the mark a bullet leaves on each of its brush models, and the view models
/// of the game's weapons and the sprites of their muzzle flashes, read once; the view model of a weapon
/// made or changed as the game runs read then, with the sprites of its flashes the level lacks, as no
/// precache limits them. It stays where it is made: the views point into it, and what is made of it
/// (the light of nx::render::WorldLight) to its map.
class Level {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The level of `map` of `content`, which must both outlive it.
    /// A WAD, the sky, a sprite or a studio model the installation lacks is left out and logged, as the
    /// original carries on without it. Errors: GameUnspawnable, ViewModelUnreadable.
    [[nodiscard]] static auto load(const GameContent& content, const GameMap& map)
        -> nx::Result<std::unique_ptr<Level>, LevelError>;

    /// Use load(). Public only so that load() can use std::make_unique.
    inline explicit Level(CreateToken /*token*/) noexcept {}

    Level(const Level&) = delete;
    auto operator=(const Level&) -> Level& = delete;
    Level(Level&&) = delete;
    auto operator=(Level&&) -> Level& = delete;
    ~Level() = default;

    /// The map as the game plays it.
    [[nodiscard]] inline auto gameMap() const noexcept -> const GameMap& { return *m_gameMap; }
    /// Its BSP, read whole: its game map's.
    [[nodiscard]] inline auto map() const noexcept -> const nx::assets::BspView& { return m_gameMap->map(); }
    /// The WADs its textures are in, those the installation has.
    [[nodiscard]] inline auto wads() const noexcept -> std::span<const nx::assets::WadView> { return m_wads; }
    /// Its sides, in the order of nx::render::kSkySuffixes; none when no sky could be read.
    [[nodiscard]] inline auto sky() const noexcept -> std::span<const nx::assets::RgbaImage> { return m_sky; }
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
    /// The hulls of its brush models, which its traces go through: its game map's.
    [[nodiscard]] inline auto hulls() const noexcept -> const nx::collision::BrushHulls& {
        return m_gameMap->hulls();
    }
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
    /// The mark a bullet leaves on brush model `model` of the map, one it has: Gunshot on the world,
    /// what bulletMarkOf says of its entity on a brush entity's (Gunshot without one).
    [[nodiscard]] auto bulletMarkOn(nx::assets::ModelIndex model) const noexcept -> BulletMark;

private:
    const GameMap* m_gameMap = nullptr;
    std::vector<std::vector<std::byte>> m_wadBytes;
    std::vector<nx::assets::WadView> m_wads;
    std::optional<LevelDecals> m_decals;
    std::vector<MapDecal> m_mapDecals;
    std::vector<BulletMark> m_bulletMarks; ///< By brush model.
    std::vector<nx::assets::RgbaImage> m_sky;
    nx::render::SkyLight m_skyLight;
    std::vector<nx::render::StudioLight> m_lights;
    std::vector<nx::Vec3> m_probePositions;
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

/// The view of a player standing at the first spawn point of `map` (firstSpawnOf), its eye and the way
/// it looks; the origin without one.
[[nodiscard]] auto spawnView(const nx::assets::BspView& map) -> nx::render::Camera;

} // namespace cs
