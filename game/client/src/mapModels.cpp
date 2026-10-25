// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

namespace cs {

namespace {

constexpr std::string_view kSpriteExtension = ".spr";
/// The model of a hostage the map names none for (CHostage::Precache).
constexpr std::string_view kDefaultHostageModel = "models/scientist.mdl";
/// The activity a hostage idles in (ACT_IDLE).
constexpr std::int32_t kIdleActivity = 1;
/// The controllers the game sets straight ahead for a hostage: its four channels; the mouth's is not
/// an entity's controller.
constexpr std::uint32_t kHostageControllers = 4;

/// Whether `model` names a sprite.
[[nodiscard]] auto isSprite(std::string_view model) noexcept -> bool {
    return model.size() >= kSpriteExtension.size() &&
           nx::equalsIgnoringCase(model.substr(model.size() - kSpriteExtension.size()), kSpriteExtension);
}

/// The model entity `entity` is, as the game makes it; none for another.
[[nodiscard]] auto mapModelOf(const nx::assets::BspEntity& entity) -> std::optional<MapModel> {
    const std::string_view classname = entity.find("classname").value_or("");
    const std::string_view model = entity.find("model").value_or("");
    const nx::Vec3 angles = anglesOf(entity);
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

/// The sequence `model` plays of `files`' model: a hostage's first idle one (the game picks one of
/// them at random by weight, LookupActivity), a sequence the model lacks its first.
[[nodiscard]] auto sequenceOf(const MapModel& model, const nx::assets::MdlView& view)
    -> nx::assets::SequenceIndex {
    const std::span<const nx::assets::StudioSequence> sequences = view.sequences();
    if (model.kind == ModelClass::Hostage) {
        const auto idle = std::ranges::find(sequences, kIdleActivity, &nx::assets::StudioSequence::activity);
        return nx::assets::SequenceIndex{
            idle == sequences.end() ? 0 : nx::narrow<std::uint32_t>(idle - sequences.begin())};
    }
    return nx::assets::SequenceIndex{model.sequence < sequences.size() ? model.sequence : 0};
}

/// The controllers of `model` the game sets for `kind`: a hostage's straight ahead, 0 degrees
/// (CHostage::Spawn, SetBoneController); a cycler's none.
[[nodiscard]] auto controllersOf(ModelClass kind, const nx::assets::MdlView& view) -> cs::ControllerSettings {
    cs::ControllerSettings settings;
    if (kind != ModelClass::Hostage) {
        return settings;
    }
    for (std::uint32_t channel = 0; channel < kHostageControllers; ++channel) {
        const auto found =
            std::ranges::find(view.boneControllers(), channel, &nx::assets::BoneController::channel);
        if (found != view.boneControllers().end()) {
            settings.channels.at(channel) = cs::controllerSettingOf(*found, 0.0F);
        }
    }
    return settings;
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
        auto [found, isNew] = files.try_emplace(model.model | std::views::transform(nx::toLowerAscii) |
                                                std::ranges::to<std::string>()); // the VFS ignores case
        if (isNew) {
            auto modelFiles = readModelFiles(vfs, model.model);
            if (!modelFiles) {
                read.m_missing.push_back(
                    std::format("{} ({})", model.model, nx::describe(modelFiles.error())));
                continue;
            }
            found->second = read.m_files.size();
            read.m_files.push_back(std::make_unique<File>(File{.files = std::move(*modelFiles)}));
        }
        if (!found->second) {
            continue;
        }
        File& file = *read.m_files[*found->second];
        const nx::assets::MdlView& view = *file.files->view;
        const nx::assets::SequenceIndex sequence = sequenceOf(model, view);
        if (!file.animations.contains(sequence.value)) {
            auto frames = animationOf(vfs, *file.files, sequence);
            if (!frames) {
                read.m_missing.push_back(std::format("{} ({})", model.model, nx::describe(frames.error())));
                continue;
            }
            file.animations.emplace(sequence.value, std::move(*frames));
        }
        model.sequence = sequence.value;
        const cs::ControllerSettings controllers = controllersOf(model.kind, view);
        read.m_entities.push_back(
            Entity{.model = std::move(model), .file = *found->second, .controllers = controllers});
    }
    return read;
}

void MapModels::dropToFloor(nx::ecs::World& world, const nx::collision::BrushHulls& hulls) {
    for (Entity& entity : m_entities) {
        if (entity.model.kind == ModelClass::Hostage) {
            entity.model.origin = nx::game::droppedToFloor(world, hulls, kHostageBox, entity.model.origin);
        }
    }
}

auto MapModels::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<std::vector<nx::render::StudioModel>, nx::render::StudioError> {
    std::vector<nx::render::StudioModel> models;
    models.reserve(m_files.size());
    for (const std::unique_ptr<File>& file : m_files) {
        auto model =
            renderer.load(commands, {.model = *file->files->view, .textures = file->files->texturesView()});
        if (!model) {
            return std::unexpected{std::move(model.error())};
        }
        models.push_back(std::move(*model));
    }
    return models;
}

auto MapModels::posed(std::span<const nx::render::StudioModel> models, nx::Seconds sinceStart,
                      const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                      const nx::render::LightStyleValues& styles) const -> PosedModels {
    nx::expects(models.size() == m_files.size(), "MapModels::posed: a model for each file");
    PosedModels posed;
    posed.bones.reserve(m_entities.size());
    for (const Entity& entity : m_entities) {
        const File& file = *m_files[entity.file];
        const nx::assets::MdlView& view = *file.files->view;
        const nx::assets::StudioSequence& sequence = view.sequences()[entity.model.sequence];
        // The engine's studio transform turns a model by its entity's angles, the pitch negated
        // (R_StudioSetUpTransform's "stupid quake bug").
        const nx::EulerAngles turn{.pitch = -entity.model.angles.pitch,
                                   .yaw = entity.model.angles.yaw,
                                   .roll = entity.model.angles.roll};
        posed.bones.push_back(
            cs::poseBones(cs::skeletonOf(view), sequence, file.animations.at(entity.model.sequence),
                          {.frame = cs::sequenceFrameAt(sequence, sinceStart, entity.model.framerate),
                           .controllers = entity.controllers,
                           .placement = nx::affine(nx::rotationMatrix(turn), entity.model.origin)}));
    }
    // The instances point into the bones, which no longer move.
    for (std::size_t index = 0; index < m_entities.size(); ++index) {
        const Entity& entity = m_entities[index];
        if (entity.file >= models.size()) {
            continue; // the contract's observe path
        }
        posed.instances.push_back(nx::render::StudioInstance{
            .model = models[entity.file],
            .bones = posed.bones[index],
            .light = light.modelLightAt({.origin = entity.model.origin, .sky = sky}, styles),
            .skin = entity.model.skin,
            .origin = entity.model.origin,
            .mode = entity.model.mode,
            .amount = entity.model.amount});
    }
    return posed;
}

} // namespace cs
