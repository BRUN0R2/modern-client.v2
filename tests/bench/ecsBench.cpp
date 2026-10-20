// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The ECS operations with performance targets (docs/03-ecs.md, section 16): iteration, command
// buffers and scheduler overhead. The targets hold on the reference hardware, still to be defined.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.jobs;
import nx.ecs;

namespace {

using nx::ecs::Commands;
using nx::ecs::Entity;
using nx::ecs::Phase;
using nx::ecs::Query;
using nx::ecs::Schedule;
using nx::ecs::World;

struct Position {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Velocity {
    float dx = 0.0F;
    float dy = 0.0F;
    float dz = 0.0F;
};

constexpr std::size_t kIteratedEntities = 100'000; // target: < 0.2 ms on one thread
constexpr std::size_t kCommandEntities = 10'000;   // target: < 1 ms to create and destroy
constexpr std::size_t kEmptySystems = 256;         // target: < 2 µs per system
constexpr float kTickSeconds = 0.01F;

void registerComponents(World& world) {
    const bool registered = world.registerComponent<Position>("bench::Position").has_value() &&
                            world.registerComponent<Velocity>("bench::Velocity").has_value();
    nx::invariant(registered, "ecsBench: the components register in a fresh world");
}

/// One thread moves 100,000 entities with two components.
void iterateTwoComponents(benchmark::State& state) {
    World world;
    registerComponents(world);
    for (std::size_t index = 0; index < kIteratedEntities; ++index) {
        std::ignore = world.spawn(Position{}, Velocity{.dx = 1.0F, .dy = 2.0F, .dz = 3.0F});
    }
    Query<Position&, const Velocity&> moving{world};
    for ([[maybe_unused]] auto iteration : state) {
        moving.forEach([](Position& position, const Velocity& velocity) {
            position.x += velocity.dx * kTickSeconds;
            position.y += velocity.dy * kTickSeconds;
            position.z += velocity.dz * kTickSeconds;
        });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kIteratedEntities));
}
BENCHMARK(iterateTwoComponents)->Unit(benchmark::kMicrosecond);

/// 10,000 entities spawned and then destroyed through command buffers.
void spawnAndDestroyThroughCommands(benchmark::State& state) {
    World world;
    registerComponents(world);
    std::vector<Entity> spawned;
    spawned.reserve(kCommandEntities);
    for ([[maybe_unused]] auto iteration : state) {
        Commands spawning{world};
        for (std::size_t index = 0; index < kCommandEntities; ++index) {
            std::ignore = spawning.spawn(Position{}, Velocity{});
        }
        std::ignore = spawning.apply(world);

        spawned.clear();
        Query<Entity>{world}.forEach([&spawned](Entity entity) { spawned.push_back(entity); });
        Commands destroying{world};
        for (const Entity entity : spawned) {
            destroying.destroy(entity);
        }
        std::ignore = destroying.apply(world);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kCommandEntities));
}
BENCHMARK(spawnAndDestroyThroughCommands)->Unit(benchmark::kMicrosecond);

/// A phase of empty systems: what the scheduler costs per system. The counter is the time per system.
void emptySystems(benchmark::State& state) {
    auto jobs = nx::jobs::JobSystem::create({.workerCount = static_cast<std::uint32_t>(state.range(0))});
    nx::invariant(jobs.has_value(), "ecsBench: the job system starts");
    World world;
    Schedule schedule{world};
    for (std::size_t index = 0; index < kEmptySystems; ++index) {
        std::ignore = schedule.add({.name = "bench.empty"}, [] {});
    }
    nx::invariant(schedule.build().has_value(), "ecsBench: empty systems have no ordering conflicts");
    for ([[maybe_unused]] auto iteration : state) {
        std::ignore = schedule.run(Phase::Simulation, **jobs);
    }
    state.counters["perSystem"] =
        benchmark::Counter(static_cast<double>(kEmptySystems),
                           benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(emptySystems)->ArgName("workers")->Arg(0)->Arg(3)->UseRealTime()->Unit(benchmark::kMicrosecond);

} // namespace
