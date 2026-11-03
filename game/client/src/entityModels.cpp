// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;
import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :modelMaterials;

namespace cs {
auto EntityModels::load(const EntityCatalog& catalog, nx::render::StudioRenderer& renderer,
                        const nx::assets::Vfs& vfs, nx::rhi::CommandList& commands)
    -> nx::Result<void, EntityModelError> {
    while (models.size() < catalog.modelCount()) {
        const AnimatedModel& file = catalog.model(EntityModelIndex{nx::narrow<std::uint32_t>(models.size())});
        auto materials = readModelMaterials(vfs, file.files->path, file.files->texturesView());
        if (!materials) {
            return nx::fail(EntityModelError::MaterialsInvalid, std::move(materials.error()));
        }
        auto loaded = renderer.load(
            commands,
            {.model = *file.files->view, .textures = file.files->texturesView(), .materials = *materials});
        if (!loaded) {
            return nx::fail(EntityModelError::UploadFailed, std::move(loaded.error()));
        }
        models.push_back(std::move(*loaded));
    }
    return {};
}

void EntityModels::pose(const EntityCatalog& catalog, std::span<const ShownEntity> entities,
                        const ModelFrame& frame, PosedModels& posed) const {
    posed.clear();
    // A body the frame may show, what it is drawn with, and where its bones are among them all.
    struct Posing {
        const ShownEntity* entity = nullptr;
        const nx::render::StudioModel* drawn = nullptr;
        const AnimatedModel* file = nullptr;
        nx::Vec3 origin{};
        std::size_t firstBone = 0;
        nx::render::ModelLight light{};
    };
    std::vector<Posing> posings;
    posings.reserve(entities.size());
    std::size_t boneCount = 0;
    for (const ShownEntity& entity : entities) {
        const EntityType& type = catalog.type(entity.type);
        nx::expects(type.model.value < models.size(), "EntityModels: upload before drawing");
        const AnimatedModel& file = catalog.model(type.model);
        const nx::Vec3 origin = entity.origin + type.definition.modelOffset.vector();
        const nx::assets::MdlView& view = *file.files->view;
        if (frame.world.mayShow(
                shownBoxOf(origin, std::array{view.sequences()[entity.sequence.value].bounds}))) {
            posings.push_back(Posing{.entity = &entity,
                                     .drawn = &models[type.model.value],
                                     .file = &file,
                                     .origin = origin,
                                     .firstBone = boneCount});
            boneCount += view.bones().size();
        }
    }
    posed.bones.resize(boneCount);
    const std::span<nx::Mat3x4> bones = posed.bones;
    poseAll(frame, {.label = "entities.pose", .priority = nx::jobs::Priority::Critical}, posings.size(),
            [&posings, bones, &frame](std::size_t index) {
                Posing& posing = posings[index];
                const ShownEntity& entity = *posing.entity;
                const nx::assets::MdlView& view = *posing.file->files->view;
                poseBones(skeletonOf(view), view.sequences()[entity.sequence.value],
                          posing.file->animations[entity.sequence.value],
                          {.frame = entity.frame,
                           .placement = nx::affine(nx::rotationMatrix(entity.angles), posing.origin)},
                          bones.subspan(posing.firstBone, view.bones().size()));
                posing.light =
                    frame.light.modelLightAt({.origin = posing.origin, .sky = frame.sky}, frame.styles);
            });
    posed.instances.reserve(posings.size());
    for (const Posing& posing : posings) {
        posed.instances.push_back(
            {.model = *posing.drawn,
             .bones = bones.subspan(posing.firstBone, posing.file->files->view->bones().size()),
             .light = posing.light,
             .origin = posing.origin,
             .mode = renderModeOf(posing.entity->amount),
             .amount = posing.entity->amount});
    }
}
} // namespace cs
