// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;
import std;
import nx.core;
import nx.math;
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

auto EntityModels::posed(const EntityCatalog& catalog, std::span<const ShownEntity> entities,
                         const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                         const nx::render::LightStyleValues& styles) const -> PosedModels {
    PosedModels posed;
    posed.bones.reserve(entities.size());
    posed.instances.reserve(entities.size());
    for (const ShownEntity& entity : entities) {
        const EntityType& type = catalog.type(entity.type);
        nx::expects(type.model.value < models.size(), "EntityModels: upload before drawing");
        const AnimatedModel& file = catalog.model(type.model);
        const nx::Vec3 origin = entity.origin + type.definition.modelOffset.vector();
        const nx::assets::StudioSequence& sequence = file.files->view->sequences()[entity.sequence.value];
        posed.bones.push_back(poseBones(
            skeletonOf(*file.files->view), sequence, file.animations[entity.sequence.value],
            {.frame = entity.frame, .placement = nx::affine(nx::rotationMatrix(entity.angles), origin)}));
        posed.instances.push_back({.model = models[type.model.value],
                                   .bones = posed.bones.back(),
                                   .light = light.modelLightAt({.origin = origin, .sky = sky}, styles),
                                   .origin = origin});
    }
    return posed;
}
} // namespace cs
