// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;
import std;
import nx.core;
import nx.math;
import nx.assets;

namespace cs {
namespace {
[[nodiscard]] auto validate(const EntityDefinition& definition) -> nx::Result<void, EntityError> {
    if (definition.id.empty() || definition.model.empty() || !nx::assets::VfsPath::parse(definition.model) ||
        !std::isfinite(definition.modelOffset.x.value()) ||
        !std::isfinite(definition.modelOffset.y.value()) ||
        !std::isfinite(definition.modelOffset.z.value())) {
        return nx::fail(EntityError::InvalidDefinition,
                        "an id, a VFS model path and a finite model offset are required");
    }
    const auto positive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    if (!positive(definition.health.value()) || !positive(definition.speed.value()) ||
        !positive(definition.meleeDamage.value()) || !positive(definition.meleeReach.value()) ||
        !positive(definition.meleeInterval.count())) {
        return nx::fail(EntityError::InvalidDefinition,
                        "health, speed, melee damage, reach and interval must be finite and positive");
    }
    for (const DamageShare share :
         {definition.hitGroups.generic, definition.hitGroups.head, definition.hitGroups.chest,
          definition.hitGroups.stomach, definition.hitGroups.arm, definition.hitGroups.leg,
          definition.hitGroups.shield, definition.hitGroups.other}) {
        if (!std::isfinite(share.value()) || share < DamageShare{}) {
            return nx::fail(EntityError::InvalidDefinition,
                            "hit-group shares must be finite and nonnegative");
        }
    }
    return {};
}
} // namespace

auto EntityCatalog::add(const nx::assets::Vfs& vfs, EntityDefinition definition, ModelTextures textures)
    -> nx::Result<EntityTypeIndex, EntityError> {
    if (auto checked = validate(definition); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (std::ranges::any_of(
            types, [&definition](const EntityType& type) { return type.definition.id == definition.id; })) {
        return nx::fail(EntityError::InvalidDefinition,
                        std::format("entity id {:?} already exists", definition.id));
    }
    auto file = std::ranges::find_if(models, [&definition, textures](const AnimatedModel& model) {
        return model.files->path == definition.model &&
               (textures == ModelTextures::Skipped || !model.files->view->textures().empty() ||
                model.files->textures.has_value());
    });
    std::optional<AnimatedModel> newModel;
    if (file == models.end()) {
        auto read = readAnimatedModel(vfs, definition.model, textures);
        if (!read) {
            return nx::fail(EntityError::ModelUnreadable, std::move(read.error()));
        }
        newModel.emplace(std::move(*read));
    }
    const AnimatedModel& animated = newModel ? *newModel : *file;
    const nx::assets::MdlView& view = *animated.files->view;
    if (view.hitboxes().empty()) {
        return nx::fail(EntityError::InvalidDefinition,
                        std::format("{}: an entity model needs hitboxes", definition.model));
    }
    EntitySequences sequences;
    const std::array requested{std::pair{&definition.idleSequence, &sequences.idle},
                               std::pair{&definition.walkSequence, &sequences.walk},
                               std::pair{&definition.attackSequence, &sequences.attack},
                               std::pair{&definition.deathSequence, &sequences.death}};
    for (const auto& [name, destination] : requested) {
        const auto found = std::ranges::find(view.sequences(), *name, &nx::assets::StudioSequence::label);
        if (name->empty() || found == view.sequences().end()) {
            return nx::fail(EntityError::UnknownSequence,
                            std::format("{}: no sequence {:?}", definition.model, *name));
        }
        *destination = nx::assets::SequenceIndex{nx::narrow<std::uint32_t>(found - view.sequences().begin())};
    }
    if (view.sequences()[sequences.death.value].isLooping) {
        return nx::fail(EntityError::InvalidDefinition, "a death sequence must not loop");
    }
    const EntityModelIndex modelIndex{nx::narrow<std::uint32_t>(
        newModel ? models.size() : static_cast<std::size_t>(file - models.begin()))};
    if (newModel) {
        models.push_back(std::move(*newModel));
    }
    const EntityTypeIndex index{nx::narrow<std::uint32_t>(types.size())};
    types.push_back({.definition = std::move(definition), .model = modelIndex, .sequences = sequences});
    return index;
}

auto EntityCatalog::type(EntityTypeIndex index) const -> const EntityType& {
    nx::expects(index.value < types.size(), "EntityCatalog: an issued type index");
    return types.at(index.value);
}

auto EntityCatalog::model(EntityModelIndex index) const -> const AnimatedModel& {
    nx::expects(index.value < models.size(), "EntityCatalog: an issued model index");
    return models.at(index.value);
}
} // namespace cs
