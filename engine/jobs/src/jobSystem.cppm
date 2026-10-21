// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The Job System (docs/04-job-system.md, ADR-0003): a fixed pool of workers with Chase-Lev deques,
/// a global queue per priority for submissions from other threads, dependencies through atomic
/// counters and wait() that runs other jobs instead of blocking.
export module nx.jobs:jobSystem;

import std;
import nx.core;
import :concurrentPool;
import :intrusiveQueue;
import :job;
import :workStealingDeque;

export namespace nx::jobs {

/// Worker count must not exceed this: every thief scans every worker, so far more workers than
/// hardware threads only adds scanning. Raise it when hardware with more threads shows up.
inline constexpr std::uint32_t kMaxWorkerCount = 256;

/// One worker per hardware thread except one, which stays with the thread that submits and waits
/// (the main thread). 0 when the hardware thread count is unknown.
[[nodiscard]] auto defaultWorkerCount() noexcept -> std::uint32_t;

/// How to start a JobSystem.
struct JobSystemConfig {
    /// Threads that run jobs, besides the threads that wait on them (`jobs_workers`). With 0, every
    /// job runs inside wait() on the waiting thread, in priority and submission order: the serial
    /// mode used to isolate concurrency bugs (`jobs_serial`).
    std::uint32_t workerCount = defaultWorkerCount();
};

/// Why a job system could not start.
enum class JobSystemError : std::uint8_t {
    TooManyWorkers, ///< More workers than kMaxWorkerCount.
};

/// What one worker has done since the system started.
struct WorkerStats {
    std::uint64_t executedJobs = 0; ///< Jobs it ran, stolen ones included.
    std::uint64_t stolenJobs = 0;   ///< Taken from another worker's deque.
    std::uint64_t sleeps = 0;       ///< Times the worker found no work and slept.
};

/// Counters for `jobs_stats`. Each value is exact for its own counter, but they are read one after
/// another while the system runs.
struct JobSystemStats {
    std::vector<WorkerStats> workers;        ///< One per worker, in worker order.
    std::uint64_t jobsExecutedByWaiters = 0; ///< Run by threads inside wait().
};

namespace detail {

struct Worker;

} // namespace detail

/// Runs jobs on a fixed pool of worker threads. Any thread may submit and wait, a running job
/// included; a thread that waits runs jobs meanwhile.
class JobSystem {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// Starts the workers. Fails with TooManyWorkers above kMaxWorkerCount. A failure to create a
    /// thread ends the process: std::thread reports it only through an exception, and the engine
    /// is built without them.
    [[nodiscard]] static auto create(const JobSystemConfig& config = {})
        -> Result<std::unique_ptr<JobSystem>, JobSystemError>;

    /// Use create(). Public only so that create() can use std::make_unique.
    JobSystem(CreateToken token, std::uint32_t workerCount);

    JobSystem(const JobSystem&) = delete;
    auto operator=(const JobSystem&) -> JobSystem& = delete;
    JobSystem(JobSystem&&) = delete;
    auto operator=(JobSystem&&) -> JobSystem& = delete;

    /// Runs every submitted job to completion, then stops the workers. Must not run inside a job.
    ~JobSystem();

    /// Queues `function` to run once every handle in `desc.dependsOn` has completed. The captures
    /// are moved into the job and destroyed right after it runs, before its handle completes.
    template <JobFunction Function>
    [[nodiscard]] auto submit(const JobDesc& desc, Function&& function) -> JobHandle {
        const JobHandle handle = allocateCounter(1);
        detail::Job& job = allocateJob(desc, handle);
        job.function.emplace(std::forward<Function>(function));
        schedule(job, desc.dependsOn);
        return handle;
    }

