// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.game;
import cs.shared;

namespace cs {

namespace {

constexpr std::string_view kSpriteExtension = ".spr";

/// Whether `model` names a sprite.
[[nodiscard]] auto isSprite(std::string_view model) noexcept -> bool {
    return model.size() >= kSpriteExtension.size() &&
           nx::equalsIgnoringCase(model.substr(model.size() - kSpriteExtension.size()), kSpriteExtension);
}

/// The model entity `entity` is, as the game makes it; none for another.
[[nodiscard]] auto mapModelOf(const nx::assets::BspEntity& entity) -> std::optional<MapModel> {
    const std::string_view classname = entity.find("classname").value_or("");
    const std::string_view model = entity.find("model").value_or("");
    const nx::Vec3 angles = nx::game::anglesOf(entity);
    MapModel made{.origin = entity.findVector("origin").value_or(nx::Vec3{}),
                  .angles = {.pitch = nx::Degrees{angles.x},
                             .yaw = nx::Degrees{angles.y},
                             .roll = nx::Degrees{angles.z}},
                  .sequence =
                      static_cast<std::uint32_t>(std::max(entity.findInteger("sequence").value_or(0), 0))};
    const EntityRendering rendering = renderingOf(entity);
    made.mode = rendering.mode;
    made.amount = rendering.amount;
    if (classname == "cycler_sprite" && !model.empty() && !isSprite(model)) {
        made.model = std::string{model};
        made.kind = ModelClass::Cycler;
        made.framerate = entity.findNumber("framerate").value_or(0.0F);
        return made;
    }
    if (classname == kHostageClass) {
        made.model = std::string{model.empty() ? kDefaultHostageModel : model};
        made.kind = ModelClass::Hostage;
        made.framerate = 1.0F; // ResetSequenceInfo
        made.skin = static_cast<std::uint32_t>(std::max(entity.findInteger("skin").value_or(0), 0));
        return made;
    }
    return std::nullopt;
}

/// The sequence a cycler of `view`'s model plays: its own, or the model's first when it lacks it.
[[nodiscard]] auto cyclerSequenceOf(const MapModel& model, const nx::assets::MdlView& view)
    -> nx::assets::SequenceIndex {
    return nx::assets::SequenceIndex{model.sequence < view.sequences().size() ? model.sequence : 0};
}

/// The bones of a model of `file`, playing `sequence` at `frame`, as the client draws them: its
/// sequence's motion removed, set as `controllers` says, at `origin`, turned by `angles`, the pitch
/// negated as the engine's studio transform negates it (R_StudioSetUpTransform's "stupid quake bug").
[[nodiscard]] auto drawnBonesOf(const nx::assets::MdlView& view, const nx::assets::AnimationFrames& animation,
                                nx::assets::SequenceIndex sequence, cs::SequenceFrame frame,
                                const cs::ControllerSettings& controllers, nx::Vec3 origin,
                                nx::EulerAngles angles) -> std::vector<nx::Mat3x4> {
    const nx::EulerAngles turn{.pitch = -angles.pitch, .yaw = angles.yaw, .roll = angles.roll};
    return cs::poseBones(cs::skeletonOf(view), view.sequences()[sequence.value], animation,
                         {.frame = frame,
                          .controllers = controllers,
                          .motion = SequenceMotion::Removed,
                          .placement = nx::affine(nx::rotationMatrix(turn), origin)});
}

} // namespace

auto mapModelsOf(const nx::assets::BspView& map) -> std::vector<MapModel> {
    std::vector<MapModel> models;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (std::optional<MapModel> model = mapModelOf(entity)) {
            models.push_back(std::move(*model));
        }
    }
    return models;
}

auto MapModels::read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map) -> MapModels {
    MapModels read;
    std::map<std::string, std::optional<std::size_t>, std::less<>> files; // by path; none: left out
    for (MapModel& model : mapModelsOf(map)) {
        const std::size_t spawn = read.hostagesBySpawn.size();
        if (model.kind == ModelClass::Hostage) {
            read.hostagesBySpawn.emplace_back();
        }
        auto [found, isNew] = files.try_emplace(model.model | std::views::transform(nx::toLowerAscii) |
                                                std::ranges::to<std::string>()); // the VFS ignores case
        if (isNew) {
            auto animated = readAnimatedModel(vfs, model.model, ModelTextures::Read);
            if (!animated) {
                read.m_missing.push_back(std::format("{} ({})", model.model, nx::describe(animated.error())));
                continue;
            }
            found->second = read.m_files.size();
            read.m_files.push_back(std::move(*animated));
        }
        if (!found->second) {
            continue;
        }
        const std::size_t file = *found->second;
        if (model.kind == ModelClass::Cycler) {
            model.sequence = cyclerSequenceOf(model, *read.m_files[file].files->view).value;
            read.m_cyclers.push_back(Entity{.model = std::move(model), .file = file});
        } else {
            read.hostagesBySpawn[spawn] = Entity{.model = std::move(model), .file = file};
        }
    }
    return read;
}

