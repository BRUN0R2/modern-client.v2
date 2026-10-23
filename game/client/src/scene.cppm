// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the client draws of a level (docs/05-rendering.md): the world and the light it gives, the
/// sprites and studio models of its entities, the blended ones of them all in one order, the muzzle
/// flashes of the weapon in the player's hand, and the weapon, drawn last.
export module cs.client:scene;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import :level;
import :viewModel;
import :muzzleFlash;

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
    std::vector<std::byte> probe; ///< The enhanced look's reflection probes'.
};

/// The shaders of the scene in `folder`: world.spv, studio.spv, sprite.spv and probe.spv. Errors:
/// ShadersUnreadable.
[[nodiscard]] auto readSceneShaders(const std::filesystem::path& folder)
    -> nx::Result<SceneShaders, SceneError>;

/// What a frame of the scene shows.
struct SceneView {
    nx::render::Camera camera{};
    float aspect = 1.0F;
    nx::Seconds time{}; ///< Since the map started.
    std::span<const nx::render::ModelPlacement> placements{};
    std::optional<ViewModelPose> viewModel{}; ///< None without a weapon in hand.
    /// The client events the view model passed since the last frame (ViewModelEvents).
    std::span<const nx::assets::StudioEvent* const> clientEvents{};
};

/// A level on the GPU. A renderer draws once a frame: the view's own model has its own, drawn last.
class Scene {
public:
    /// The scene of `level`, which must outlive it, for targets of `format`, drawn with `shaders`
    /// and `gamma`, its uploads recorded in `commands`. Errors: CreateFailed.
    [[nodiscard]] static auto create(nx::rhi::Device& device, nx::rhi::CommandList& commands,
                                     const Level& level, const SceneShaders& shaders, nx::rhi::Format format,
                                     const nx::render::GammaSettings& gamma, nx::render::StudioLook look)
        -> nx::Result<Scene, SceneError>;

    /// Draws `view` of `level`, the level the scene was made of: the world, the studio models of its
    /// entities, each lit where it is, and the sprites of its entities, a glow the player's line of
    /// sight does not reach hidden, the blended ones of them all in one order
    /// (nx.render:blendedOrder), among them the muzzle flashes of the view's client events and those
    /// still showing; then the weapon in the player's hand, lit
    /// where the player's eye is, as the original lights the view's own model. Errors: DrawFailed.
    [[nodiscard]] auto draw(nx::rhi::CommandList& list, const Level& level, const SceneView& view)
        -> nx::Result<void, SceneError>;

    /// Puts on the GPU the view models `level` has now of `weapons` (none for one it has none of), its
    /// uploads recorded in `commands`. The GPU must be done with the frames that drew a model this
    /// replaces. Errors: CreateFailed.
    [[nodiscard]] auto loadWeapons(nx::rhi::CommandList& commands, const Level& level,
                                   std::span<const WeaponIndex> weapons) -> nx::Result<void, SceneError>;

    /// Records the baking of the next of the level's reflection probes in the enhanced look, outside a
    /// render pass and before the frame's draw: the world as the frame shows it at `time`, its brush
    /// entities where `placements` puts them (nx::render::ReflectionProbes::bakeNext); nothing once they
    /// are baked, or in the legacy look.
    void bakeReflections(nx::rhi::CommandList& list, const Level& level, nx::Seconds time,
                         std::span<const nx::render::ModelPlacement> placements);

    /// Stamps decal `decal`, of the level's decals, near `position` on the faces of `on`, the world's or
    /// a brush entity's where the game has it, as nx::render::BspWorld::shootDecal does; returns the
    /// faces it was stamped on.
    auto shootDecal(std::uint32_t decal, nx::Vec3 position, const nx::render::DecalTarget& on)
        -> std::uint32_t;

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
          std::vector<std::optional<nx::render::StudioModel>> weapons,
          std::vector<std::optional<nx::render::SpriteModel>> flashModels) noexcept;

    /// Adds the muzzle flashes of `events`, the client events `viewModel` passed, its bones `bones`, at
    /// `time`; and forgets those shown long enough.
    void flash(const ViewModel* viewModel, std::span<const nx::assets::StudioEvent* const> events,
               std::span<const nx::Mat3x4> bones, nx::Seconds time);

    nx::render::BspWorld m_world;
    nx::render::WorldLight m_light;
    nx::render::SpriteRenderer m_sprites;
    std::vector<nx::render::SpriteModel> m_spriteModels; ///< Of the level's sprites, by file.
    nx::render::StudioRenderer m_studio;
    std::vector<nx::render::StudioModel> m_models; ///< Of the level's models, by file.
    nx::render::StudioRenderer m_viewModels;
    /// The weapons' view models, by their WeaponIndex; none for one the level has none of.
    std::vector<std::optional<nx::render::StudioModel>> m_weapons;
    std::vector<std::optional<nx::render::SpriteModel>> m_flashModels; ///< Of sprites/muzzleflash1.spr to 3.
    std::vector<MuzzleFlash> m_flashes;                                ///< Those showing.
    std::uint64_t m_flashCount = 0; ///< The flashes made, which seed each one's chances.
    std::optional<nx::render::ReflectionProbes> m_probes; ///< The enhanced look's.
};

} // namespace cs
