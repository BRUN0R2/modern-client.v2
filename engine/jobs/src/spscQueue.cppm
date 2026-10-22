// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A queue between one producer thread and one consumer thread (docs/04-job-system.md, section 2):
/// a ring of a fixed capacity, lock-free and wait-free, whose push and pop neither allocate nor wait,
/// so that a real-time thread (the audio mixer's) may use either end.
export module nx.jobs:spscQueue;

import std;
import nx.core;

export namespace nx::jobs {

/// Why a queue refused an element.
enum class QueueError : std::uint8_t {
    Full, ///< It holds as many elements as it can; the element was not added.
};

/// A queue of at most `Capacity` elements, a power of two, from one producer thread to one consumer
/// thread. Elements are moved in and out; they must be default-constructible and movable without
/// throwing, and moving them must not allocate where a real-time thread moves them.
template <std::movable Element, std::size_t Capacity>
    requires(std::has_single_bit(Capacity) && std::is_nothrow_move_assignable_v<Element> &&
             std::is_nothrow_default_constructible_v<Element>)
class SpscQueue {
public:
    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    auto operator=(const SpscQueue&) -> SpscQueue& = delete;
    SpscQueue(SpscQueue&&) = delete;
    auto operator=(SpscQueue&&) -> SpscQueue& = delete;
    ~SpscQueue() = default;

    /// Adds `element` at the back, from the producer thread alone. Errors: Full, with no detail, so
    /// that a refusal does not allocate either.
    [[nodiscard]] auto push(Element element) noexcept -> Result<void, QueueError> {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_head.load(std::memory_order_acquire) == Capacity) {
            return fail(QueueError::Full); // an empty detail: nothing to allocate
        }
        m_elements[tail & kMask] = std::move(element);
        m_tail.store(tail + 1, std::memory_order_release);
        return {};
    }

    /// The element at the front, taken out, from the consumer thread alone; none when it is empty.
    [[nodiscard]] auto pop() noexcept -> std::optional<Element> {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        std::optional<Element> taken{std::move(m_elements[head & kMask])};
        m_head.store(head + 1, std::memory_order_release);
        return taken;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    /// Apart, so that the two threads do not share a cache line.
    static constexpr std::size_t kCacheLine = 64;

    std::array<Element, Capacity> m_elements{};
    alignas(kCacheLine) std::atomic<std::size_t> m_head{0}; ///< The consumer's: the next to take.
    alignas(kCacheLine) std::atomic<std::size_t> m_tail{0}; ///< The producer's: the next to fill.
};

} // namespace nx::jobs
