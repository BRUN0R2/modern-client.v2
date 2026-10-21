// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Vocabulary of the Job System (docs/04-job-system.md, section 3): priorities, labels, handles,
/// descriptors, granularity and the context a job runs with.
export module nx.jobs:job;

import std;
import nx.core;
import :concurrentPool;
import :intrusiveQueue;

export namespace nx::jobs {

class JobSystem;

/// Urgency of a job. A thread looking for work tries the priorities from the most urgent down, and
/// takes a job of some priority only when it found none of a more urgent one: none in its own queue,
/// in the global queue (unless another thread was taking from it at that moment) or in what it
/// could steal.
enum class Priority : std::uint8_t {
    Critical,   ///< The current frame or tick.
    Normal,     ///< Work due soon, but not on the frame's critical path.
    Low,        ///< Streaming, compression.
    Background, ///< Precomputation, caches.
    Count,
};

namespace detail {

/// Not constexpr on purpose: reaching it during constant evaluation is the compile error.
inline void jobLabelMustNotBeEmpty() noexcept {}

} // namespace detail

/// Name of a job for the profiler and for debugging. It can only be built from a constant
/// expression (a string literal, a type name), so it always outlives the job that carries it.
class JobLabel {
public:
    /// From a string literal. Implicit, so that descriptors read `{.label = "physics"}`.
    consteval JobLabel(const char* text) : JobLabel{std::string_view{text}} {}

    /// From a constant string; an empty one does not compile.
    consteval JobLabel(std::string_view text) : m_text{text} {
        if (text.empty()) {
            detail::jobLabelMustNotBeEmpty();
        }
    }

    /// The name, as given.
    [[nodiscard]] constexpr auto text() const noexcept -> std::string_view { return m_text; }

private:
    std::string_view m_text;
};

/// Refers to the completion of a submitted job (or of all the batches of a parallelFor). A handle
/// is a small value: copying it is free and keeping it after completion is harmless, because the
/// counter it names carries a generation that changes when the counter is reused. Generations are
/// 32 bits: a handle kept while its counter is reused 2^32 times reads its newest incarnation.
class JobHandle {
public:
    /// An invalid handle; waiting on it is a contract violation.
    constexpr JobHandle() noexcept = default;

    /// Whether the handle names a job (it came from submit or parallelFor).
    [[nodiscard]] constexpr auto isValid() const noexcept -> bool { return m_counter != kInvalidCounter; }

    // A member, not a hidden friend: GCC 16 crashes on defaulted friend comparisons from modules
    // (docs/17-build-and-tools.md, section 2.2).
    [[nodiscard]] constexpr auto operator==(const JobHandle&) const noexcept -> bool = default;

private:
    friend class JobSystem;

    /// No pool index reaches it (ConcurrentPool::kIndexLimit).
    static constexpr PoolIndex kInvalidCounter{std::numeric_limits<std::uint32_t>::max()};

    constexpr JobHandle(PoolIndex counter, std::uint32_t generation) noexcept
        : m_counter{counter}, m_generation{generation} {}

    PoolIndex m_counter = kInvalidCounter;
    std::uint32_t m_generation = 0;
};

/// How to submit a job.
struct JobDesc {
    JobLabel label;                       ///< Required: every job is named (docs/04, section 7).
    Priority priority = Priority::Normal; ///< How urgently threads take the job.
    /// The job starts only after every one of these has completed. The span is read during the
    /// submit call only.
    std::span<const JobHandle> dependsOn = {};
};

/// Batches per thread that runs jobs under Grain::automatic().
inline constexpr std::size_t kBatchesPerThread = 4;

/// How parallelFor splits a range into batches (one job per batch).
class Grain {
public:
    /// About kBatchesPerThread batches per thread that runs jobs, each with at least
    /// `minimumBatchSize` elements. The split depends on the worker count: use fixed() when the
    /// result of each batch (a partial sum, for instance) must be the same on every machine.
    [[nodiscard]] static constexpr auto automatic(std::size_t minimumBatchSize = 1) noexcept -> Grain {
        return Grain{Kind::Automatic, std::max(minimumBatchSize, std::size_t{1})};
    }

