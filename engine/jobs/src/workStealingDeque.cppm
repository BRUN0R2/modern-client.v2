// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Chase-Lev work-stealing deque (docs/04-job-system.md, section 2).
///
/// One owner thread pushes and pops at the bottom (newest first); any thread steals from the top
/// (oldest first). The algorithm follows Lê, Pop, Cohen and Zappa Nardelli, "Correct and Efficient
/// Work-Stealing for Weak Memory Models" (PPoPP 2013), with one deliberate change: the standalone
/// seq_cst fences of the paper are folded into seq_cst accesses to `top` and `bottom`. For this
/// algorithm the orderings are equivalent, and ThreadSanitizer, which does not model standalone
/// fences, can then check it.
export module nx.jobs:workStealingDeque;

import std;
import nx.core;

export namespace nx::jobs {

/// Elements live in atomics, so they must be trivially copyable and lock-free (pointers, indices).
template <class Type>
concept DequeElement = std::is_trivially_copyable_v<Type> && std::atomic<Type>::is_always_lock_free;

template <DequeElement Element>
class WorkStealingDeque {
public:
    static constexpr std::int64_t kDefaultCapacity = 256;

    WorkStealingDeque() : WorkStealingDeque{kDefaultCapacity} {}

    /// `initialCapacity` must be a power of two. The deque doubles when full, so a push never fails.
    explicit WorkStealingDeque(std::int64_t initialCapacity) {
        const bool isPowerOfTwo =
            initialCapacity > 0 && std::has_single_bit(static_cast<std::uint64_t>(initialCapacity));
        expects(isPowerOfTwo, "WorkStealingDeque: the initial capacity must be a power of two");
        // If the handler lets execution continue ("observe"), the deque stays usable.
        m_rings.push_back(std::make_unique<Ring>(isPowerOfTwo ? initialCapacity : kDefaultCapacity));
        m_ring.store(m_rings.back().get(), std::memory_order_relaxed);
    }

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    auto operator=(const WorkStealingDeque&) -> WorkStealingDeque& = delete;
    WorkStealingDeque(WorkStealingDeque&&) = delete;
    auto operator=(WorkStealingDeque&&) -> WorkStealingDeque& = delete;
    ~WorkStealingDeque() = default;

    /// Owner thread only.
    void push(Element element) {
        // Only the owner writes `bottom` and `m_ring`, so it reads them relaxed.
        const auto bottom = m_bottom.load(std::memory_order_relaxed);
        const auto top = m_top.load(std::memory_order_acquire);
        Ring* ring = m_ring.load(std::memory_order_relaxed);
        if (bottom - top >= ring->capacity()) {
            ring = grow(*ring, top, bottom);
        }
        ring->store(bottom, element);
        // Publishes the element: a thief that reads the new bottom also sees the slot.
        m_bottom.store(bottom + 1, std::memory_order_seq_cst);
    }

    /// Owner thread only. The newest element, or nullopt when the deque is empty.
    [[nodiscard]] auto pop() noexcept -> std::optional<Element> {
        const auto bottom = m_bottom.load(std::memory_order_relaxed) - 1;
        const Ring* const ring = m_ring.load(std::memory_order_relaxed);
        // Reserve the bottom slot before looking at `top`: this store-load pair must not be
        // reordered, or the owner and a thief could both take the last element.
        m_bottom.store(bottom, std::memory_order_seq_cst);
        auto top = m_top.load(std::memory_order_seq_cst);

        if (top > bottom) {
            m_bottom.store(bottom + 1, std::memory_order_seq_cst);
            return std::nullopt;
        }
        const Element element = ring->load(bottom);
        if (top < bottom) {
            return element; // more than one element left: no thief can reach this slot
        }
        // Last element: whoever advances `top` first takes it.
        const bool won =
            m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_seq_cst);
        m_bottom.store(bottom + 1, std::memory_order_seq_cst);
        return won ? std::optional<Element>{element} : std::nullopt;
    }

    /// Any thread. The oldest element, or nullopt when the deque is empty. Lock-free: a retry only
    /// happens after another thread took an element.
    [[nodiscard]] auto steal() noexcept -> std::optional<Element> {
        auto top = m_top.load(std::memory_order_seq_cst);
        while (true) {
            const auto bottom = m_bottom.load(std::memory_order_seq_cst);
            if (top >= bottom) {
                return std::nullopt;
            }
            // The slot is read before claiming it: once `top` moves, the owner may reuse the slot.
            const Ring* const ring = m_ring.load(std::memory_order_acquire);
            const Element element = ring->load(top);
            if (m_top.compare_exchange_weak(top, top + 1, std::memory_order_seq_cst,
                                            std::memory_order_seq_cst)) {
                return element;
            }
            // `top` now holds the value another thread left: try the next element.
        }
    }

    /// Element count at some recent moment; exact only while no other thread uses the deque.
    [[nodiscard]] auto sizeApprox() const noexcept -> std::size_t {
        const auto bottom = m_bottom.load(std::memory_order_seq_cst);
        const auto top = m_top.load(std::memory_order_seq_cst);
        return bottom > top ? static_cast<std::size_t>(bottom - top) : 0;
    }

    /// Owner thread only.
    [[nodiscard]] auto capacity() const noexcept -> std::int64_t {
        return m_ring.load(std::memory_order_relaxed)->capacity();
    }

private:
    /// Circular buffer addressed by the ever-growing `top`/`bottom` indices.
    class Ring {
    public:
        explicit Ring(std::int64_t capacity)
            : m_mask{capacity - 1},
              m_slots{std::make_unique<std::atomic<Element>[]>(static_cast<std::size_t>(capacity))} {}

        [[nodiscard]] auto capacity() const noexcept -> std::int64_t { return m_mask + 1; }

        void store(std::int64_t index, Element element) noexcept {
            m_slots[slotOf(index)].store(element, std::memory_order_relaxed);
        }

        [[nodiscard]] auto load(std::int64_t index) const noexcept -> Element {
            return m_slots[slotOf(index)].load(std::memory_order_relaxed);
        }

    private:
        [[nodiscard]] auto slotOf(std::int64_t index) const noexcept -> std::size_t {
            return static_cast<std::size_t>(index & m_mask);
        }

        std::int64_t m_mask;
        std::unique_ptr<std::atomic<Element>[]> m_slots;
    };

    auto grow(const Ring& ring, std::int64_t top, std::int64_t bottom) -> Ring* {
        invariant(ring.capacity() <= std::numeric_limits<std::int64_t>::max() / 2,
                  "WorkStealingDeque: capacity overflow");
        auto bigger = std::make_unique<Ring>(ring.capacity() * 2);
        for (auto index = top; index < bottom; ++index) {
            bigger->store(index, ring.load(index));
        }
        Ring* const published = bigger.get();
        // The old ring stays alive until the deque dies: a thief may still be reading it.
        m_rings.push_back(std::move(bigger));
        m_ring.store(published, std::memory_order_release);
        return published;
    }

    alignas(kCacheLineSize) std::atomic<std::int64_t> m_top{0};
    alignas(kCacheLineSize) std::atomic<std::int64_t> m_bottom{0};
    alignas(kCacheLineSize) std::atomic<Ring*> m_ring{nullptr};
    std::vector<std::unique_ptr<Ring>> m_rings; // current and retired rings, owner thread only
};

} // namespace nx::jobs
