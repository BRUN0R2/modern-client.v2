// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// How far each face of a player's hitboxes moves out along each of the world's axes for a line's trace:
/// sv_clienttrace's 1 times half (SV_HullForStudioModel's size and factor).
constexpr float kClientTraceGrowth = 0.5F;
/// R_StudioPlayerBlend weighs the body's pitch times this against a blend's start and end, and takes a
/// third of the start or the end off the pitch beyond them.
constexpr double kPitchScale = 3.0;
constexpr double kBlendingTop = 255.0;
constexpr std::uint8_t kMiddleBlending = 127;
constexpr std::uint8_t kLastBlending = 255;
constexpr double kShortestBlendWay = 0.1;
constexpr float kDegreesPerTurn = 360.0F;

/// The trace of a box `traced` from `start` to `end` through the player's body `entity` of `world` (its
/// ClipMethod), as SV_HullForStudioModel chooses for a client: a line through its hitboxes but the last
/// (a shield's), grown kClientTraceGrowth, its bones posed as tracedBonesOf poses them, the trace's hit
/// group the one's it met, all in solid and not stopped for a model without any other; a box, or a body
/// whose model could not be read, through its box grown by the one traced.
[[nodiscard]] auto clipPlayer(nx::ecs::World& world, nx::ecs::Entity entity, const nx::Aabb& traced,
                              nx::Vec3 start, nx::Vec3 end) -> nx::collision::Trace {
    const nx::Vec3 origin = world.get<nx::game::Transform>(entity)->origin;
    const nx::Aabb& solid = world.get<nx::game::BoxSolidity>(entity)->box;
    PlayerBody& body = *world.get<PlayerBody>(entity);
    const PlayerPose& pose = body.pose;
    if (traced.max != traced.min || pose.model == nullptr) {
        return nx::collision::traceBox({.min = solid.min - traced.max, .max = solid.max - traced.min}, origin,
                                       start, end);
    }
    // A player without a shield has none of the last hitbox, its shield's (pev->gamestate 1).
    const std::span<const nx::assets::Hitbox> modelHitboxes = pose.model->view->hitboxes();
    if (modelHitboxes.size() <= 1) {
        return nx::collision::Trace{.fraction = 1.0F, .end = end, .isAllSolid = true};
    }
    const std::span<const nx::assets::Hitbox> hitboxes = modelHitboxes.first(modelHitboxes.size() - 1);
    if (!body.placed || body.placed->pose != pose || body.placed->origin != origin) {
        const nx::Vec3 grownBy{.x = kClientTraceGrowth, .y = kClientTraceGrowth, .z = kClientTraceGrowth};
        body.placed =
            PlacedPlayerHitboxes{.pose = pose,
                                 .origin = origin,
                                 .boxes = placedHitboxesOf(hitboxes, tracedBonesOf(pose, origin), grownBy)};
    }
    const nx::collision::HitboxTrace hit = nx::collision::traceHitboxes(body.placed->boxes, start, end);
    nx::collision::Trace met = hit.trace;
    met.hitGroup = hitboxes[hit.hitbox].group;
    return met;
}

} // namespace

auto registerPlayerBodies(nx::ecs::World& world) -> nx::Result<void, nx::ecs::ComponentError> {
    if (auto registered = world.registerComponent<PlayerBody>("cs::PlayerBody"); !registered) {
        return std::unexpected{registered.error()};
    }
    return {};
}

void setBodySolidity(nx::ecs::World& world, nx::ecs::Entity body, std::optional<nx::Aabb> box) {
    if (!box) {
        world.remove<nx::game::BoxSolidity>(body);
        world.remove<nx::game::ClipMethod>(body);
        return;
    }
    if (nx::game::BoxSolidity* const solidity = world.get<nx::game::BoxSolidity>(body)) {
        solidity->box = *box;
        return;
    }
    world.add(body, nx::game::BoxSolidity{.box = *box});
    world.add(body, nx::game::ClipMethod{.clip = &clipPlayer});
}

auto playerBlendOf(const nx::assets::StudioSequence& sequence, float pitch) noexcept -> PlayerBlend {
    const float start = sequence.blendStarts[0];
    const float end = sequence.blendEnds[0];
    // (int)(*pPitch * 3.0): in double, cut to a whole number; weighed against the start and end in float.
    const auto blend = static_cast<std::int32_t>(static_cast<double>(pitch) * kPitchScale);
    if (static_cast<float>(blend) < start) {
        return PlayerBlend{.blending = 0,
                           .pitch = pitch - static_cast<float>(static_cast<double>(start) / kPitchScale)};
    }
    if (static_cast<float>(blend) <= end) {
        const float way = end - start;
        if (static_cast<double>(way) < kShortestBlendWay) {
            return PlayerBlend{.blending = kMiddleBlending};
        }
        const double share =
            static_cast<double>(static_cast<float>(blend) - start) * kBlendingTop / static_cast<double>(way);
        return PlayerBlend{.blending = static_cast<std::uint8_t>(static_cast<std::int32_t>(share))};
    }
    return PlayerBlend{
        .blending = kLastBlending,
        .pitch = static_cast<float>(static_cast<double>(pitch) - (static_cast<double>(end) / kPitchScale))};
}

auto tracedBonesOf(const PlayerPose& pose, nx::Vec3 origin) -> std::vector<nx::Mat3x4> {
    nx::expects(pose.model != nullptr, "tracedBonesOf: a pose of a model");
    const nx::assets::MdlView& model = *pose.model->view;
    const std::span<const nx::assets::StudioSequence> sequences = model.sequences();
    // As SV_StudioSetupBones bounds it: a sequence the model lacks is its first.
    const nx::assets::StudioSequence& sequence =
        sequences[pose.sequence.value < sequences.size() ? pose.sequence.value : 0];
    const PlayerBlend blend = playerBlendOf(sequence, pose.angles.pitch.value());
    const float yaw = pose.gaitYaw < 0.0F ? pose.gaitYaw + kDegreesPerTurn : pose.gaitYaw;
    const nx::EulerAngles turn{
        .pitch = nx::Degrees{-blend.pitch}, .yaw = nx::Degrees{yaw}, .roll = pose.angles.roll};
    return posePlayerBones(model, pose.model->animations, pose.sequence,
                           PlayerPoseQuery{.frame = serverFrameOf(sequence, pose.frame),
                                           .gaitSequence = pose.gaitSequence,
                                           .yawBlend = pose.yawBlend,
                                           .pitchBlend = pose.pitchBlend,
                                           .blending = blend.blending,
                                           .placement = nx::affine(nx::rotationMatrix(turn), origin)});
}

} // namespace cs
