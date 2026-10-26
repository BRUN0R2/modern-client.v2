// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The studio models the map's entities show (docs/05-rendering.md, section 9): the cycler_sprite of a
/// model, as the client shows it until the game makes it (ReGameDLL's CCyclerSprite; GPL, consulted:
/// docs/20 section 3), and the hostage_entity, which the game makes (cs::spawnHostages) and plays, the
/// client showing it as the game says; and the files of their models, which the game plays too.
export module cs.client:mapModels;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :modelFiles;

export namespace cs {

/// Which entity of the game shows a studio model: how it plays it.
enum class ModelClass : std::uint8_t {
    Cycler,  ///< cycler_sprite: its sequence at its framerate, from when the map starts.
    Hostage, ///< hostage_entity: as the game plays it (cs::Hostage); scientist.mdl when the map names no
             ///< model.
};

/// A studio model entity of the map, as the game spawns it.
struct MapModel {
    std::string model{}; ///< Its file, "models/palmtree.mdl".
    ModelClass kind = ModelClass::Cycler;
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    std::uint32_t sequence = 0;
    float framerate = 0.0F;
    std::uint32_t skin = 0;
    nx::render::RenderMode mode = nx::render::RenderMode::Normal;
    std::uint8_t amount = 0;
};

/// The studio model entities of `map`: its cycler_sprites of a model (not ".spr") and its hostages.
[[nodiscard]] auto mapModelsOf(const nx::assets::BspView& map) -> std::vector<MapModel>;

/// The map's models posed for a frame: their bones, which their instances point into.
struct PosedModels {
    std::vector<std::vector<nx::Mat3x4>> bones{};
    std::vector<nx::render::StudioInstance> instances{};
};

/// The map's studio model entities and their models' files and animations.
class MapModels {
public:
    /// Reads the model of every studio model entity of `map` from `vfs`, its `*T.mdl` when its textures
    /// are not in it, and the animation of the sequence each entity plays, each file once; an entity
    /// whose model or animation cannot be read is left out, and listed.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map) -> MapModels;

    MapModels(MapModels&& other) noexcept = default;
    auto operator=(MapModels&& other) noexcept -> MapModels& = default;
    MapModels(const MapModels&) = delete;
    auto operator=(const MapModels&) -> MapModels& = delete;
    ~MapModels() = default;

    /// The models left out: missing or unreadable, with why.
    [[nodiscard]] inline auto missing() const noexcept -> std::span<const std::string> { return m_missing; }

    /// Its models as the game plays them, by the index of their file: each file's and the frames of each
    /// of its sequences.
    [[nodiscard]] inline auto playedModels() const noexcept -> std::span<const cs::PlayedModel> {
        return m_played;
    }

    /// The map's hostages, of models it read, in the map's order, for the game to make (cs::spawnHostages):
    /// their models among playedModels, where they are, and how they are turned.
    [[nodiscard]] auto hostageSpawns() const -> std::vector<HostageSpawn>;

    /// Every model on the GPU of `renderer`, a model for each file, its uploads recorded in `commands`.
    /// Errors: those of StudioRenderer::load.
    [[nodiscard]] auto load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<std::vector<nx::render::StudioModel>, nx::render::StudioError>;

    /// The models `sinceStart` after the map started, drawn with `models`, what load gave, each lit
    /// where its entity is by `light`, the sky's `sky` and the styles' `styles`: the cyclers, then the
    /// hostages as `hostages` shows them (cs::hostagesShownAt).
    [[nodiscard]] auto posed(std::span<const nx::render::StudioModel> models, nx::Seconds sinceStart,
                             std::span<const ShownHostage> hostages, const nx::render::WorldLight& light,
                             const nx::render::SkyLight& sky,
                             const nx::render::LightStyleValues& styles) const -> PosedModels;

private:
    /// A model's files, and the decoded frames of each of its sequences, by index.
    struct File {
        std::unique_ptr<ModelFiles> files{};
        std::vector<nx::assets::AnimationFrames> animations{};
    };

    /// An entity and its file's index.
    struct Entity {
        MapModel model{};
        std::size_t file = 0;
    };

    MapModels() = default;

    /// The bones of `cycler` `sinceStart` after the map started.
    [[nodiscard]] auto cyclerPoseOf(const Entity& cycler, nx::Seconds sinceStart) const
        -> std::vector<nx::Mat3x4>;

    /// The bones of `hostage`, as `shown` shows it.
    [[nodiscard]] auto hostagePoseOf(const Entity& hostage, const ShownHostage& shown) const
        -> std::vector<nx::Mat3x4>;

    /// A model's instance of `entity`, posed as `bones`, drawn with `models`, lit where it is.
    [[nodiscard]] auto instanceOf(const Entity& entity, nx::Vec3 origin, std::span<const nx::Mat3x4> bones,
                                  std::span<const nx::render::StudioModel> models,
                                  const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                                  const nx::render::LightStyleValues& styles) const
        -> std::optional<nx::render::StudioInstance>;

    std::vector<std::unique_ptr<File>> m_files;
    std::vector<cs::PlayedModel> m_played; ///< One for each file.
    std::vector<Entity> m_cyclers;
    std::vector<Entity> m_hostages; ///< In the map's order: the game's hostages' spawns.
    std::vector<std::string> m_missing;
};

} // namespace cs
