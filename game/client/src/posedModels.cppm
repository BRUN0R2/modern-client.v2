// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Studio models posed for a frame (docs/05-rendering.md, section 9): what the map's models and the other
/// players give the scene to draw.
export module cs.client:posedModels;

import std;
import nx.math;
import nx.jobs;
import nx.assets;
import nx.render;

export namespace cs {

/// Studio models posed for a frame: the bones of them all, which their instances point into. Kept from
/// frame to frame for its room.
struct PosedModels {
    std::vector<nx::Mat3x4> bones{};
    std::vector<nx::render::StudioInstance> instances{};

    inline void clear() noexcept {
        bones.clear();
        instances.clear();
    }
};

/// What a frame's models are posed in: the world, which tells what the frame may show, its light, the sky's
/// and the styles', and the jobs they are posed in, the calling thread alone for none.
struct ModelFrame {
    const nx::render::BspWorld& world;
    const nx::render::WorldLight& light;
    const nx::render::SkyLight& sky;
    const nx::render::LightStyleValues& styles;
    nx::jobs::JobSystem* jobs = nullptr;
};

/// How far past what its sequences' bounds say a model may show: what its parts hang out of them by, and a
/// weapon merged onto it.
inline constexpr float kShownMargin = 32.0F;
/// How far about its origin a model whose sequence names no bounds may show.
inline constexpr float kBoundlessReach = 128.0F;

/// The box a model at `origin` playing sequences of `bounds` may show in, however it turns about its height,
/// so that a frame poses and draws only what it may show (nx::render::BspWorld::mayShow).
[[nodiscard]] inline auto shownBoxOf(nx::Vec3 origin, std::span<const nx::Aabb> bounds) noexcept -> nx::Aabb {
    float reach = 0.0F;
    float lowest = 0.0F;
    float highest = 0.0F;
    bool isBounded = false;
    for (const nx::Aabb& box : bounds) {
        isBounded = isBounded || box.min != nx::Vec3{} || box.max != nx::Vec3{};
        reach = std::max(
            {reach, std::abs(box.min.x), std::abs(box.min.y), std::abs(box.max.x), std::abs(box.max.y)});
        lowest = std::min(lowest, box.min.z);
        highest = std::max(highest, box.max.z);
    }
    if (!isBounded) {
        reach = kBoundlessReach;
        lowest = -kBoundlessReach;
        highest = kBoundlessReach;
    }
    const float across = reach + kShownMargin;
    return nx::Aabb{.min = origin + nx::Vec3{.x = -across, .y = -across, .z = lowest - kShownMargin},
                    .max = origin + nx::Vec3{.x = across, .y = across, .z = highest + kShownMargin}};
}

/// The fewest models it is worth handing to a worker: a pose costs some tens of microseconds, waking one
/// about as much, so that a frame wakes as few as the models it poses need.
inline constexpr std::size_t kSmallestPosedBatch = 4;

/// Calls `pose` with each index below `count`, in jobs of `frame`, which the calling thread runs too while it
/// waits for them; on the calling thread alone for few, or none to run them. Each must write only its own.
template <typename Pose>
inline void poseAll(const ModelFrame& frame, const nx::jobs::JobDesc& desc, std::size_t count,
                    const Pose& pose) {
    const auto poseBatch = [&pose](std::size_t /*batch*/, std::size_t first, std::size_t last) {
        for (std::size_t index = first; index < last; ++index) {
            pose(index);
        }
    };
    if (frame.jobs == nullptr) {
        poseBatch(0, 0, count);
        return;
    }
    frame.jobs->forBatches(desc, count, kSmallestPosedBatch, poseBatch);
}

} // namespace cs
