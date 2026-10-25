// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The studio models the map's entities show (docs/05-rendering.md, section 9), as the game makes them
/// until its server spawns its entities: the cycler_sprite of a model and the hostage_entity, as
/// ReGameDLL's CCyclerSprite and CHostage make them (GPL, consulted: docs/20 section 3).
export module cs.client:mapModels;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ecs;
import nx.collision;
import cs.shared;
import :modelFiles;

export namespace cs {

/// Which entity of the game shows a studio model: how it plays it.
enum class ModelClass : std::uint8_t {
    Cycler,  ///< cycler_sprite: its sequence at its framerate, from when the map starts.
    Hostage, ///< hostage_entity: its idle sequence at its own speed, its head straight ahead, dropped to
             ///< the floor; scientist.mdl when the map names no model.
};

/// A hostage's box about its origin, at its feet (VEC_HOSTAGE_HULL_MIN, VEC_HOSTAGE_HULL_MAX).
inline constexpr nx::Aabb kHostageBox{.min = {.x = -10.0F, .y = -10.0F, .z = 0.0F},
                                      .max = {.x = 10.0F, .y = 10.0F, .z = 62.0F}};

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

    /// Drops the hostages to the floor of the world of `hulls` and of the solid brush entities of
    /// `world` (DROP_TO_FLOOR), as the game does when it makes them.
    void dropToFloor(nx::ecs::World& world, const nx::collision::BrushHulls& hulls);

    /// The boxes of its entities that stop a player (SOLID_SLIDEBOX): each hostage's, kHostageBox
    /// about where it stands.
    [[nodiscard]] auto solidBoxes() const -> std::vector<SolidBox>;

    /// The server's way of `hull` from `start` to `end` through the hostages `sinceStart` after the map
    /// started, where it takes the place of `sofar`, what the way met otherwise (keepFirstStop): each
    /// hostage whose box (kHostageBox, a unit more each way, as the engine links it) the way's box
    /// meets (SV_ClipToLinks) is met as SV_HullForStudioModel chooses, a line by its hitboxes, its
    /// bones posed with its sequence's motion kept, and a hull by its box grown by the hull's. Posed
    /// when the way is traced, where the original's server poses a hostage as its last think left it,
    /// at most a thirtieth of a second before. None when no hostage takes its place.
    [[nodiscard]] auto traceHostages(nx::collision::Hull hull, nx::Vec3 start, nx::Vec3 end,
                                     nx::Seconds sinceStart, const nx::collision::Trace& sofar) const
        -> std::optional<nx::collision::Trace>;

    /// Every model on the GPU of `renderer`, a model for each file, its uploads recorded in `commands`.
    /// Errors: those of StudioRenderer::load.
    [[nodiscard]] auto load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<std::vector<nx::render::StudioModel>, nx::render::StudioError>;

    /// The models `sinceStart` after the map started, drawn with `models`, what load gave, each lit
    /// where its entity is by `light`, the sky's `sky` and the styles' `styles`.
    [[nodiscard]] auto posed(std::span<const nx::render::StudioModel> models, nx::Seconds sinceStart,
                             const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                             const nx::render::LightStyleValues& styles) const -> PosedModels;

private:
    /// A model's files, and the animations its entities play.
    struct File {
        std::unique_ptr<ModelFiles> files{};
        std::map<std::uint32_t, nx::assets::AnimationFrames> animations{}; ///< By sequence.
    };

    /// An entity, its file's index, and the settings of its controllers.
    struct Entity {
        MapModel model{};
        std::size_t file = 0;
        cs::ControllerSettings controllers{};
    };

    MapModels() = default;

    /// traceHostages' way through `hostage` alone, before what it met is weighed; none for a model
    /// without hitboxes, which a line does not meet.
    [[nodiscard]] auto traceHostage(const Entity& hostage, nx::collision::Hull hull, nx::Vec3 start,
                                    nx::Vec3 end, nx::Seconds sinceStart) const
        -> std::optional<nx::collision::Trace>;

    /// The bones of `entity` `sinceStart` after the map started, its sequence's motion as `motion` says.
    [[nodiscard]] auto poseOf(const Entity& entity, nx::Seconds sinceStart, SequenceMotion motion) const
        -> std::vector<nx::Mat3x4>;

    std::vector<std::unique_ptr<File>> m_files;
    std::vector<Entity> m_entities;
    std::vector<std::string> m_missing;
};

} // namespace cs
