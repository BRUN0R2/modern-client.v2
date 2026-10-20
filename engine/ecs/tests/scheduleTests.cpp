// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;
import nx.ecs;

#include "contractRecorder.hpp"

namespace {

using nx::ecs::Changed;
using nx::ecs::Commands;
using nx::ecs::Entity;
using nx::ecs::EventReader;
using nx::ecs::EventWriter;
using nx::ecs::Phase;
using nx::ecs::Query;
using nx::ecs::Res;
using nx::ecs::Schedule;
using nx::ecs::ScheduleError;
using nx::ecs::SystemId;
using nx::ecs::World;

struct Position {
    float x = 0;
    float y = 0;
};

struct Velocity {
    float dx = 0;
    float dy = 0;
};

struct Health {
    float points = 0;
};

/// Resource the test systems append their names to.
struct RunLog {
    std::vector<std::string_view> entries;
};

struct Control {
    bool isMoving = true;
};

struct Missing {};

struct Hit {
    Entity target;
};

[[nodiscard]] auto makeJobs(std::uint32_t workerCount) -> std::unique_ptr<nx::jobs::JobSystem> {
    auto created = nx::jobs::JobSystem::create({.workerCount = workerCount});
    return created.has_value() ? std::move(*created) : nullptr;
}

void registerComponents(World& world) {
    EXPECT_TRUE(world.registerComponent<Position>("test::Position").has_value());
    EXPECT_TRUE(world.registerComponent<Velocity>("test::Velocity").has_value());
    EXPECT_TRUE(world.registerComponent<Health>("test::Health").has_value());
}

class ScheduleTest : public testing::Test {
protected:
    ScheduleTest() {
        registerComponents(world);
        world.insertResource(RunLog{});
        world.insertResource(Control{});
    }

    [[nodiscard]] auto log() -> std::vector<std::string_view>& { return world.resource<RunLog>()->entries; }

