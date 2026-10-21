// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.data;
import nx.ecs;
import nx.assets;

namespace nx::game {

auto readEntityClasses(std::string_view text, std::string_view sourceName)
    -> Result<EntityClasses, GameError> {
    auto classes = data::parseToml<EntityClasses>(text, sourceName);
    if (!classes) {
        return fail(GameError::ConfigInvalid, std::string{sourceName}, std::move(classes.error()));
    }
    std::set<std::string_view> seen;
    for (const BrushClass& brushClass : classes->brushClasses) {
        if (!seen.insert(brushClass.classname).second) {
            return fail(GameError::ConfigInvalid,
                        std::format("{}: the brush class {:?} twice", sourceName, brushClass.classname));
        }
    }
    return std::move(*classes);
}

auto registerBrushComponents(ecs::World& world) -> Result<void, ecs::ComponentError> {
    if (auto registered = world.registerComponent<BrushModel>("nx::game::BrushModel"); !registered) {
        return std::unexpected{std::move(registered.error())};
    }
    if (auto registered = world.registerComponent<Transform>("nx::game::Transform"); !registered) {
        return std::unexpected{std::move(registered.error())};
    }
    if (auto registered = world.registerComponent<BrushSolidity>("nx::game::BrushSolidity"); !registered) {
        return std::unexpected{std::move(registered.error())};
    }
    return {};
}

auto spawnBrushEntities(ecs::World& world, std::span<const assets::BspEntity> entities,
                        std::size_t modelCount, const EntityClasses& classes)
    -> Result<std::size_t, GameError> {
    std::size_t spawned = 0;
    for (const assets::BspEntity& entity : entities) {
        const std::optional<std::uint32_t> model = entity.findBrushModel();
        const std::string_view classname = entity.find("classname").value_or("");
        const auto brushClass = std::ranges::find(classes.brushClasses, classname, &BrushClass::classname);
        if (!model || brushClass == classes.brushClasses.end()) {
            continue;
        }
        if (*model == 0 || *model >= modelCount) {
            return fail(GameError::UnknownModel,
                        std::format("{} \"*{}\" of {} brush models", classname, *model, modelCount));
        }
        const std::int32_t contents = brushClass->contents.value_or(entity.findInteger("skin").value_or(0));
        const std::int32_t flags = entity.findInteger("spawnflags").value_or(0);
        const bool isNotSolid = (flags & brushClass->notSolidFlags.value_or(0)) != 0 ||
                                (brushClass->skinMakesNotSolid.value_or(false) && contents != 0);
        world.spawn(
            BrushModel{.index = *model}, Transform{.origin = entity.findVector("origin").value_or(Vec3{})},
            BrushSolidity{.solid = isNotSolid ? Solid::Not : brushClass->solid, .contents = contents});
        ++spawned;
    }
    return spawned;
}

} // namespace nx::game
