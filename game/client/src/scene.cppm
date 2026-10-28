// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the client draws of a level (docs/05-rendering.md): the world and the light it gives, the
/// sprites and studio models of its entities, the blended ones of them all in one order, the particles
/// over them, and the weapon in the player's hand, drawn last in its own depths; then its muzzle flashes,
/// effects added as the original adds them, in those depths.
export module cs.client:scene;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import :level;
import :playerModels;
import :entityModels;
import :viewModel;
import :muzzleFlash;
import :tempEntities;
import :particles;

export namespace cs {

/// Why the scene could not be made or drawn.
enum class SceneError : std::uint8_t {
    ShadersUnreadable, ///< A shader of the scene could not be read (cause: the FileError).
    CreateFailed,      ///< The GPU refused a renderer or a model (cause: the renderer's error).
    DrawFailed,        ///< The GPU refused the room a frame needed (cause: the renderer's error).
};

/// The shaders the scene draws with: SPIR-V, as the build compiles them next to the programs.
struct SceneShaders {
    std::vector<std::byte> world;
    std::vector<std::byte> studio;
    std::vector<std::byte> sprite;
    std::vector<std::byte> particle;
    std::vector<std::byte> probe; ///< The enhanced look's reflection probes'.
};

/// The shaders of the scene in `folder`: world.spv, studio.spv, sprite.spv, particle.spv and probe.spv.
/// Errors: ShadersUnreadable.
[[nodiscard]] auto readSceneShaders(const std::filesystem::path& folder)
    -> nx::Result<SceneShaders, SceneError>;

/// What a frame of the scene shows.
struct SceneView {
    nx::render::Camera camera{};
    float aspect = 1.0F;
    nx::Seconds time{};                         ///< Since the map started.
    nx::render::LightStyleValues lightStyles{}; ///< The server's styles at time (ClientGame::lightStylesAt).
    std::span<const nx::render::ModelPlacement> placements{};
    std::span<const ShownHostage> hostages{}; ///< How the game's hostages show (ClientGame::writeHostages).
    std::span<const ShownPlayer> players{};   ///< How the other players show (ClientGame::updatePlayers).
    std::span<const ShownEntity> entities{};
    std::optional<ViewModelPose> viewModel{};   ///< None without a weapon in hand.
    std::span<const TempEntity> tempEntities{}; ///< The client's (TempEntities::shown).
    std::span<const Particle> particles{};      ///< The client's (Particles::shown).
    std::span<const Streak> streaks{};          ///< The client's (Particles::shownStreaks).
    /// The client events the view model passed since the last frame (ViewModelEvents).
    std::span<const PassedEvent> clientEvents{};
    Hand hand = Hand::Right; ///< The player's, which it holds its weapons in (cl_righthand).
};

/// A level on the GPU. A renderer draws once a frame: the view's own model has its own, drawn last.
class Scene {
public:
    /// The scene of `level`, which must outlive it, for targets of `format`, its effects for targets of
    /// `effectsFormat` (drawEffects), drawn with `shaders` and `gamma` in `look`, its uploads recorded in
    /// `commands`, keeping `decalLimit` decals on faces (setDecalLimit), the decals its map stamps for good
    /// as its game starts stamped. Errors: CreateFailed.
    [[nodiscard]] static auto create(nx::rhi::Device& device, nx::rhi::CommandList& commands,
                                     const Level& level, const SceneShaders& shaders, nx::rhi::Format format,
                                     nx::rhi::Format effectsFormat, const nx::render::GammaSettings& gamma,
                                     nx::render::StudioLook look, std::size_t decalLimit)
        -> nx::Result<Scene, SceneError>;

    /// Draws `view` of `level`, the level the scene was made of: the world, the studio models of its
    /// entities, each lit where it is, the sprites of its entities, a glow the player's line of sight
    /// does not reach hidden, and its temporary entities, each as translucent as it is, the blended ones of
    /// them all in one order (nx.render:blendedOrder); its particles over them, each of its entry of the
    /// engine's palette, none without it, as opaque as it is at the view's time (amountOf), and its
    /// streaks after them, each as long and as opaque as it is then (endOf, amountOf); then the
    /// weapon in the player's hand, lit where the
    /// player's eye is, as the original lights the view's own model, and in the enhanced look by the light of
    /// its flashes too. It prepares the muzzle flashes, those of the view's client events and those still
    /// showing, for drawEffects to record. Errors: DrawFailed.
    [[nodiscard]] auto draw(nx::rhi::CommandList& list, const Level& level, const SceneView& view)
        -> nx::Result<void, SceneError>;
    /// Records the effects the last draw prepared, the weapon's muzzle flashes, as the original adds
    /// them: display colors, as bright as their styles make them, each channel stopping at white where
    /// its target stops it; in the weapon's depths, so that a wall the muzzle is pushed against hides
    /// none. In a render pass whose color target is of the effects' format, the screen in the legacy
    /// look, nx::render::PostRenderer's effects' layer in the enhanced look, its depth target the one
    /// draw drew with.
    void drawEffects(nx::rhi::CommandList& list) const;