    World world;
    Schedule schedule{world};
    std::unique_ptr<nx::jobs::JobSystem> jobs = makeJobs(3);
};

TEST_F(ScheduleTest, ConflictingSystemsRunInDeclarationOrder) {
    std::ignore = schedule.add({.name = "first"}, [](Res<RunLog> log) { log->entries.push_back("first"); });
    std::ignore = schedule.add({.name = "second"}, [](Res<RunLog> log) { log->entries.push_back("second"); });
    std::ignore = schedule.add({.name = "third"}, [](Res<RunLog> log) { log->entries.push_back("third"); });
    ASSERT_TRUE(schedule.build().has_value());

    constexpr int kRuns = 50;
    for (int run = 0; run < kRuns; ++run) {
        log().clear();
        std::ignore = schedule.run(Phase::Simulation, *jobs);
        ASSERT_EQ(log(), (std::vector<std::string_view>{"first", "second", "third"}));
    }
}

TEST_F(ScheduleTest, BeforeAndAfterOverrideDeclarationOrder) {
    const SystemId late =
        schedule.add({.name = "late"}, [](Res<RunLog> log) { log->entries.push_back("late"); });
    const std::array beforeLate{late};
    const SystemId early = schedule.add({.name = "early", .before = beforeLate},
                                        [](Res<RunLog> log) { log->entries.push_back("early"); });
    const std::array afterEarly{early};
    std::ignore = schedule.add({.name = "middle", .after = afterEarly, .before = beforeLate},
                               [](Res<RunLog> log) { log->entries.push_back("middle"); });
    ASSERT_TRUE(schedule.build().has_value());

    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(log(), (std::vector<std::string_view>{"early", "middle", "late"}));
}

TEST_F(ScheduleTest, OnlyConflictingSystemsDependOnEachOther) {
    const SystemId movePositions = schedule.add({.name = "movePositions"}, [](Query<Position&>&) {});
    const SystemId healEveryone = schedule.add({.name = "healEveryone"}, [](Query<Health&>&) {});
    const SystemId readPositions = schedule.add({.name = "readPositions"}, [](Query<const Position&>&) {});
    const SystemId readTwice = schedule.add({.name = "readTwice"}, [](Query<const Position&>&) {});
    ASSERT_TRUE(schedule.build().has_value());

    EXPECT_TRUE(schedule.dependenciesOf(movePositions).empty());
    EXPECT_TRUE(schedule.dependenciesOf(healEveryone).empty()); // writes something else
    EXPECT_EQ(std::vector(std::from_range, schedule.dependenciesOf(readPositions)),
              std::vector{movePositions});
    EXPECT_EQ(std::vector(std::from_range, schedule.dependenciesOf(readTwice)), std::vector{movePositions});
    EXPECT_EQ(schedule.nameOf(readTwice), "readTwice");
}

TEST_F(ScheduleTest, AnExclusiveSystemRunsAlone) {
    const SystemId reader = schedule.add({.name = "reader"}, [](Query<const Position&>&) {});
    const SystemId exclusive =
        schedule.add({.name = "exclusive"}, [](World& target) { std::ignore = target.spawn(Position{}); });
    const SystemId other = schedule.add({.name = "other"}, [](Query<Health&>&) {});
    ASSERT_TRUE(schedule.build().has_value());

    EXPECT_EQ(std::vector(std::from_range, schedule.dependenciesOf(exclusive)), std::vector{reader});
    EXPECT_EQ(std::vector(std::from_range, schedule.dependenciesOf(other)), std::vector{exclusive});

    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(world.entityCount(), 1U);
}

TEST_F(ScheduleTest, ACycleIsABuildError) {
    const SystemId first = schedule.add({.name = "alpha"}, [] {});
    const std::array around{first};
    std::ignore = schedule.add({.name = "beta", .after = around, .before = around}, [] {});

    const auto built = schedule.build();
    ASSERT_FALSE(built.has_value());
    EXPECT_EQ(built.error().code, ScheduleError::Cycle);
    EXPECT_NE(built.error().detail.find("alpha -> beta -> alpha"), std::string::npos) << built.error().detail;
}

TEST_F(ScheduleTest, ConstraintsMustNameKnownSystems) {
    const std::array unknown{SystemId{42}};
    std::ignore = schedule.add({.name = "lonely", .after = unknown}, [] {});
    const auto built = schedule.build();
    ASSERT_FALSE(built.has_value());
    EXPECT_EQ(built.error().code, ScheduleError::UnknownSystem);
}

TEST_F(ScheduleTest, ConstraintsMustAgreeWithThePhaseOrder) {
    const SystemId simulation = schedule.add({.name = "simulation", .phase = Phase::Simulation}, [] {});
    const std::array afterSimulation{simulation};
    std::ignore = schedule.add({.name = "input", .phase = Phase::Input, .after = afterSimulation}, [] {});
    const auto built = schedule.build();
    ASSERT_FALSE(built.has_value());
    EXPECT_EQ(built.error().code, ScheduleError::OrderAcrossPhases);
}

TEST_F(ScheduleTest, RunsOnlyTheRequestedPhase) {
    std::ignore = schedule.add({.name = "input", .phase = Phase::Input},
                               [](Res<RunLog> log) { log->entries.push_back("input"); });
    std::ignore = schedule.add({.name = "rules", .phase = Phase::Rules},
                               [](Res<RunLog> log) { log->entries.push_back("rules"); });
    ASSERT_TRUE(schedule.build().has_value());

    std::ignore = schedule.run(Phase::Rules, *jobs);
    EXPECT_EQ(log(), std::vector<std::string_view>{"rules"});
}

TEST_F(ScheduleTest, CommandBuffersApplyAtTheEndOfThePhaseInDeclarationOrder) {
    std::size_t seenDuringPhase = 0;
    std::ignore = schedule.add({.name = "spawnTwo"}, [](Commands& commands) {
        std::ignore = commands.spawn(Health{.points = 1});
        std::ignore = commands.spawn(Health{.points = 2});
    });
    std::ignore = schedule.add({.name = "spawnOne"},
                               [](Commands& commands) { std::ignore = commands.spawn(Health{.points = 3}); });
    std::ignore =
        schedule.add({.name = "count", .phase = Phase::Simulation},
                     [&seenDuringPhase](Query<const Health&>& health) { seenDuringPhase = health.count(); });
    ASSERT_TRUE(schedule.build().has_value());

    const auto stats = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(seenDuringPhase, 0U); // nothing exists until the sync point
    EXPECT_EQ(stats.applied, 3U);   // three spawns, each with its component

    std::vector<float> points;
    Query<const Health&>{world}.forEach([&points](const Health& health) { points.push_back(health.points); });
    EXPECT_EQ(points, (std::vector{1.0F, 2.0F, 3.0F}));
}

TEST_F(ScheduleTest, ChangedSeesWritesSinceTheSystemsLastRun) {
    for (int index = 0; index < 10; ++index) {
        std::ignore = world.spawn(Position{});
    }
    std::size_t changed = 0;
    std::ignore = schedule.add({.name = "move"}, [](Query<Position&>& positions, Res<const Control> control) {
        if (control->isMoving) {
            positions.forEach([](Position& position) { position.x += 1; });
        }
    });
    std::ignore = schedule.add({.name = "watch"},
                               [&changed](Query<Changed<Position>>& moved) { changed = moved.count(); });
    ASSERT_TRUE(schedule.build().has_value());

    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(changed, 10U);

    world.resource<Control>()->isMoving = false;
    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(changed, 0U); // nothing written since watch last ran

    world.resource<Control>()->isMoving = true;
    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(changed, 10U);
}

TEST_F(ScheduleTest, EventsFlowFromWritersToReaders) {
    world.addEvents<Hit>();
    const Entity target = world.spawn(Health{.points = 100});
    const SystemId shoot =
        schedule.add({.name = "shoot"}, [target](EventWriter<Hit> hits) { hits.send({.target = target}); });
    const std::array afterShoot{shoot};
    std::ignore = schedule.add({.name = "damage", .after = afterShoot},
                               [](EventReader<Hit> hits, Query<Health&>& health) {
                                   hits.forEach([&health](const Hit& hit) {
                                       if (const auto row = health.get(hit.target)) {
                                           std::get<0>(*row).points -= 10;
                                       }
                                   });
                               });
    ASSERT_TRUE(schedule.build().has_value());

    std::ignore = schedule.run(Phase::Simulation, *jobs);
    world.advanceEvents();
    std::ignore = schedule.run(Phase::Simulation, *jobs);
    world.advanceEvents();
    EXPECT_EQ(world.get<Health>(target)->points, 80); // each hit read exactly once
}

TEST_F(ScheduleTest, AMissingResourceIsAViolationAndSkipsTheSystem) {
    bool ran = false;
    std::ignore = schedule.add({.name = "needsMissing"}, [&ran](Res<const Missing>) { ran = true; });
    ASSERT_TRUE(schedule.build().has_value());

    const nx::test::ContractRecorder recorder;
    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_FALSE(ran);
}

TEST_F(ScheduleTest, RunningWithoutBuildingIsAViolation) {
    std::ignore = schedule.add({.name = "unbuilt"}, [] {});
    const nx::test::ContractRecorder recorder;
    std::ignore = schedule.run(Phase::Simulation, *jobs);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
}

/// A small simulation: spawning, parallel movement, events and destruction through commands.
[[nodiscard]] auto simulate(std::uint32_t workerCount) -> std::uint64_t {
    constexpr int kTicks = 30;
    constexpr int kSpawnsPerTick = 40;
    constexpr float kWall = 25;

    auto jobs = makeJobs(workerCount);
    EXPECT_NE(jobs, nullptr);
    World world;
    registerComponents(world);
    world.addEvents<Hit>();
    Schedule schedule{world};

    std::ignore = schedule.add({.name = "spawn", .phase = Phase::PreSimulation}, [](Commands& commands) {
        for (int index = 0; index < kSpawnsPerTick; ++index) {
            const auto speed = static_cast<float>(index % 7) + 1;
            std::ignore =
                commands.spawn(Position{}, Velocity{.dx = speed, .dy = speed / 2}, Health{.points = 3});
        }
    });
    std::ignore = schedule.add({.name = "integrate"}, [](Query<const Velocity&, Position&>& bodies,
                                                         nx::jobs::JobSystem& jobSystem) {
        bodies.parallelForEachChunk(jobSystem,
                                    [](std::span<const Velocity> velocities, std::span<Position> positions) {
                                        for (std::size_t row = 0; row < positions.size(); ++row) {
                                            positions[row].x += velocities[row].dx;
                                            positions[row].y += velocities[row].dy;
                                        }
                                    });
    });
    std::ignore =
        schedule.add({.name = "collide"}, [](Query<Entity, const Position&>& bodies, EventWriter<Hit> hits) {
            bodies.forEach([&hits](Entity entity, const Position& position) {
                if (position.x > kWall) {
                    hits.send({.target = entity});
                }
            });
        });
    std::ignore = schedule.add({.name = "damage", .phase = Phase::Rules},
                               [](EventReader<Hit> hits, Query<Health&>& health, Commands& commands) {
                                   hits.forEach([&](const Hit& hit) {
                                       if (const auto row = health.get(hit.target)) {
                                           auto& [points] = std::get<0>(*row);
                                           points -= 1;
                                           if (points <= 0) {
                                               commands.destroy(hit.target);
                                           }
                                       }
                                   });
                               });
    EXPECT_TRUE(schedule.build().has_value());

    std::uint64_t hash = 0;
    for (int tick = 0; tick < kTicks; ++tick) {
        for (const Phase phase : {Phase::PreSimulation, Phase::Simulation, Phase::Rules}) {
            std::ignore = schedule.run(phase, *jobs);
        }
        world.advanceEvents();
        Query<Entity, const Position&, const Health&>{world}.forEach(
            [&hash](Entity entity, const Position& position, const Health& health) {
                const std::array values{static_cast<double>(std::to_underlying(entity.index())),
                                        static_cast<double>(std::to_underlying(entity.generation())),
                                        static_cast<double>(position.x), static_cast<double>(position.y),
                                        static_cast<double>(health.points)};
                hash = nx::hash64(std::as_bytes(std::span{values}), hash);
            });
    }
    return hash;
}

TEST(ScheduleDeterminismTest, TheWorldEvolvesTheSameWayWithAnyWorkerCount) {
    const std::uint64_t reference = simulate(0);
    for (const std::uint32_t workerCount : {1U, 3U, 15U}) {
        EXPECT_EQ(simulate(workerCount), reference) << workerCount << " workers";
    }
}

} // namespace
