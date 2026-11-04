// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.jobs;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :modelMaterials;

namespace cs {
namespace {
/// How many ticks a body's light holds, a tenth of a second at the classic rate: as often as a light style
/// changes.
constexpr std::uint64_t kTicksALightHolds = 10;
/// The fewest bodies it is worth handing to a worker to write for the GPU: little work each.
constexpr std::size_t kSmallestBodyBatch = 1024;
/// The fewest corpses it is worth handing to a worker to find whether a frame may show them.
constexpr std::size_t kSmallestFoundBatch = 256;
constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0F;

/// Makes `drawn`, of `file`, one a crowd of `renderer` may be of. Errors: the renderer's.
[[nodiscard]] auto loadCrowdOf(const AnimatedModel& file, const nx::render::StudioModel& drawn,
                               nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands)
    -> nx::Result<void, nx::render::StudioError> {
    const nx::assets::MdlView& view = *file.files->view;
    const Skeleton skeleton = skeletonOf(view);
    const std::span<const nx::assets::StudioSequence> sequences = view.sequences();
    std::vector<std::vector<nx::render::CrowdBonePose>> poses(sequences.size());
    std::vector<nx::render::CrowdSequence> played(sequences.size());
    for (std::size_t index = 0; index < sequences.size(); ++index) {
        const nx::assets::StudioSequence& sequence = sequences[index];
        poses[index] = framePosesOf(skeleton, sequence, file.animations[index]) |
                       std::views::transform([](const BoneFramePose& pose) {
                           return nx::render::CrowdBonePose{.position = nx::extend(pose.position, 0.0F),
                                                            .rotation = {.x = pose.rotation.x,
                                                                         .y = pose.rotation.y,
                                                                         .z = pose.rotation.z,
                                                                         .w = pose.rotation.w}};
                       }) |
                       std::ranges::to<std::vector>();
        const nx::Aabb shown = shownBoxOf({}, std::array{sequence.bounds});
        played[index] = {.poses = poses[index],
                         .frameCount = sequence.frameCount,
                         .fps = sequence.fps,
                         .isLooping = sequence.isLooping,
                         .reach = shown.max.x,
                         .lowest = shown.min.z,
                         .highest = shown.max.z};
    }
    const std::vector<std::int32_t> parents =
        view.bones() | std::views::transform([](const nx::assets::StudioBone& bone) {
            return bone.parent ? nx::narrow<std::int32_t>(bone.parent->value) : std::int32_t{-1};
        }) |
        std::ranges::to<std::vector>();
    return renderer.loadCrowdModel(commands, {.model = drawn, .parents = parents, .sequences = played});
}
} // namespace

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
        if (renderer.drawsCrowds()) {
            if (auto crowd = loadCrowdOf(file, models.back(), renderer, commands); !crowd) {
                return nx::fail(EntityModelError::UploadFailed, std::move(crowd.error()));
            }
        }
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
        frame.jobs != nullptr ? frame.jobs->batchCountOf(entities.size(), kSmallestFoundBatch) : 1);
    const auto findBatch = [&](std::size_t batch, std::size_t first, std::size_t last) {
        std::size_t count = 0;
        for (const ShownEntity& entity : entities.subspan(first, last - first)) {
            const EntityType& type = catalog.type(entity.type);
            nx::expects(type.model.value < models.size(), "EntityModels: upload before drawing");
            const AnimatedModel& file = catalog.model(type.model);
            const nx::Vec3 origin = entity.origin + modelOffsetOf(type.definition, entity.isDucked);
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
                               entities.size(), kSmallestFoundBatch, findBatch);
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

void EntityModels::writeBodies(const EntityCatalog& catalog, const ShownCrowd& crowd, const ModelFrame& frame,
                               nx::render::CrowdRoom room) {
    const std::span<const EntityState> states = crowd.states;
    const auto tick = static_cast<std::uint64_t>(crowd.lastTick.tick());
    const std::size_t batches =
        frame.jobs != nullptr ? frame.jobs->batchCountOf(states.size(), kSmallestBodyBatch) : 1;
    // Where each batch writes: how many live before it. The mapped memory is not to be read, nor closed up
    // after.
    struct Counted {
        std::size_t alive = 0;
        std::size_t firstBody = 0;
        std::uint32_t mostSlots = 0;
    };
    std::vector<Counted> counted(batches);
    const auto inBatches = [&frame](nx::jobs::JobLabel label, std::size_t count, const auto& batch) {
        if (frame.jobs != nullptr) {
            frame.jobs->forBatches({.label = label, .priority = nx::jobs::Priority::Critical}, count,
                                   kSmallestBodyBatch, batch);
        } else {
            batch(0, 0, count);
        }
    };
    inBatches("entities.count", states.size(), [&](std::size_t batch, std::size_t first, std::size_t last) {
        Counted& part = counted[batch];
        for (const EntityState& state : states.subspan(first, last - first)) {
            part.mostSlots = std::max(part.mostSlots, std::to_underlying(state.entity.index()) + 1);
            part.alive += state.health > nx::HitPoints{} ? 1U : 0U;
        }
    });
    std::size_t bodies = 0;
    std::uint32_t slots = 0;
    for (Counted& part : counted) {
        part.firstBody = bodies;
        bodies += part.alive;
        slots = std::max(slots, part.mostSlots);
    }
    litBodies.resize(std::max(litBodies.size(), std::size_t{slots}));
    std::vector<std::vector<std::uint32_t>> ofModel(batches,
                                                    std::vector<std::uint32_t>(room.bodiesOfModel.size()));
    inBatches("entities.write", states.size(), [&](std::size_t batch, std::size_t first, std::size_t last) {
        if (first == last) {
            return;
        }
        std::size_t place = counted[batch].firstBody;
        for (const EntityState& state : states.subspan(first, last - first)) {
            if (state.health <= nx::HitPoints{}) {
                continue; // a corpse: posed by the CPU, blended as it fades
            }
            const EntityType& type = catalog.type(state.type);
            const nx::Vec3 offset = modelOffsetOf(type.definition, state.isDucked);
            const nx::Vec3 origin = state.origin + offset;
            const auto slot = std::size_t{std::to_underlying(state.entity.index())};
            LitBody& lit = litBodies[slot];
            if (lit.entity != state.entity || (slot + tick) % kTicksALightHolds == 0) {
                lit = {.entity = state.entity,
                       .light = frame.light.modelLightAt({.origin = origin, .sky = frame.sky}, frame.styles)};
            }
            const float sharePerTick = 1.0F / static_cast<float>(state.ticksPerMove);
            room.bodies[place++] = {
                .previousOrigin = state.movedFrom + offset,
                .yaw = state.angles.yaw.value() * kRadiansPerDegree,
                .origin = origin,
                .model = type.model.value,
                .sequence = state.animation.sequence.value,
                .frame = state.animation.frame,
                .advancedAgo = crowd.lastTick.since(state.animation.advancedAt, crowd.rate).count(),
                .framerate = state.animation.framerate,
                .lightDirection = lit.light.direction,
                .ambient = lit.light.ambient,
                .lightColor = lit.light.color,
                .shade = lit.light.shade,
                .moveShare = static_cast<float>(crowd.lastTick.tick() - state.movedAt.tick()) * sharePerTick,
                .moveSharePerTick = sharePerTick};
            ++ofModel[batch][type.model.value];
        }
    });
    for (const std::vector<std::uint32_t>& counts : ofModel) {
        for (std::size_t model = 0; model < counts.size(); ++model) {
            room.bodiesOfModel[model] += counts[model];
        }
    }
}
} // namespace cs
