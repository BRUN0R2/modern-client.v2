// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The sprites the map's entities show (docs/05-rendering.md, section 10), as the game makes them
/// until its server spawns its entities: env_sprite, env_glow and the cycler_sprite of a sprite, as
/// ReGameDLL's CSprite, CGlow and CCyclerSprite make and animate them (GPL, consulted: docs/20
/// section 3), and the line of sight that hides a glow, as the original's client traces it.
export module tool.nxview:mapSprites;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.collision;

export namespace nxview {

/// Which entity of the game shows a sprite: how it animates it.
enum class SpriteClass : std::uint8_t {
    Sprite, ///< env_sprite: animates when its framerate and frames ask, from when it turns on; one that
            ///< plays once turns off after its last frame; one with a name that does not start on is
            ///< off until something turns it on.
    Glow,   ///< env_glow: animates when its framerate and frames ask, a tenth of a second in.
    Cycler, ///< cycler_sprite: animates when its frames ask, a tenth of a second in.
};

/// A sprite entity of the map, as the game spawns it.
struct MapSprite {
    std::string model{}; ///< Its file, "sprites/glow01.spr".
    SpriteClass kind = SpriteClass::Sprite;
    nx::Vec3 origin{};
    nx::EulerAngles angles{}; ///< An env_sprite's yaw alone is its roll, as the game moves it.
    float scale = 0.0F;
    float framerate = 0.0F; ///< Frames a second.
    nx::render::RenderMode mode = nx::render::RenderMode::Normal;
    nx::render::RenderEffect effect = nx::render::RenderEffect::None;
    std::uint8_t amount = 0;
    nx::render::RenderColor color{};
    bool isOn = true;
    bool playsOnce = false;
};

/// The sprite entities of `map`: those whose model is a sprite (".spr").
[[nodiscard]] auto mapSpritesOf(const nx::assets::BspView& map) -> std::vector<MapSprite>;

/// The frame `sprite`, of `frameCount` frames, shows `sinceStart` after the map started, as its game
/// class's think animates it ten times a second, its last frame index the end of its loop
/// (CSprite::Animate, CGlow::Animate, CCyclerSprite::Animate); none when it is off.
[[nodiscard]] auto frameAt(const MapSprite& sprite, std::size_t frameCount, nx::Seconds sinceStart)
    -> std::optional<float>;

/// What hides a glowing sprite from the view: the world and the brush entities drawn normally, as the
/// original's client traces its line of sight (EV_VisTraceLine, through glass and studio models).
class GlowBlockers {
public:
    explicit GlowBlockers(const nx::assets::BspView& map);

    /// Whether the line from `eye` to `point` stops 8 units or more short of it among `hulls`, the brush
    /// entities where `placements` puts them (R_SpriteGlowBlend).
    [[nodiscard]] auto hides(const nx::collision::BrushHulls& hulls,
                             std::span<const nx::render::ModelPlacement> placements, nx::Vec3 eye,
                             nx::Vec3 point) const -> bool;

private:
    std::vector<bool> m_isGlass; ///< By brush model: drawn other than normally, which the line passes.
};

/// The map's sprite entities and their sprites' files.
class MapSprites {
public:
    /// Reads the sprite of every sprite entity of `map` from `vfs`, each file once; an entity whose
    /// sprite is missing or unreadable is left out, and listed.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map) -> MapSprites;

    MapSprites(MapSprites&& other) noexcept = default;
    auto operator=(MapSprites&& other) noexcept -> MapSprites& = default;
    MapSprites(const MapSprites&) = delete;
    auto operator=(const MapSprites&) -> MapSprites& = delete;
    ~MapSprites() = default;

    /// The files left out: missing or unreadable, with why.
    [[nodiscard]] inline auto missing() const noexcept -> std::span<const std::string> { return m_missing; }

    /// Every sprite on the GPU of `renderer`, a model for each file, its uploads recorded in
    /// `commands`. Errors: those of SpriteRenderer::load.
    [[nodiscard]] auto load(nx::render::SpriteRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<std::vector<nx::render::SpriteModel>, nx::render::SpriteError>;

    /// The sprites that show `sinceStart` after the map started, drawn with `models`, what load gave:
    /// none hidden yet.
    [[nodiscard]] auto instances(std::span<const nx::render::SpriteModel> models,
                                 nx::Seconds sinceStart) const -> std::vector<nx::render::SpriteInstance>;

private:
    /// A sprite file: its bytes, which its view points into.
    struct File {
        std::string path{};
        std::vector<std::byte> bytes{};
        std::optional<nx::assets::SpriteView> view{};
    };

    MapSprites() = default;

    std::vector<std::unique_ptr<File>> m_files;
    std::vector<std::pair<MapSprite, std::size_t>> m_entities; ///< Each with its file's index.
    std::vector<std::string> m_missing;
};

} // namespace nxview
