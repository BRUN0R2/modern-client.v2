// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The players' bodies among the entities of the server's world (docs/07-simulation-and-physics.md,
/// section 5.3), as the original's engine traces a client's (SV_HullForStudioModel with sv_clienttrace
/// at 1, R_StudioPlayerBlend, R_StudioHull, and the game's SV_StudioSetupBones; ReHLDS and ReGameDLL_CS,
/// GPL, consulted: docs/20 section 3): solid as its box to a hull, standing or ducked; to a line, its
/// model's hitboxes but the last, a shield's, which a player without one has none of, each grown half a
/// unit each way, its bones posed as its model plays, the hit group of the one met the trace's.
export module cs.server:playerBodies;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

export namespace cs {

/// How a player's model plays where the server's traces meet it, as its entity holds it: the model,
/// none when it could not be read (its box alone then); its torso's sequence (pev->sequence) at its frame
/// (pev->frame, of kFrameScale); its legs' gait sequence (pev->gaitsequence) and the way they face
/// (m_flGaityaw, degrees); the aim's blends (m_flYaw, m_flPitch); and how its body is turned (pev->angles:
/// the pitch a third of the view's, negated).
struct PlayerPose {
    const PlayedModel* model = nullptr;
    nx::assets::SequenceIndex sequence{};
    float frame = 0.0F;
    nx::assets::SequenceIndex gaitSequence{};
    float gaitYaw = 0.0F;
    float yawBlend = 0.0F;
    float pitchBlend = 0.0F;
    nx::EulerAngles angles{};
};

/// A player's body: which player it is, and how its model plays.
struct PlayerBody {
    PlayerIndex player{};
    PlayerPose pose{};
};

/// Registers PlayerBody in `world`, made ready by nx::game::registerComponents. Errors: those of
/// registering a component.
[[nodiscard]] auto registerPlayerBodies(nx::ecs::World& world) -> nx::Result<void, nx::ecs::ComponentError>;

/// Makes `body` of `world`, a player's, solid as `box` about its origin to the server's traces, a line
/// meeting its hitboxes (its ClipMethod); not solid at all for none, as a dead player is (SOLID_NOT).
void setBodySolidity(nx::ecs::World& world, nx::ecs::Entity body, std::optional<nx::Aabb> box);

/// The first blending of a player's sequence for the server's traces, 0 to 255, and the pitch its model
/// is turned by then (R_StudioPlayerBlend's).
struct PlayerBlend {
    std::uint8_t blending = 0;
    float pitch = 0.0F;
};

/// The PlayerBlend of a player's `sequence` when its body's pitch is `pitch`, as R_StudioPlayerBlend
/// weighs it: the pitch times 3, cut to a whole number, against the sequence's first blend's start and
/// end; below its start, the first blending, and the pitch less a third of the start; up to its end, the
/// share of the way from the start, cut to a whole one of 255, or the middle's for a way shorter than a
/// tenth, and no pitch; beyond, the last, and the pitch less a third of the end.
[[nodiscard]] auto playerBlendOf(const nx::assets::StudioSequence& sequence, float pitch) noexcept
    -> PlayerBlend;

/// The bones of a player's model posed as `pose` says at `origin` for the server's traces
/// (SV_HullForStudioModel of a client): its sequence at the frame the server estimates (serverFrameOf),
/// its motion kept, the aim's blends, its first blending playerBlendOf's, its legs at the first frame of
/// their gait sequence (posePlayerBones); turned by playerBlendOf's pitch negated (R_StudioHull), the
/// gait's yaw, a turn added when it is negative, and the body's roll. `pose` has a model.
[[nodiscard]] auto tracedBonesOf(const PlayerPose& pose, nx::Vec3 origin) -> std::vector<nx::Mat3x4>;

} // namespace cs
