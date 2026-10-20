// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.jobs;

import std;
import nx.core;

namespace nx::jobs {

namespace detail {

struct Worker {
    Worker(JobSystem& owner, std::uint32_t workerIndex) noexcept : system{&owner}, index{workerIndex} {}

    JobSystem* system;
    std::uint32_t index;
    EnumArray<Priority, WorkStealingDeque<Job*>> deques;
    // Written by this worker only; read by JobSystem::stats().
    alignas(kCacheLineSize) std::atomic<std::uint64_t> executedJobs{0};
    std::atomic<std::uint64_t> stolenJobs{0};
    std::atomic<std::uint64_t> sleeps{0};
    std::thread thread;
};

} // namespace detail

namespace {

/// The worker running on this thread, if any. It is the identity of the thread itself, set once
/// when a worker starts; jobs never keep state here (docs/04-job-system.md, section 7).
thread_local detail::Worker* tCurrentWorker = nullptr;

/// Declaration order of Priority is the order of urgency.
constexpr std::array kPrioritiesByUrgency{Priority::Critical, Priority::Normal, Priority::Low,
                                          Priority::Background};
static_assert(kPrioritiesByUrgency.size() == enumCount<Priority>());

/// Idle rounds (each one a full scan for work followed by a yield) before a thread sleeps. Keeps
/// short gaps between bursts of jobs from paying for a sleep and a wake-up.
constexpr int kIdleRoundsBeforeSleep = 32;

// Counter::state layout.
constexpr unsigned kGenerationShift = 32;
constexpr std::uint32_t kEmptyList = 0;
constexpr std::uint32_t kClosedList = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] constexpr auto packState(std::uint32_t generation, std::uint32_t list) noexcept
    -> std::uint64_t {
    return (std::uint64_t{generation} << kGenerationShift) | list;
}

[[nodiscard]] constexpr auto generationOf(std::uint64_t state) noexcept -> std::uint32_t {
    return truncate<std::uint32_t>(state >> kGenerationShift);
}

[[nodiscard]] constexpr auto listOf(std::uint64_t state) noexcept -> std::uint32_t {
    return truncate<std::uint32_t>(state);
}

/// Whether the counter incarnation `generation` has completed: it was closed, or reused since.
[[nodiscard]] constexpr auto hasCompleted(std::uint64_t state, std::uint32_t generation) noexcept -> bool {
    return generationOf(state) != generation || listOf(state) == kClosedList;
}

} // namespace

auto defaultWorkerCount() noexcept -> std::uint32_t {
    const std::uint32_t hardwareThreads = std::thread::hardware_concurrency();
    return hardwareThreads > 1 ? std::min(hardwareThreads - 1, kMaxWorkerCount) : 0;
}

auto JobSystem::create(const JobSystemConfig& config) -> Result<std::unique_ptr<JobSystem>, JobSystemError> {
    if (config.workerCount > kMaxWorkerCount) {
        return fail(JobSystemError::TooManyWorkers,
                    std::format("{} workers requested, at most {}", config.workerCount, kMaxWorkerCount));
    }
    return std::make_unique<JobSystem>(CreateToken{}, config.workerCount);
}

JobSystem::JobSystem(CreateToken /*token*/, std::uint32_t workerCount) : m_workerCount{workerCount} {
    m_workers.reserve(workerCount);
    for (std::uint32_t index = 0; index < workerCount; ++index) {
        m_workers.push_back(std::make_unique<detail::Worker>(*this, index));
    }
    // Threads start only once every worker exists: a thief scans all of them from its first round.
    for (const auto& worker : m_workers) {
        worker->thread = std::thread{&JobSystem::workerMain, this, std::ref(*worker)};
    }
}

JobSystem::~JobSystem() {
    expects(currentWorker() == nullptr, "JobSystem: destroyed from one of its own jobs");
    m_stopping.store(true, std::memory_order_seq_cst);
    wakeAll();
    // Help the workers finish what is left; with no workers, this thread runs all of it.
    helpUntil([this] { return m_jobsInFlight.load(std::memory_order_seq_cst) == 0; }, [] {});
    for (const auto& worker : m_workers) {
        worker->thread.join();
    }
}

void JobSystem::wait(JobHandle handle) {
    if (!handle.isValid()) {
        expects(false, "JobSystem::wait: invalid handle");
        return;
    }
    detail::Counter& counter = m_counters[handle.m_counter];
    helpUntil([this, handle] { return isComplete(handle); },
              [&counter] { counter.hasWaiter.store(true, std::memory_order_seq_cst); });
}

auto JobSystem::isComplete(JobHandle handle) const noexcept -> bool {
    if (!handle.isValid()) {
        expects(false, "JobSystem::isComplete: invalid handle");
        return true;
    }
    // seq_cst: wait() pairs this load with its store to Counter::hasWaiter (see finishCounter).
    return hasCompleted(m_counters[handle.m_counter].state.load(std::memory_order_seq_cst),
                        handle.m_generation);
}