    /// Calls `function` on every element of `range`, in batches that run as separate jobs. The
    /// handle completes when every batch has run, and never before its dependencies: an empty range
    /// with dependencies runs one empty batch after them; without, it is complete at once. Each batch gets a
    /// copy of `function`, and the range must stay alive until then: borrowed ranges only (lvalues, spans,
    /// views), so a temporary container cannot dangle. Outputs written at the element's index make the result
    /// independent of the worker count (docs/04, section 8).
    template <std::ranges::random_access_range Range, class Function>
        requires std::ranges::sized_range<Range> && std::ranges::borrowed_range<Range> &&
                 ElementFunction<std::decay_t<Function>, std::ranges::range_reference_t<Range>> &&
                 std::copy_constructible<std::decay_t<Function>>
    [[nodiscard]] auto parallelFor(const JobDesc& desc, Range&& range, Grain grain, Function&& function)
        -> JobHandle {
        using Batch = detail::BatchJob<std::ranges::iterator_t<Range>, std::decay_t<Function>>;
        static_assert(sizeof(Batch) <= kJobCaptureCapacity,
                      "parallelFor: the element function's captures plus the batch bounds exceed "
                      "nx::jobs::kJobCaptureCapacity bytes; capture large data by reference");

        const auto elementCount = static_cast<std::size_t>(std::ranges::size(range));
        const std::size_t batchSize = grain.batchSize(elementCount, threadCount());
        const std::size_t batchCount = elementCount == 0 && !desc.dependsOn.empty()
                                           ? std::size_t{1}
                                           : divideRoundingUp(elementCount, batchSize);

        const JobHandle handle = allocateCounter(batchCount);
        if (batchCount == 0) {
            completeWithoutJobs(handle);
            return handle;
        }
        const auto first = std::ranges::begin(range);
        for (std::size_t batch = 0; batch < batchCount; ++batch) {
            const std::size_t begin = batch * batchSize;
            detail::Job& job = allocateJob(desc, handle);
            job.function.emplace(Batch{.first = first,
                                       .begin = begin,
                                       .end = std::min(begin + batchSize, elementCount),
                                       .function = function});
            schedule(job, desc.dependsOn);
        }
        return handle;
    }

    /// Returns once `handle` has completed. Meanwhile the calling thread runs other jobs, and it
    /// sleeps only while no job is visible to it.
    void wait(JobHandle handle);

    /// Whether `handle`, which must be valid, has completed; true for every handle once its job and
    /// everything it depends on have run.
    [[nodiscard]] auto isComplete(JobHandle handle) const noexcept -> bool;

    /// Worker threads, besides the threads that wait.
    [[nodiscard]] inline auto workerCount() const noexcept -> std::uint32_t { return m_workerCount; }

    /// What every worker has done so far: for jobs_stats.
    [[nodiscard]] auto stats() const -> JobSystemStats;

private:
    /// Threads that run jobs during a frame: the workers plus the thread that waits.
    [[nodiscard]] inline auto threadCount() const noexcept -> std::size_t {
        return std::size_t{m_workerCount} + 1;
    }

    [[nodiscard]] auto allocateCounter(std::uint64_t pendingJobs) -> JobHandle;
    [[nodiscard]] auto allocateJob(const JobDesc& desc, JobHandle handle) -> detail::Job&;
    void completeWithoutJobs(JobHandle handle) noexcept;
    void schedule(detail::Job& job, std::span<const JobHandle> dependsOn);
    [[nodiscard]] auto addContinuation(JobHandle dependency, detail::Job& job) -> bool;
    void enqueue(detail::Job& job);

    [[nodiscard]] auto currentWorker() const noexcept -> detail::Worker*;
    [[nodiscard]] auto findJob(detail::Worker* self) noexcept -> detail::Job*;
    [[nodiscard]] auto steal(Priority priority, detail::Worker* self) noexcept -> detail::Job*;
    [[nodiscard]] auto hasVisibleWork() const noexcept -> bool;
    void execute(detail::Job& job, detail::Worker* self);
    void finishCounter(PoolIndex counterIndex);

    template <class Done, class BeforeSleep>
    void helpUntil(Done done, BeforeSleep beforeSleep);
    void sleep(std::uint32_t observedSignal, detail::Worker* self) noexcept;
    void wakeOne() noexcept;
    void wakeAll() noexcept;
    void workerMain(detail::Worker& worker);

    std::uint32_t m_workerCount;
    ConcurrentPool<detail::Job> m_jobs;
    ConcurrentPool<detail::Counter> m_counters;
    ConcurrentPool<detail::ContinuationLink> m_links;
    EnumArray<Priority, IntrusiveQueue<detail::Job>> m_globalQueues;
    std::vector<std::unique_ptr<detail::Worker>> m_workers;

    /// Bumped whenever a sleeping thread may have something to do: a job was queued, a counter
    /// with a waiter completed, or shutdown progressed. Sleepers wait for it to change.
    alignas(kCacheLineSize) std::atomic<std::uint32_t> m_signal{0};
    alignas(kCacheLineSize) std::atomic<std::uint32_t> m_sleepers{0};
    alignas(kCacheLineSize) std::atomic<std::uint64_t> m_jobsInFlight{0}; // submitted, not yet finished
    std::atomic<std::uint64_t> m_jobsExecutedByWaiters{0};
    std::atomic<bool> m_stopping{false};
};

} // namespace nx::jobs