    /// Exactly `batchSize` elements per batch (the last batch may be shorter). `batchSize` must be at
    /// least 1.
    [[nodiscard]] static constexpr auto fixed(std::size_t batchSize) noexcept -> Grain {
        expects(batchSize > 0, "Grain::fixed: a batch needs at least one element");
        return Grain{Kind::Fixed, std::max(batchSize, std::size_t{1})};
    }

    /// Elements per batch for `elementCount` elements and `threadCount` threads running jobs.
    [[nodiscard]] constexpr auto batchSize(std::size_t elementCount, std::size_t threadCount) const noexcept
        -> std::size_t {
        if (m_kind == Kind::Fixed) {
            return m_size;
        }
        const std::size_t targetBatches = std::max(threadCount, std::size_t{1}) * kBatchesPerThread;
        return std::max(divideRoundingUp(elementCount, targetBatches), m_size);
    }

private:
    enum class Kind : std::uint8_t {
        Automatic,
        Fixed,
    };

    constexpr Grain(Kind kind, std::size_t size) noexcept : m_kind{kind}, m_size{size} {}

    Kind m_kind;
    std::size_t m_size; // minimum (Automatic) or exact (Fixed) batch size
};

/// What a running job can reach. Valid only during the job's execution.
class JobContext {
public:
    JobContext(const JobContext&) = delete;
    auto operator=(const JobContext&) -> JobContext& = delete;
    JobContext(JobContext&&) = delete;
    auto operator=(JobContext&&) -> JobContext& = delete;
    ~JobContext() = default;

    /// The system running the job, to submit and wait on further jobs.
    [[nodiscard]] inline auto jobSystem() const noexcept -> JobSystem& { return *m_system; }

    /// The label the job was submitted with.
    [[nodiscard]] inline auto label() const noexcept -> std::string_view { return m_label; }

private:
    friend class JobSystem;

    inline JobContext(JobSystem& system, std::string_view label) noexcept
        : m_system{&system}, m_label{label} {}

    JobSystem* m_system;
    std::string_view m_label;
};

/// A job body: `void(JobContext&)` or `void()`. Jobs report results through the data they write, so
/// a body that returns a value (an ignored Result, for instance) does not compile.
template <class Function>
concept JobFunction = (std::invocable<Function&, JobContext&> &&
                       std::is_void_v<std::invoke_result_t<Function&, JobContext&>>) ||
                      (std::invocable<Function&> && std::is_void_v<std::invoke_result_t<Function&>>);

/// A parallelFor body: `void(JobContext&, Element)` or `void(Element)`.
template <class Function, class Element>
concept ElementFunction =
    (std::invocable<Function&, JobContext&, Element> &&
     std::is_void_v<std::invoke_result_t<Function&, JobContext&, Element>>) ||
    (std::invocable<Function&, Element> && std::is_void_v<std::invoke_result_t<Function&, Element>>);

/// Bytes a job keeps inline for its captures. Larger data goes by reference or through an arena.
inline constexpr std::size_t kJobCaptureCapacity = 96;
/// Strictest alignment a capture may need.
inline constexpr std::size_t kJobCaptureAlignment = 16;

namespace detail {

template <class Function>
void invokeJob(Function& function, JobContext& context) {
    if constexpr (std::invocable<Function&, JobContext&>) {
        std::invoke(function, context);
    } else {
        std::invoke(function);
    }
}

template <class Function, class Element>
void invokeElement(Function& function, JobContext& context, Element&& element) {
    if constexpr (std::invocable<Function&, JobContext&, Element>) {
        std::invoke(function, context, std::forward<Element>(element));
    } else {
        std::invoke(function, std::forward<Element>(element));
    }
}

/// Type-erased job body stored inside the job descriptor: no allocation per job.
class InlineJobFunction {
public:
    template <class Function>
    void emplace(Function&& function) {
        using Callable = std::decay_t<Function>;
        static_assert(sizeof(Callable) <= kJobCaptureCapacity,
                      "the job captures more than nx::jobs::kJobCaptureCapacity bytes: capture large data by "
                      "reference, or pass it through an arena");
        static_assert(alignof(Callable) <= kJobCaptureAlignment,
                      "the job captures an object aligned beyond nx::jobs::kJobCaptureAlignment");
        static_assert(std::is_nothrow_destructible_v<Callable>, "job captures must not throw on destruction");

        std::construct_at(static_cast<Callable*>(static_cast<void*>(m_storage.data())),
                          std::forward<Function>(function));
        m_invoke = [](void* callable, JobContext& context) {
            invokeJob(*std::launder(static_cast<Callable*>(callable)), context);
        };
        m_destroy = [](void* callable) noexcept {
            std::destroy_at(std::launder(static_cast<Callable*>(callable)));
        };
    }