auto JobSystem::stats() const -> JobSystemStats {
    JobSystemStats result;
    result.workers.reserve(m_workers.size());
    for (const auto& worker : m_workers) {
        result.workers.push_back({
            .executedJobs = worker->executedJobs.load(std::memory_order_relaxed),
            .stolenJobs = worker->stolenJobs.load(std::memory_order_relaxed),
            .sleeps = worker->sleeps.load(std::memory_order_relaxed),
        });
    }
    result.jobsExecutedByWaiters = m_jobsExecutedByWaiters.load(std::memory_order_relaxed);
    return result;
}

auto JobSystem::allocateCounter(std::uint64_t pendingJobs) -> JobHandle {
    const auto index = m_counters.acquire();
    detail::Counter& counter = m_counters[index];
    // The counter becomes visible to other threads only through the jobs it is given, and queuing a
    // job publishes these stores.
    counter.pendingJobs.store(pendingJobs, std::memory_order_relaxed);
    counter.hasWaiter.store(false, std::memory_order_relaxed);
    return JobHandle{index, generationOf(counter.state.load(std::memory_order_relaxed))};
}

auto JobSystem::allocateJob(const JobDesc& desc, JobHandle handle) -> detail::Job& {
    const auto index = m_jobs.acquire();
    detail::Job& job = m_jobs[index];
    job.label = desc.label.text();
    job.priority = desc.priority;
    job.counter = handle.m_counter;
    job.poolIndex = index;
    m_jobsInFlight.fetch_add(1, std::memory_order_relaxed);
    return job;
}

void JobSystem::completeWithoutJobs(JobHandle handle) {
    finishCounter(handle.m_counter);
}

