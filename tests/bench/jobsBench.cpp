// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Throughput and latency of the Job System (docs/04-job-system.md; docs/18-testing-and-quality.md,
// "Performance"). Every benchmark runs with 0, 1 and 3 workers and with the machine's default.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.jobs;

namespace {

using nx::jobs::Grain;
using nx::jobs::JobHandle;
using nx::jobs::JobSystem;

constexpr std::size_t kBatchJobs = 1024;
constexpr std::size_t kChainLength = 256;
constexpr std::size_t kElements = std::size_t{1} << 20U;
constexpr float kScale = 2.0F;

[[nodiscard]] auto makeJobs(const benchmark::State& state) -> std::unique_ptr<JobSystem> {
    auto created = JobSystem::create({.workerCount = static_cast<std::uint32_t>(state.range(0))});
    return created.has_value() ? std::move(*created) : nullptr;
}

void workerCounts(benchmark::Benchmark* benchmark) {
    benchmark->ArgName("workers")->Arg(0)->Arg(1)->Arg(3);
    if (const auto machine = nx::jobs::defaultWorkerCount(); machine > 3) {
        benchmark->Arg(machine);
    }
    benchmark->UseRealTime();
}

/// Independent empty jobs submitted from the waiting thread: the cost of submit, schedule and wait.
void submitEmptyJobs(benchmark::State& state) {
    const auto jobs = makeJobs(state);
    std::vector<JobHandle> handles(kBatchJobs);
    for ([[maybe_unused]] auto iteration : state) {
        for (JobHandle& handle : handles) {
            handle = jobs->submit({.label = "bench.empty"}, [] {});
        }
        for (const JobHandle handle : handles) {
            jobs->wait(handle);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatchJobs));
}
BENCHMARK(submitEmptyJobs)->Apply(workerCounts);

/// A chain where each job depends on the previous one: the latency of continuations.
void dependencyChain(benchmark::State& state) {
    const auto jobs = makeJobs(state);
    for ([[maybe_unused]] auto iteration : state) {
        JobHandle previous = jobs->submit({.label = "bench.chain"}, [] {});
        for (std::size_t link = 1; link < kChainLength; ++link) {
            const std::array dependsOn{previous};
            previous = jobs->submit({.label = "bench.chain", .dependsOn = dependsOn}, [] {});
        }
        jobs->wait(previous);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kChainLength));
}
BENCHMARK(dependencyChain)->Apply(workerCounts);

/// parallelFor over a million floats, one output per element.
void parallelForScale(benchmark::State& state) {
    const auto jobs = makeJobs(state);
    std::vector<float> input(kElements, 1.0F);
    std::vector<float> output(kElements);
    std::vector<std::size_t> indices(kElements);
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    for ([[maybe_unused]] auto iteration : state) {
        jobs->wait(jobs->parallelFor(
            {.label = "bench.parallelFor"}, indices, Grain::automatic(),
            [&input, &output](std::size_t index) { output[index] = input[index] * kScale; }));
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElements));
}
BENCHMARK(parallelForScale)->Apply(workerCounts);

} // namespace
