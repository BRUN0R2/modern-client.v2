// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;

#include "contractRecorder.hpp"

namespace {

using nx::jobs::Grain;
using nx::jobs::JobContext;
using nx::jobs::JobHandle;
using nx::jobs::JobSystem;
using nx::jobs::Priority;

/// The worker counts of the determinism rule (docs/04-job-system.md, section 8). 0 is the serial
/// mode: every job runs inside wait().
constexpr std::array<std::uint32_t, 4> kWorkerCounts{0, 1, 3, 15};

/// Step of the sine wave the determinism test computes.
constexpr float kSineStep = 0.01F;

[[nodiscard]] auto makeJobSystem(std::uint32_t workerCount) -> std::unique_ptr<JobSystem> {
    auto created = JobSystem::create({.workerCount = workerCount});
    return created.has_value() ? std::move(*created) : nullptr;
}

class JobSystemTest : public testing::TestWithParam<std::uint32_t> {
protected:
    void SetUp() override {
        m_jobs = makeJobSystem(GetParam());
        ASSERT_NE(m_jobs, nullptr);
    }

    [[nodiscard]] auto jobs() -> JobSystem& { return *m_jobs; }

private:
    std::unique_ptr<JobSystem> m_jobs;
};

/// Test names such as "RunsASubmittedJob/3Workers". A named function, because the macro below
/// declares its own parameter called `info`, which a lambda parameter would shadow.
[[nodiscard]] auto workerCountName(const testing::TestParamInfo<std::uint32_t>& parameter) -> std::string {
    return std::format("{}Workers", parameter.param);
}

INSTANTIATE_TEST_SUITE_P(WorkerCounts, JobSystemTest, testing::ValuesIn(kWorkerCounts), workerCountName);

TEST_P(JobSystemTest, RunsASubmittedJob) {
    int value = 0;
    const JobHandle handle = jobs().submit({.label = "set"}, [&value] { value = 42; });
    ASSERT_TRUE(handle.isValid());
    jobs().wait(handle);

    EXPECT_EQ(value, 42);
    EXPECT_TRUE(jobs().isComplete(handle));
    jobs().wait(handle); // waiting again on a completed handle returns at once
}

TEST_P(JobSystemTest, GivesTheJobItsContext) {
    std::string_view label;
    const JobSystem* system = nullptr;
    jobs().wait(jobs().submit({.label = "context"}, [&](JobContext& context) {
        label = context.label();
        system = &context.jobSystem();
    }));

    EXPECT_EQ(label, "context");
    EXPECT_EQ(system, &jobs());
}

TEST_P(JobSystemTest, RunsDependentsAfterTheirDependencies) {
    constexpr int kRepetitions = 200;
    for (int repetition = 0; repetition < kRepetitions; ++repetition) {
        std::atomic<int> clock{0};
        std::array<int, 4> finishedAt{};
        auto record = [&clock, &finishedAt](std::size_t job) {
            return [&clock, &finishedAt, job] {
                finishedAt[job] = clock.fetch_add(1, std::memory_order_seq_cst);
            };
        };

        // Diamond: top, then left and right, then bottom.
        const JobHandle top = jobs().submit({.label = "top"}, record(0));
        const std::array topOnly{top};
        const JobHandle left = jobs().submit({.label = "left", .dependsOn = topOnly}, record(1));
        const JobHandle right = jobs().submit({.label = "right", .dependsOn = topOnly}, record(2));
        const std::array sides{left, right};
        const JobHandle bottom = jobs().submit({.label = "bottom", .dependsOn = sides}, record(3));
        jobs().wait(bottom);

        ASSERT_LT(finishedAt[0], finishedAt[1]);
        ASSERT_LT(finishedAt[0], finishedAt[2]);
        ASSERT_LT(finishedAt[1], finishedAt[3]);
        ASSERT_LT(finishedAt[2], finishedAt[3]);
    }
}

TEST_P(JobSystemTest, ADependencyThatAlreadyCompletedDoesNotHoldTheJob) {
    const JobHandle first = jobs().submit({.label = "first"}, [] {});
    jobs().wait(first);

    bool ran = false;
    const std::array dependencies{first};
    jobs().wait(jobs().submit({.label = "second", .dependsOn = dependencies}, [&ran] { ran = true; }));
    EXPECT_TRUE(ran);
}

TEST_P(JobSystemTest, ParallelForVisitsEveryElementOnce) {
    std::vector<int> visits(10'000, 0);
    jobs().wait(
        jobs().parallelFor({.label = "visit"}, visits, Grain::automatic(), [](int& visit) { ++visit; }));
    EXPECT_EQ(std::ranges::count(visits, 1), std::ssize(visits));
}

TEST_P(JobSystemTest, ParallelForOverAnIndexRangeWithAFixedGrain) {
    constexpr std::size_t kCount = 1001;
    std::vector<std::size_t> squares(kCount, 0);
    const JobHandle handle = jobs().parallelFor(
        {.label = "squares", .priority = Priority::Normal}, std::views::iota(std::size_t{0}, kCount),
        Grain::fixed(7), [&squares](JobContext&, std::size_t index) { squares[index] = index * index; });
    jobs().wait(handle);

    for (std::size_t index = 0; index < kCount; ++index) {
        ASSERT_EQ(squares[index], index * index);
    }
}

TEST_P(JobSystemTest, ParallelForOnAnEmptyRangeIsAlreadyComplete) {
    std::vector<int> empty;
    const JobHandle handle = jobs().parallelFor({.label = "empty"}, empty, Grain::automatic(), [](int&) {});
    EXPECT_TRUE(jobs().isComplete(handle));
    jobs().wait(handle);
}

TEST_P(JobSystemTest, AnEmptyParallelForCompletesAfterItsDependencies) {
    // Regression: an empty range completed at once, so work chained on it could run before the
    // dependencies had. The urgent job after the loop would then run before the low-priority one.
    std::atomic<bool> dependencyRan{false};
    const JobHandle dependency =
        jobs().submit({.label = "dependency", .priority = Priority::Low},
                      [&dependencyRan] { dependencyRan.store(true, std::memory_order_seq_cst); });
    std::vector<int> empty;
    const std::array beforeLoop{dependency};
    const JobHandle loop = jobs().parallelFor({.label = "empty", .dependsOn = beforeLoop}, empty,
                                              Grain::automatic(), [](int&) {});
    std::atomic<bool> sawDependency{false};
    const std::array afterLoop{loop};
    jobs().wait(jobs().submit({.label = "after", .priority = Priority::Critical, .dependsOn = afterLoop},
                              [&dependencyRan, &sawDependency] {
                                  sawDependency.store(dependencyRan.load(std::memory_order_seq_cst),
                                                      std::memory_order_seq_cst);
                              }));
    EXPECT_TRUE(sawDependency.load(std::memory_order_seq_cst));
    EXPECT_TRUE(jobs().isComplete(loop));
}

TEST_P(JobSystemTest, JobsCanSubmitAndWaitOnMoreJobs) {
    constexpr std::size_t kCount = 512;
    std::vector<std::uint64_t> values(kCount, 0);
    std::uint64_t sum = 0;
    jobs().wait(jobs().submit({.label = "outer"}, [&](JobContext& context) {
        JobSystem& system = context.jobSystem();
        system.wait(system.parallelFor({.label = "inner"}, values, Grain::fixed(16),
                                       [](std::uint64_t& value) { value = 3; }));
        sum = std::ranges::fold_left(values, std::uint64_t{0}, std::plus{});
    }));
    EXPECT_EQ(sum, 3 * kCount);
}

/// Sets a flag when destroyed; moving hands the duty to the new object.
class DestructionFlag {
public:
    explicit DestructionFlag(std::atomic<bool>& destroyed) noexcept : m_destroyed{&destroyed} {}
    DestructionFlag(DestructionFlag&& other) noexcept
        : m_destroyed{std::exchange(other.m_destroyed, nullptr)} {}
    DestructionFlag(const DestructionFlag&) = delete;
    auto operator=(const DestructionFlag&) -> DestructionFlag& = delete;
    auto operator=(DestructionFlag&&) -> DestructionFlag& = delete;
    ~DestructionFlag() {
        if (m_destroyed != nullptr) {
            m_destroyed->store(true, std::memory_order_seq_cst);
        }
    }

private:
    std::atomic<bool>* m_destroyed;
};

TEST_P(JobSystemTest, DestroysTheCapturesBeforeTheHandleCompletes) {
    std::atomic<bool> destroyed{false};
    const JobHandle handle = jobs().submit({.label = "captures"}, [flag = DestructionFlag{destroyed}] {});
    jobs().wait(handle);
    EXPECT_TRUE(destroyed.load(std::memory_order_seq_cst));
}

TEST_P(JobSystemTest, StatsCountEveryExecutedJob) {
    constexpr std::size_t kJobCount = 300;
    jobs().wait(jobs().parallelFor({.label = "count"}, std::views::iota(std::size_t{0}, kJobCount),
                                   Grain::fixed(1), [](std::size_t) {}));

    const auto stats = jobs().stats();
    ASSERT_EQ(stats.workers.size(), GetParam());
    std::uint64_t executed = stats.jobsExecutedByWaiters;
    for (const auto& worker : stats.workers) {
        executed += worker.executedJobs;
    }
    EXPECT_EQ(executed, kJobCount);
    if (GetParam() == 0) {
        EXPECT_EQ(stats.jobsExecutedByWaiters, kJobCount);
    }
}

TEST_P(JobSystemTest, ManyJobsWithRandomDependencies) {
    constexpr std::size_t kJobCount = 2000;
    constexpr std::size_t kMaxDependencies = 3;
    constexpr std::uint32_t kSeed = 2026;

    std::mt19937 random{kSeed};
    std::atomic<int> clock{0};
    std::vector<int> finishedAt(kJobCount, -1);
    std::vector<std::vector<std::size_t>> dependenciesOf(kJobCount);
    std::vector<JobHandle> handles;
    handles.reserve(kJobCount);

    for (std::size_t job = 0; job < kJobCount; ++job) {
        std::vector<JobHandle> dependencies;
        if (job > 0) {
            std::uniform_int_distribution<std::size_t> pick{0, job - 1};
            const std::size_t count = std::uniform_int_distribution<std::size_t>{0, kMaxDependencies}(random);
            for (std::size_t dependency = 0; dependency < count; ++dependency) {
                const std::size_t target = pick(random);
                dependenciesOf[job].push_back(target);
                dependencies.push_back(handles[target]);
            }
        }
        handles.push_back(
            jobs().submit({.label = "random", .dependsOn = dependencies}, [&clock, &finishedAt, job] {
                finishedAt[job] = clock.fetch_add(1, std::memory_order_seq_cst);
            }));
    }
    for (const JobHandle handle : handles) {
        jobs().wait(handle);
    }

    for (std::size_t job = 0; job < kJobCount; ++job) {
        ASSERT_GE(finishedAt[job], 0) << "job " << job << " never ran";
        for (const std::size_t dependency : dependenciesOf[job]) {
            ASSERT_LT(finishedAt[dependency], finishedAt[job])
                << "job " << job << " ran before " << dependency;
        }
    }
}

TEST_P(JobSystemTest, DestructionRunsTheJobsNobodyWaitedFor) {
    constexpr int kJobCount = 100;
    std::atomic<int> ran{0};
    {
        auto system = makeJobSystem(GetParam());
        ASSERT_NE(system, nullptr);
        for (int job = 0; job < kJobCount; ++job) {
            std::ignore = system->submit({.label = "unwaited"}, [&ran] {
                ran.fetch_add(1, std::memory_order_seq_cst);
            }); // never waited on
        }
    }
    EXPECT_EQ(ran.load(std::memory_order_seq_cst), kJobCount);
}

TEST(JobSystemSerialTest, WithoutWorkersJobsRunInsideWaitOnTheWaitingThread) {
    auto jobs = makeJobSystem(0);
    ASSERT_NE(jobs, nullptr);
    std::thread::id ranOn;
    const JobHandle handle =
        jobs->submit({.label = "serial"}, [&ranOn] { ranOn = std::this_thread::get_id(); });

    EXPECT_FALSE(jobs->isComplete(handle));
    jobs->wait(handle);
    EXPECT_EQ(ranOn, std::this_thread::get_id());
}

TEST(JobSystemSerialTest, MoreUrgentJobsRunFirst) {
    auto jobs = makeJobSystem(0);
    ASSERT_NE(jobs, nullptr);
    std::vector<Priority> order;
    auto record = [&order](Priority priority) {
        return [&order, priority] {
            order.push_back(priority);
        };
    };

    const JobHandle background =
        jobs->submit({.label = "background", .priority = Priority::Background}, record(Priority::Background));
    // Waiting on the background job is enough: the serial system runs the more urgent ones first.
    std::ignore = jobs->submit({.label = "low", .priority = Priority::Low}, record(Priority::Low));
    std::ignore = jobs->submit({.label = "normal", .priority = Priority::Normal}, record(Priority::Normal));
    std::ignore =
        jobs->submit({.label = "critical", .priority = Priority::Critical}, record(Priority::Critical));
    jobs->wait(background);

    EXPECT_EQ(order,
              (std::vector{Priority::Critical, Priority::Normal, Priority::Low, Priority::Background}));
}

TEST(JobSystemDeterminismTest, ResultsDoNotDependOnTheWorkerCount) {
    constexpr std::size_t kElementCount = 4096;
    constexpr std::size_t kBatchSize = 64;

    auto run = [&](std::uint32_t workerCount) -> std::uint64_t {
        auto jobs = makeJobSystem(workerCount);
        EXPECT_NE(jobs, nullptr);
        if (jobs == nullptr) {
            return 0;
        }
        // Outputs at the element's index, then partial sums per fixed batch combined in index order.
        std::vector<float> outputs(kElementCount);
        jobs->wait(jobs->parallelFor({.label = "outputs"}, std::views::iota(std::size_t{0}, kElementCount),
                                     Grain::automatic(), [&outputs](std::size_t index) {
                                         outputs[index] = std::sin(static_cast<float>(index) * kSineStep);
                                     }));
        std::vector<float> partials(kElementCount / kBatchSize);
        jobs->wait(jobs->parallelFor({.label = "partials"}, std::views::iota(std::size_t{0}, partials.size()),
                                     Grain::fixed(1), [&](std::size_t batch) {
                                         const auto slice =
                                             std::span{outputs}.subspan(batch * kBatchSize, kBatchSize);
                                         partials[batch] = std::ranges::fold_left(slice, 0.0F, std::plus{});
                                     }));
        const float total = std::ranges::fold_left(partials, 0.0F, std::plus{});
        return nx::hash64(std::as_bytes(std::span{outputs}), nx::hash64(std::as_bytes(std::span{&total, 1})));
    };

    const std::uint64_t reference = run(kWorkerCounts.front());
    for (const std::uint32_t workerCount : kWorkerCounts) {
        EXPECT_EQ(run(workerCount), reference) << workerCount << " workers";
    }
}

TEST(JobSystemCreateTest, RejectsMoreWorkersThanTheLimit) {
    const auto created = JobSystem::create({.workerCount = nx::jobs::kMaxWorkerCount + 1});
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, nx::jobs::JobSystemError::TooManyWorkers);
}

TEST(JobSystemCreateTest, DefaultWorkerCountLeavesOneHardwareThreadToTheCaller) {
    const std::uint32_t hardwareThreads = std::thread::hardware_concurrency();
    const std::uint32_t expected =
        hardwareThreads > 1 ? std::min(hardwareThreads - 1, nx::jobs::kMaxWorkerCount) : 0;
    EXPECT_EQ(nx::jobs::defaultWorkerCount(), expected);
    EXPECT_EQ(nx::jobs::JobSystemConfig{}.workerCount, expected);
}

TEST(JobSystemContractTest, WaitingOnAnInvalidHandleIsAViolation) {
    auto jobs = makeJobSystem(0);
    ASSERT_NE(jobs, nullptr);
    const nx::test::ContractRecorder recorder;
    jobs->wait(JobHandle{});
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_EQ(nx::test::ContractRecorder::last().kind, nx::ContractKind::Precondition);
}

TEST(JobSystemContractTest, DependingOnAnInvalidHandleIsAViolation) {
    auto jobs = makeJobSystem(0);
    ASSERT_NE(jobs, nullptr);
    const nx::test::ContractRecorder recorder;
    bool ran = false;
    const std::array dependencies{JobHandle{}};
    jobs->wait(jobs->submit({.label = "invalid", .dependsOn = dependencies}, [&ran] { ran = true; }));
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_TRUE(ran); // the invalid dependency is ignored after the report
}

TEST(GrainTest, FixedUsesTheGivenBatchSize) {
    static_assert(Grain::fixed(8).batchSize(100, 4) == 8);
    static_assert(Grain::fixed(8).batchSize(3, 4) == 8);
}

TEST(GrainTest, AutomaticAimsAtAFewBatchesPerThread) {
    // 1000 elements over 4 threads: 16 batches of 63 elements (the last one shorter).
    static_assert(Grain::automatic().batchSize(1000, 4) == 63);
    static_assert(Grain::automatic(100).batchSize(1000, 4) == 100);
    static_assert(Grain::automatic().batchSize(0, 4) == 1);
}

TEST(GrainTest, AFixedGrainOfZeroIsAViolation) {
    const nx::test::ContractRecorder recorder;
    const Grain grain = Grain::fixed(0);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_EQ(grain.batchSize(10, 1), 1U);
}

TEST(JobLabelTest, KeepsTheText) {
    static_assert(nx::jobs::JobLabel{"physics"}.text() == "physics");
    static_assert(nx::jobs::JobLabel{std::string_view{"animation"}}.text() == "animation");
}

} // namespace
