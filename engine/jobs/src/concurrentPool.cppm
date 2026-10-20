// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Lock-free pool of nodes addressed by 32-bit indices (docs/04-job-system.md, section 3.3): job
/// descriptors, counters and continuation links come from here, so the hot path never calls malloc
/// once the pool has warmed up.
export module nx.jobs:concurrentPool;

import std;
import nx.core;

export namespace nx::jobs {

/// A pool node is default-constructible and carries the link the free list threads through it.
template <class Node>
concept PoolNode = std::default_initializable<Node> && requires(Node& node) {
    { node.nextFree } -> std::same_as<std::atomic<std::uint32_t>&>;
};

/// Nodes live in chunks that are allocated on first use and never move or shrink, so an index stays
/// valid (and a node stays readable) for the lifetime of the pool. Chunk sizes double (64, 128, 256,
/// ...), which covers the whole 32-bit index space with a few dozen chunk pointers.
///
/// Released nodes go to a Treiber stack whose head carries a tag, incremented on every change, so a
/// node that is taken and returned between a thread's read and its compare-exchange (ABA) cannot
/// corrupt the list. Nodes are recycled as they are, without being destroyed and constructed again:
/// the owner of a node resets the fields it uses.
template <PoolNode Node>
class ConcurrentPool {
public:
    using Index = std::uint32_t;

    /// Indices stay below this bound, so `index + 1` never reaches the largest Index value, which
    /// callers may use as a sentinel.
    static constexpr Index kIndexLimit = std::numeric_limits<Index>::max() - 1;

    ConcurrentPool() = default;
    ConcurrentPool(const ConcurrentPool&) = delete;
    auto operator=(const ConcurrentPool&) -> ConcurrentPool& = delete;
    ConcurrentPool(ConcurrentPool&&) = delete;
    auto operator=(ConcurrentPool&&) -> ConcurrentPool& = delete;

    ~ConcurrentPool() {
        for (auto& chunk : m_chunks) {
            const std::unique_ptr<Node[]> owned{chunk.load(std::memory_order_relaxed)};
        }
    }

    /// A node nobody else holds. Reuses released nodes first.
    [[nodiscard]] auto acquire() -> Index {
        auto head = m_freeHead.load(std::memory_order_acquire);
        while (linkOf(head) != kEndOfList) {
            const Index index = linkOf(head) - 1;
            // The node may be taken by another thread right now; the tag makes the exchange fail then.
            const Index next = (*this)[index].nextFree.load(std::memory_order_relaxed);
            if (m_freeHead.compare_exchange_weak(head, pack(tagOf(head) + 1, next), std::memory_order_acquire,
                                                 std::memory_order_acquire)) {
                return index;
            }
        }
        const Index fresh = m_freshIndex.fetch_add(1, std::memory_order_relaxed);
        invariant(fresh < kIndexLimit, "ConcurrentPool: the 32-bit index space is exhausted");
        ensureChunk(locate(fresh).chunk);
        return fresh;
    }

    /// Returns a node acquired from this pool. The caller must not touch it afterwards.
    void release(Index index) noexcept {
        Node& node = (*this)[index];
        auto head = m_freeHead.load(std::memory_order_relaxed);
        do {
            node.nextFree.store(linkOf(head), std::memory_order_relaxed);
        } while (!m_freeHead.compare_exchange_weak(head, pack(tagOf(head) + 1, index + 1),
                                                   std::memory_order_release, std::memory_order_relaxed));
    }

    /// The node at `index`, which must have been acquired from this pool at least once.
    [[nodiscard]] auto operator[](Index index) const noexcept -> Node& {
        const auto [chunk, offset] = locate(index);
        return m_chunks[chunk].load(std::memory_order_acquire)[offset];
    }

    /// Distinct nodes created so far (live and released).
    [[nodiscard]] auto createdCount() const noexcept -> std::size_t {
        return m_freshIndex.load(std::memory_order_relaxed);
    }

private:
    struct Location {
        std::size_t chunk;
        std::size_t offset;
    };

    static constexpr unsigned kFirstChunkShift = 6; // the first chunk holds 64 nodes
    static constexpr std::size_t kChunkCount = std::numeric_limits<Index>::digits - kFirstChunkShift + 1;
    static constexpr Index kEndOfList = 0; // free-list links store `index + 1`
    static constexpr unsigned kTagShift = std::numeric_limits<Index>::digits;

    [[nodiscard]] static constexpr auto chunkSize(std::size_t chunk) noexcept -> std::size_t {
        return std::size_t{1} << (chunk + kFirstChunkShift);
    }

    /// Chunk `c` starts at index 64 * (2^c - 1).
    [[nodiscard]] static constexpr auto locate(Index index) noexcept -> Location {
        const std::uint64_t scaled = (std::uint64_t{index} >> kFirstChunkShift) + 1;
        const auto chunk = static_cast<std::size_t>(std::bit_width(scaled)) - 1;
        const std::uint64_t first = ((std::uint64_t{1} << chunk) - 1) << kFirstChunkShift;
        return {.chunk = chunk, .offset = static_cast<std::size_t>(index - first)};
    }

    [[nodiscard]] static constexpr auto pack(std::uint32_t tag, Index link) noexcept -> std::uint64_t {
        return (std::uint64_t{tag} << kTagShift) | link;
    }
    [[nodiscard]] static constexpr auto tagOf(std::uint64_t head) noexcept -> std::uint32_t {
        return truncate<std::uint32_t>(head >> kTagShift);
    }
    [[nodiscard]] static constexpr auto linkOf(std::uint64_t head) noexcept -> Index {
        return truncate<Index>(head);
    }

    void ensureChunk(std::size_t chunk) {
        if (m_chunks[chunk].load(std::memory_order_acquire) != nullptr) {
            return;
        }
        auto fresh = std::make_unique<Node[]>(chunkSize(chunk));
        Node* expected = nullptr;
        if (m_chunks[chunk].compare_exchange_strong(expected, fresh.get(), std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            std::ignore = fresh.release(); // owned by m_chunks from now on
        }
        // Otherwise another thread installed the chunk first and `fresh` is freed here.
    }

    /// Tag (high 32 bits) and `index + 1` of the first free node (low 32 bits; 0 = none).
    alignas(kCacheLineSize) std::atomic<std::uint64_t> m_freeHead{0};
    alignas(kCacheLineSize) std::atomic<Index> m_freshIndex{0};
    std::array<std::atomic<Node*>, kChunkCount> m_chunks{};
};

} // namespace nx::jobs