void JobSystem::schedule(detail::Job& job, std::span<const JobHandle> dependsOn) {
    // The extra count keeps the job from starting while its dependencies are being registered.
    job.unresolvedDependencies.store(dependsOn.size() + 1, std::memory_order_relaxed);
    for (const JobHandle dependency : dependsOn) {
        const bool isValid = dependency.isValid();
        expects(isValid, "JobSystem: a job depends on an invalid handle");
        if (!isValid || !addContinuation(dependency, job)) {
            // Already complete (or ignored after the violation): one dependency fewer to wait for.
            job.unresolvedDependencies.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
    if (job.unresolvedDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        enqueue(job);
    }
}

auto JobSystem::addContinuation(JobHandle dependency, detail::Job& job) -> bool {
    detail::Counter& counter = m_counters[dependency.m_counter];
    auto state = counter.state.load(std::memory_order_acquire);
    if (hasCompleted(state, dependency.m_generation)) {
        return false;
    }
    const auto linkIndex = m_links.acquire();
    detail::ContinuationLink& link = m_links[linkIndex];
    link.job = &job;
    do {
        if (hasCompleted(state, dependency.m_generation)) {
            m_links.release(linkIndex);
            return false;
        }
        link.next = listOf(state);
        // The generation is part of the compared word, so a counter that completed or was reused
        // in the meantime makes the exchange fail.
    } while (!counter.state.compare_exchange_weak(state, packState(dependency.m_generation, linkIndex + 1),
                                                  std::memory_order_acq_rel, std::memory_order_acquire));
    return true;
}

void JobSystem::enqueue(detail::Job& job) {
    if (detail::Worker* const self = currentWorker()) {
        self->deques[job.priority].push(&job);
    } else {
        m_globalQueues[job.priority].push(job);
    }
    wakeOne();
}

auto JobSystem::currentWorker() const noexcept -> detail::Worker* {
    return tCurrentWorker != nullptr && tCurrentWorker->system == this ? tCurrentWorker : nullptr;
}

auto JobSystem::findJob(detail::Worker* self) -> detail::Job* {
    for (const Priority priority : kPrioritiesByUrgency) {
        if (self != nullptr) {
            if (const auto own = self->deques[priority].pop()) {
                return *own;
            }
        }
        if (detail::Job* const submitted = m_globalQueues[priority].tryPop()) {
            return submitted;
        }
        if (detail::Job* const stolen = steal(priority, self)) {
            return stolen;
        }
    }
    return nullptr;
}

auto JobSystem::steal(Priority priority, detail::Worker* self) -> detail::Job* {
    const std::size_t workerCount = m_workers.size();
    // Each worker starts with its right-hand neighbour, which spreads the thieves over the victims.
    const std::size_t start = self != nullptr ? std::size_t{self->index} + 1 : 0;
    for (std::size_t offset = 0; offset < workerCount; ++offset) {
        detail::Worker& victim = *m_workers[(start + offset) % workerCount];
        if (&victim == self) {
            continue;
        }
        if (const auto stolen = victim.deques[priority].steal()) {
            if (self != nullptr) {
                self->stolenJobs.fetch_add(1, std::memory_order_relaxed);
            }
            return *stolen;
        }
    }
    return nullptr;
}

auto JobSystem::hasVisibleWork() const noexcept -> bool {
    for (const Priority priority : kPrioritiesByUrgency) {
        if (m_globalQueues[priority].hasVisibleNodes()) {
            return true;
        }
        for (const auto& worker : m_workers) {
            if (worker->deques[priority].sizeApprox() != 0) {
                return true;
            }
        }
    }
    return false;
}

void JobSystem::execute(detail::Job& job, detail::Worker* self) {
    JobContext context{*this, job.label};
    job.function(context);
    job.function.reset();

    const std::uint32_t counterIndex = job.counter;
    m_jobs.release(job.poolIndex);
    if (self != nullptr) {
        self->executedJobs.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_jobsExecutedByWaiters.fetch_add(1, std::memory_order_relaxed);
    }

    // acq_rel: the last job to finish acquires the writes of all the others before releasing the
    // continuations, which then see every result of the counter's jobs.
    if (m_counters[counterIndex].pendingJobs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        finishCounter(counterIndex);
    }
    // seq_cst pairs with the destructor, which stores m_stopping and then reads m_jobsInFlight: either
    // the destructor sees zero, or this thread sees m_stopping and wakes it.
    if (m_jobsInFlight.fetch_sub(1, std::memory_order_seq_cst) == 1 &&
        m_stopping.load(std::memory_order_seq_cst)) {
        wakeAll();
    }
}

void JobSystem::finishCounter(std::uint32_t counterIndex) {
    detail::Counter& counter = m_counters[counterIndex];
    // Other threads only add links, never change the generation, so it can be read on its own.
    const std::uint32_t generation = generationOf(counter.state.load(std::memory_order_relaxed));
    const std::uint64_t closed =
        counter.state.exchange(packState(generation, kClosedList), std::memory_order_seq_cst);
    // Paired with the seq_cst store in wait(): either this load sees the waiter, or the waiter's next
    // isComplete() sees the counter closed and it does not sleep.
    const bool hasWaiter = counter.hasWaiter.load(std::memory_order_seq_cst);

    for (std::uint32_t link = listOf(closed); link != kEmptyList;) {
        const std::uint32_t linkIndex = link - 1;
        const detail::ContinuationLink& continuation = m_links[linkIndex];
        detail::Job& job = *continuation.job;
        link = continuation.next;
        m_links.release(linkIndex);
        if (job.unresolvedDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            enqueue(job);
        }
    }

    // A new generation turns every handle to this one into "complete", so the counter can be reused.
    counter.state.store(packState(generation + 1, kEmptyList), std::memory_order_release);
    m_counters.release(counterIndex);
    if (hasWaiter) {
        wakeAll();
    }
}

template <class Done, class BeforeSleep>
void JobSystem::helpUntil(Done done, BeforeSleep beforeSleep) {
    detail::Worker* const self = currentWorker();
    int idleRounds = 0;
    while (true) {
        // Read before looking for work: anything queued after this read changes the signal, so the
        // sleep below returns at once instead of missing it.
        const std::uint32_t observedSignal = m_signal.load(std::memory_order_seq_cst);
        if (done()) {
            return;
        }
        if (detail::Job* const job = findJob(self)) {
            execute(*job, self);
            idleRounds = 0;
            continue;
        }
        // Work that is visible but was not taken (another consumer held the global queue) is
        // retried; so are the first idle rounds, to absorb short gaps between bursts.
        if (hasVisibleWork() || ++idleRounds < kIdleRoundsBeforeSleep) {
            std::this_thread::yield();
            continue;
        }
        beforeSleep();
        if (done()) {
            return;
        }
        sleep(observedSignal, self);
        idleRounds = 0;
    }
}

void JobSystem::sleep(std::uint32_t observedSignal, detail::Worker* self) {
    // seq_cst on both sides: either a waker sees this sleeper, or the wait below sees the waker's
    // new signal and returns immediately.
    m_sleepers.fetch_add(1, std::memory_order_seq_cst);
    m_signal.wait(observedSignal, std::memory_order_seq_cst);
    m_sleepers.fetch_sub(1, std::memory_order_seq_cst);
    if (self != nullptr) {
        self->sleeps.fetch_add(1, std::memory_order_relaxed);
    }
}

void JobSystem::wakeOne() {
    m_signal.fetch_add(1, std::memory_order_seq_cst);
    if (m_sleepers.load(std::memory_order_seq_cst) != 0) {
        m_signal.notify_one();
    }
}

void JobSystem::wakeAll() {
    m_signal.fetch_add(1, std::memory_order_seq_cst);
    if (m_sleepers.load(std::memory_order_seq_cst) != 0) {
        m_signal.notify_all();
    }
}

void JobSystem::workerMain(detail::Worker& worker) {
    tCurrentWorker = &worker;
    helpUntil(
        [this] {
            return m_stopping.load(std::memory_order_seq_cst) &&
                   m_jobsInFlight.load(std::memory_order_seq_cst) == 0;
        },
        [] {});
    tCurrentWorker = nullptr;
}

} // namespace nx::jobs
