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
                        const ModelFrame& frame, PosedModels& posed) {
    posed.clear();
    posings.resize(entities.size());
    // Each batch writes those it may show at the start of its own part, and the parts close up after.
    struct Written {
        std::size_t first = 0;
        std::size_t count = 0;
    };
    std::vector<Written> written(
        frame.jobs != nullptr ? frame.jobs->batchCountOf(entities.size(), kSmallestPosedBatch) : 1);
    const auto findBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        std::size_t count = 0;
        for (const ShownEntity& entity : entities.subspan(first, last - first)) {
            const EntityType& type = catalog.type(entity.type);
            nx::expects(type.model.value < models.size(), "EntityModels: upload before drawing");
            const AnimatedModel& file = catalog.model(type.model);
            const nx::Vec3 origin = entity.origin + type.definition.modelOffset.vector();
            const nx::assets::MdlView& view = *file.files->view;
            if (frame.world.mayShow(
                    shownBoxOf(origin, std::array{view.sequences()[entity.sequence.value].bounds}))) {
                posings[first + count++] = Posing{
                    .entity = &entity, .drawn = &models[type.model.value], .file = &file, .origin = origin};
            }
        }
        written[batch] = {.first = first, .count = count};
    };
    if (frame.jobs != nullptr) {
        frame.jobs->forBatches({.label = "entities.find", .priority = nx::jobs::Priority::Critical},
                               entities.size(), kSmallestPosedBatch, findBatch);
    } else {
        findBatch(0, 0, entities.size());
    }
    std::size_t kept = 0;
    std::size_t boneCount = 0;
    for (const Written& part : written) {
        for (std::size_t index = part.first; index < part.first + part.count; ++index) {
            Posing& posing = posings[kept++] = posings[index];
            posing.firstBone = boneCount;
            boneCount += posing.file->files->view->bones().size();
        }
    }
    posings.resize(kept);
    const std::span<nx::Mat3x4> bones = posed.roomForBones(boneCount);
    poseAll(frame, {.label = "entities.pose", .priority = nx::jobs::Priority::Critical}, posings.size(),
            [this, bones, &frame](std::size_t index) {
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