    inline void operator()(JobContext& context) { m_invoke(m_storage.data(), context); }

    /// Destroys the captures. Called right after the body returns, before the job counts as done.
    inline void reset() noexcept {
        if (m_destroy != nullptr) {
            m_destroy(m_storage.data());
        }
        m_invoke = nullptr;
        m_destroy = nullptr;
    }

private:
    alignas(kJobCaptureAlignment) std::array<std::byte, kJobCaptureCapacity> m_storage{};
    void (*m_invoke)(void* callable, JobContext& context) = nullptr;
    void (*m_destroy)(void* callable) noexcept = nullptr;
};

/// The body of one parallelFor batch: elements [begin, end) of the range that starts at `first`.
template <class Iterator, class Function>
struct BatchJob {
    Iterator first;
    std::size_t begin;
    std::size_t end;
    Function function;

    void operator()(JobContext& context) {
        using Difference = std::iter_difference_t<Iterator>;
        for (std::size_t index = begin; index < end; ++index) {
            invokeElement(function, context, first[static_cast<Difference>(index)]);
        }
    }
};

/// A submitted job. Lives in the JobSystem's pool from submit until it has run.
struct alignas(kCacheLineSize) Job : QueueLink {
    InlineJobFunction function;
    std::string_view label;
    /// Dependencies not yet complete, plus one while the job is still being submitted.
    std::atomic<std::size_t> unresolvedDependencies{0};
    PoolIndex counter{}; // completion counter this job decrements
    PoolIndex poolIndex{};
    std::atomic<PoolLink> nextFree{PoolLink::None};
    Priority priority = Priority::Normal;
};

/// Completion counter behind a JobHandle.
struct alignas(kCacheLineSize) Counter {
    /// A TaggedLink: the generation as its tag, and the newest continuation link (None when there
    /// is none, kClosedContinuations once the counter completed). One word, so that registering a
    /// continuation fails atomically when the counter completes or is reused.
    std::atomic<std::uint64_t> state{TaggedLink{}.pack()};
    std::atomic<std::uint64_t> pendingJobs{0};
    /// Set by a thread about to sleep in wait(): completion must then wake the sleepers.
    std::atomic<bool> hasWaiter{false};
    std::atomic<PoolLink> nextFree{PoolLink::None};
};

/// The continuation list of a completed counter: closed, so no continuation joins it.
inline constexpr PoolLink kClosedContinuations{std::numeric_limits<std::uint32_t>::max()};

/// "Run `job` once this counter completes."
struct ContinuationLink {
    Job* job = nullptr;
    PoolLink next = PoolLink::None; // the next link in the list
    std::atomic<PoolLink> nextFree{PoolLink::None};
};

} // namespace detail

} // namespace nx::jobs