    /// Puts on the GPU the view models `level` has now of `weapons` (none for one it has none of), and
    /// the sprites of the muzzle flashes it read since, its uploads recorded in `commands`. The GPU must
    /// be done with the frames that drew a model this replaces. Errors: CreateFailed.
    [[nodiscard]] auto loadWeapons(nx::rhi::CommandList& commands, const Level& level,
                                   std::span<const WeaponIndex> weapons) -> nx::Result<void, SceneError>;
    /// Appends the catalog's textured models outside a render pass; existing GPU models stay valid
    /// for in-flight frames. Errors: CreateFailed, retaining the material/upload failure as its cause.
    [[nodiscard]] auto loadEntities(nx::rhi::CommandList& commands, const EntityCatalog& catalog,
                                    const nx::assets::Vfs& vfs) -> nx::Result<void, SceneError>;

    /// Records the baking of the next of the level's reflection probes in the enhanced look, outside a
    /// render pass and before the frame's draw: the world as `view` shows it, including the server's
    /// light styles and brush placements (nx::render::ReflectionProbes::bakeNext); nothing once they
    /// are baked, or in the legacy look.
    void bakeReflections(nx::rhi::CommandList& list, const SceneView& view);

    /// Stamps decal `decal`, of the level's decals, near `position` on the faces of `on`, the world's or
    /// a brush entity's where the game has it, as nx::render::BspWorld::shootDecal does; returns the
    /// faces it was stamped on.
    auto shootDecal(nx::render::DecalImageIndex decal, nx::Vec3 position, const nx::render::DecalTarget& on)
        -> std::uint32_t;
    /// Takes the next decals within `limit`, as nx::render::BspWorld::setDecalLimit does: `r_decals` as
    /// it is now.
    inline void setDecalLimit(std::size_t limit) { m_world.setDecalLimit(limit); }

    /// The look it was made in: what its frames are drawn through (cs::FrameRenderer).
    [[nodiscard]] inline auto look() const noexcept -> nx::render::StudioLook { return m_look; }

    /// What the world drew in its last frame.
    [[nodiscard]] inline auto lastFrame() const noexcept -> const nx::render::FrameStats& {
        return m_world.lastFrame();
    }
    /// What the world has.
    [[nodiscard]] inline auto worldStats() const noexcept -> const nx::render::WorldStats& {
        return m_world.stats();
    }

private:
    Scene(nx::render::BspWorld world, nx::render::WorldLight light, nx::render::SpriteRenderer sprites,
          std::vector<nx::render::SpriteModel> spriteModels, nx::render::StudioRenderer studio,
          std::vector<nx::render::StudioModel> models, nx::render::StudioRenderer viewModels,
          nx::render::SpriteRenderer viewSprites, nx::render::ParticleRenderer particles) noexcept;

    /// Forgets the muzzle flashes over at `time` or of another weapon than `inHand`'s; then adds those
    /// of `events`, the client events passed by `viewModel`, that of `inHand` posed as `bones`, of the
    /// sprites of `level`.
    void flash(const Level& level, const ViewModel* viewModel, std::optional<WeaponIndex> inHand,
               std::span<const PassedEvent> events, std::span<const nx::Mat3x4> bones, nx::Seconds time);

    nx::render::BspWorld m_world;
    nx::render::WorldLight m_light;
    nx::render::SpriteRenderer m_sprites;
    std::vector<nx::render::SpriteModel> m_spriteModels; ///< Of the level's sprites, by file.
    nx::render::StudioRenderer m_studio;
    std::vector<nx::render::StudioModel> m_models; ///< Of the level's models, by file.
    nx::render::StudioRenderer m_viewModels;
    /// What the weapon in hand shows, its muzzle flashes: the effects, as display colors.
    nx::render::SpriteRenderer m_viewSprites;
    nx::render::ParticleRenderer particleRenderer;
    /// The weapons' view models, by their WeaponIndex; none for one the level has none of.
    std::vector<std::optional<nx::render::StudioModel>> m_weapons;
    PlayerGpuModels m_playerModels; ///< On m_studio: the characters' and the weapons' player models.
    EntityModels entityModels;
    /// The sprites of the muzzle flashes, on m_viewSprites, by their place in the level's
    /// muzzle flashes' EffectSprites.
    std::vector<std::optional<nx::render::SpriteModel>> m_flashModels;
    MuzzleFlashes m_flashes; ///< Those showing, or their light.
    /// The blood's sprites, on m_sprites, by their place in the level's bloodSprites; and each temporary
    /// entity's sprite's place among them, none for one the level could not read.
    std::vector<std::optional<nx::render::SpriteModel>> bloodModels;
    nx::EnumArray<TempSprite, std::optional<std::uint32_t>> tempSpriteIndices{};
    std::uint64_t m_flashCount = 0; ///< The flashes made, which seed each one's chances.
    std::optional<nx::render::ReflectionProbes> m_probes; ///< The enhanced look's.
    nx::render::StudioLook m_look = nx::render::StudioLook::Legacy;
};

} // namespace cs