auto MapModels::hostageOf(std::uint32_t spawn) const noexcept -> const Entity* {
    return spawn < hostagesBySpawn.size() && hostagesBySpawn[spawn] ? &*hostagesBySpawn[spawn] : nullptr;
}

auto MapModels::hostageModel(std::uint32_t spawn) const noexcept -> const nx::assets::MdlView* {
    const Entity* const hostage = hostageOf(spawn);
    return hostage != nullptr ? &*m_files[hostage->file].files->view : nullptr;
}

auto MapModels::cyclerPoseOf(const Entity& cycler, nx::Seconds sinceStart) const -> std::vector<nx::Mat3x4> {
    const AnimatedModel& file = m_files[cycler.file];
    const nx::assets::MdlView& view = *file.files->view;
    const nx::assets::SequenceIndex sequence{cycler.model.sequence};
    return drawnBonesOf(
        view, file.animations[sequence.value], sequence,
        cs::sequenceFrameAt(view.sequences()[sequence.value], sinceStart, cycler.model.framerate), {},
        cycler.model.origin, cycler.model.angles);
}

auto MapModels::hostagePoseOf(const Entity& hostage, const ShownHostage& shown) const
    -> std::vector<nx::Mat3x4> {
    const AnimatedModel& file = m_files[hostage.file];
    return drawnBonesOf(*file.files->view, file.animations[shown.sequence.value], shown.sequence, shown.frame,
                        shown.controllers, shown.origin, shown.angles);
}

auto MapModels::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<std::vector<nx::render::StudioModel>, nx::render::StudioError> {
    std::vector<nx::render::StudioModel> models;
    models.reserve(m_files.size());
    for (const AnimatedModel& file : m_files) {
        auto model =
            renderer.load(commands, {.model = *file.files->view, .textures = file.files->texturesView()});
        if (!model) {
            return std::unexpected{std::move(model.error())};
        }
        models.push_back(std::move(*model));
    }
    return models;
}

auto MapModels::posed(std::span<const nx::render::StudioModel> models, nx::Seconds sinceStart,
                      std::span<const ShownHostage> hostages, const nx::render::WorldLight& light,
                      const nx::render::SkyLight& sky, const nx::render::LightStyleValues& styles) const
    -> PosedModels {
    nx::expects(models.size() == m_files.size(), "MapModels::posed: a model for each file");
    PosedModels posed;
    struct Drawn {
        const Entity* entity = nullptr;
        nx::Vec3 origin{};
    };
    std::vector<Drawn> drawn;
    posed.bones.reserve(m_cyclers.size() + hostages.size());
    for (const Entity& cycler : m_cyclers) {
        posed.bones.push_back(cyclerPoseOf(cycler, sinceStart));
        drawn.push_back(Drawn{.entity = &cycler, .origin = cycler.model.origin});
    }
    for (const ShownHostage& shown : hostages) {
        const Entity* const hostage = hostageOf(shown.spawn);
        if (hostage == nullptr) {
            continue; // one whose model it could not read, which it listed
        }
        posed.bones.push_back(hostagePoseOf(*hostage, shown));
        drawn.push_back(Drawn{.entity = hostage, .origin = shown.origin});
    }
    // The instances point into the bones, which no longer move.
    for (std::size_t index = 0; index < drawn.size(); ++index) {
        if (std::optional<nx::render::StudioInstance> instance = instanceOf(
                *drawn[index].entity, drawn[index].origin, posed.bones[index], models, light, sky, styles)) {
            posed.instances.push_back(*instance);
        }
    }
    return posed;
}

auto MapModels::instanceOf(const Entity& entity, nx::Vec3 origin, std::span<const nx::Mat3x4> bones,
                           std::span<const nx::render::StudioModel> models,
                           const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                           const nx::render::LightStyleValues& styles) const
    -> std::optional<nx::render::StudioInstance> {
    if (entity.file >= models.size()) {
        return std::nullopt; // the contract's observe path
    }
    return nx::render::StudioInstance{.model = models[entity.file],
                                      .bones = bones,
                                      .light = light.modelLightAt({.origin = origin, .sky = sky}, styles),
                                      .skin = entity.model.skin,
                                      .origin = origin,
                                      .mode = entity.model.mode,
                                      .amount = entity.model.amount};
}

} // namespace cs
