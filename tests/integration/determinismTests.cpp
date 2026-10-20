// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The determinism rule of the M1 runtime (docs/04-job-system.md, section 8, and
// docs/07-simulation-and-physics.md, section 6): a world moved by nx.math inside parallel ECS systems
// reaches the same bits, tick after tick, with 0, 1, 3 and 15 workers.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;

namespace {

using nx::Aabb;
using nx::EulerAngles;
using nx::Quat;
using nx::Vec3;
using nx::ecs::Entity;
using nx::ecs::Phase;
using nx::ecs::Query;
using nx::ecs::Schedule;
using nx::ecs::World;

struct Body {
    Vec3 origin{};
    Vec3 velocity{};
};

struct Heading {
    EulerAngles angles{};
    Quat rotation{};
};

constexpr std::array<std::uint32_t, 4> kWorkerCounts{0, 1, 3, 15};
constexpr int kTicks = 60;
constexpr int kBodyCount = 3000; // several chunks, so the systems really run in parallel
constexpr float kTickSeconds = 0.01F;
constexpr float kCruiseSpeed = 250.0F;
constexpr float kGravity = 800.0F;
constexpr float kSteering = 0.1F;
constexpr float kTurnRate = 3.7F;
constexpr float kBounce = 0.5F;
constexpr nx::Plane kFloor{.normal = Vec3{.z = 1}, .distance = 0};
constexpr Vec3 kHalfHull{.x = 16, .y = 16, .z = 36};

[[nodiscard]] auto makeJobs(std::uint32_t workerCount) -> std::unique_ptr<nx::jobs::JobSystem> {
    auto created = nx::jobs::JobSystem::create({.workerCount = workerCount});
    return created.has_value() ? std::move(*created) : nullptr;
}

void spawnBodies(World& world) {
    for (int index = 0; index < kBodyCount; ++index) {
        const auto step = static_cast<float>(index);
        const EulerAngles angles{.pitch = nx::Degrees{static_cast<float>(index % 61) - 30},
                                 .yaw = nx::Degrees{step * 7.3F},
                                 .roll = nx::Degrees{static_cast<float>(index % 7)}};
        std::ignore = world.spawn(
            Body{.origin = Vec3{.x = step * 3, .y = -step * 2, .z = 50 + static_cast<float>(index % 9)}},
            Heading{.angles = angles, .rotation = Quat::fromEulerAngles(angles)});
    }
}

/// Turns every body, eases its rotation and steers its velocity towards where it faces.
void steerBodies(std::span<Heading> headings, std::span<Body> bodies) {
    for (std::size_t row = 0; row < bodies.size(); ++row) {
        Heading& heading = headings[row];
        heading.angles.yaw = nx::normalizeAngle(heading.angles.yaw + nx::Degrees{kTurnRate});
        heading.rotation = nx::slerp(heading.rotation, Quat::fromEulerAngles(heading.angles), kSteering);
        const Vec3 forward = nx::rotate(heading.rotation, nx::angleVectors(heading.angles).forward);
        bodies[row].velocity = nx::lerp(bodies[row].velocity, forward * kCruiseSpeed, kSteering);
    }
}

/// Falls, moves and bounces off the floor with a standing hull.
void moveBodies(std::span<Body> bodies) {
    for (Body& body : bodies) {
        body.velocity.z -= kGravity * kTickSeconds;
        body.origin += body.velocity * kTickSeconds;
        if (kFloor.sideOf(Aabb::around(body.origin, kHalfHull)) != nx::PlaneSide::Front) {
            body.origin.z = kHalfHull.z;
            body.velocity.z = -body.velocity.z * kBounce;
        }
    }
}

/// Hash of the bits of every body after each tick.
[[nodiscard]] auto simulate(std::uint32_t workerCount) -> std::vector<std::uint64_t> {
    auto jobs = makeJobs(workerCount);
    EXPECT_NE(jobs, nullptr);
    World world;
    EXPECT_TRUE(world.registerComponent<Body>("test::Body").has_value());
    EXPECT_TRUE(world.registerComponent<Heading>("test::Heading").has_value());
    spawnBodies(world);

    Schedule schedule{world};
    std::ignore =
        schedule.add({.name = "steer"}, [](Query<Heading&, Body&>& bodies, nx::jobs::JobSystem& jobSystem) {
            bodies.parallelForEachChunk(jobSystem, steerBodies);
        });
    std::ignore = schedule.add({.name = "move"}, [](Query<Body&>& bodies, nx::jobs::JobSystem& jobSystem) {
        bodies.parallelForEachChunk(jobSystem, moveBodies);
    });
    EXPECT_TRUE(schedule.build().has_value());

    std::vector<std::uint64_t> hashes;
    for (int tick = 0; tick < kTicks; ++tick) {
        std::ignore = schedule.run(Phase::Simulation, *jobs);
        std::uint64_t hash = 0;
        Query<Entity, const Body&, const Heading&>{world}.forEach(
            [&hash](Entity entity, const Body& body, const Heading& heading) {
                const std::array index{std::to_underlying(entity.index())};
                hash = nx::hash64(std::as_bytes(std::span{index}), hash);
                hash = nx::hash64(std::as_bytes(std::span{&body, 1}), hash);
                hash = nx::hash64(std::as_bytes(std::span{&heading, 1}), hash);
            });
        hashes.push_back(hash);
    }
    return hashes;
}

TEST(DeterminismTest, MathInParallelSystemsDoesNotDependOnTheWorkerCount) {
    static_assert(sizeof(Body) == 6 * sizeof(float) && sizeof(Heading) == 7 * sizeof(float),
                  "the hash reads the components as bytes: no padding allowed");
    const std::vector<std::uint64_t> reference = simulate(kWorkerCounts.front());
    ASSERT_EQ(reference.size(), std::size_t{kTicks});
    for (const std::uint32_t workerCount : std::span{kWorkerCounts}.subspan(1)) {
        const std::vector<std::uint64_t> hashes = simulate(workerCount);
        const auto mismatch = std::ranges::mismatch(hashes, reference);
        EXPECT_EQ(mismatch.in1, hashes.end())
            << workerCount << " workers diverge at tick " << std::distance(hashes.begin(), mismatch.in1);
    }
}

} // namespace
