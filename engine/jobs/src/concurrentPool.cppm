// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Lock-free pool of nodes addressed by 32-bit indices (docs/04-job-system.md, section 3.3): job
/// descriptors, counters and continuation links come from here, so the hot path never calls malloc
/// once the pool has warmed up.
export module nx.jobs:concurrentPool;

import std;
import nx.core;

export namespace nx::jobs {

/// Position of a node in a ConcurrentPool; valid for the lifetime of the pool.
enum class PoolIndex : std::uint32_t {};

/// A pool node that may be absent, as the lists threaded through nodes store it: `index + 1`, so
/// that None is 0. Pool indices stay below ConcurrentPool's kIndexLimit, which leaves the largest
/// value free for a sentinel of a list's own (a closed list).
enum class PoolLink : std::uint32_t {
    None = 0,
};

[[nodiscard]] constexpr auto linkTo(PoolIndex index) noexcept -> PoolLink {
    return PoolLink{std::to_underlying(index) + 1};
}

/// The node a link names; `link` must not be None.
[[nodiscard]] constexpr auto indexOf(PoolLink link) noexcept -> PoolIndex {
    expects(link != PoolLink::None, "indexOf: the link names no node");
    return PoolIndex{std::to_underlying(link) - 1};
}

/// A link and the 32-bit tag that versions it, packed in the one 64-bit word an atomic
/// compare-exchange replaces at once, so a change of either fails the exchange.
struct TaggedLink {
    std::uint32_t tag = 0;
    PoolLink link = PoolLink::None;

    [[nodiscard]] constexpr auto pack() const noexcept -> std::uint64_t {
        return (std::uint64_t{tag} << kTagShift) | std::to_underlying(link);
    }

    [[nodiscard]] static constexpr auto unpack(std::uint64_t word) noexcept -> TaggedLink {
        return TaggedLink{.tag = truncate<std::uint32_t>(word >> kTagShift),
                          .link = PoolLink{truncate<std::uint32_t>(word)}};
    }

private:
    static constexpr unsigned kTagShift = 32;
};

/// A pool node is default-constructible and carries the link the free list threads through it.
template <class Node>
concept PoolNode = std::default_initializable<Node> && requires(Node& node) {
    { node.nextFree } -> std::same_as<std::atomic<PoolLink>&>;
};

/// Nodes live in chunks that are allocated on first use and never move or shrink, so an index stays
/// valid (and a node stays readable) for the lifetime of the pool. Chunk sizes double (64, 128, 256,
/// ...), which covers the whole 32-bit index space with a few dozen chunk pointers.
///
/// Released nodes go to a Treiber stack whose head carries a tag, incremented on every change, so a
/// node that is taken and returned between a thread's read and its compare-exchange (ABA) cannot
/// corrupt the list. The tag is 32 bits: the list could only break for a thread stalled across
/// exactly a multiple of 2^32 changes between its read and its exchange. Nodes are recycled as they
/// are, without being destroyed and constructed again: the owner of a node resets the fields it
/// uses.
template <PoolNode Node>
class ConcurrentPool {
public:
    /// Indices stay below this bound, so linkTo() never reaches the largest link value.
    static constexpr std::uint32_t kIndexLimit = std::numeric_limits<std::uint32_t>::max() - 1;

    ConcurrentPool() = default;
    ConcurrentPool(const ConcurrentPool&) = delete;
    auto operator=(const ConcurrentPool&) -> ConcurrentPool& = delete;
    ConcurrentPool(ConcurrentPool&&) = delete;
    auto operator=(ConcurrentPool&&) -> ConcurrentPool& = delete;

    ~ConcurrentPool() {
        // relaxed: destruction happens after every thread that used the pool has stopped.
        for (auto& chunk : m_chunks) {
            const std::unique_ptr<Node[]> owned{chunk.load(std::memory_order_relaxed)};
        }
    }

    /// A node nobody else holds. Reuses released nodes first.
    [[nodiscard]] auto acquire() -> PoolIndex {
        // acquire: pairs with the release in release(), so the free node's nextFree is visible.
        auto head = TaggedLink::unpack(m_freeHead.load(std::memory_order_acquire));
        while (head.link != PoolLink::None) {
            const PoolIndex index = indexOf(head.link);
            // The node may be taken by another thread right now; the tag makes the exchange fail then.
            // relaxed: the acquire on the head already ordered the release that wrote it.
            const PoolLink next = (*this)[index].nextFree.load(std::memory_order_relaxed);
            std::uint64_t expected = head.pack();
            if (m_freeHead.compare_exchange_weak(expected,
                                                 TaggedLink{.tag = head.tag + 1, .link = next}.pack(),
                                                 std::memory_order_acquire, std::memory_order_acquire)) {
                return index;
            }
            head = TaggedLink::unpack(expected);
        }
        // relaxed: only uniqueness matters; the chunk publication below orders the node itself.
        const std::uint32_t fresh = m_freshIndex.fetch_add(1, std::memory_order_relaxed);
        invariant(fresh < kIndexLimit, "ConcurrentPool: the 32-bit index space is exhausted");
        ensureChunk(locate(PoolIndex{fresh}).chunk);
        return PoolIndex{fresh};
    }

    /// Returns a node acquired from this pool. The caller must not touch it afterwards.
    void release(PoolIndex index) noexcept {
        Node& node = (*this)[index];
        // relaxed load: the exchange below validates it; release on success publishes the node's
        // nextFree and everything its last owner wrote to the thread that acquires it next.
        auto head = TaggedLink::unpack(m_freeHead.load(std::memory_order_relaxed));
        std::uint64_t expected = head.pack();
        do {
            head = TaggedLink::unpack(expected);
            node.nextFree.store(head.link, std::memory_order_relaxed);
        } while (!m_freeHead.compare_exchange_weak(
            expected, TaggedLink{.tag = head.tag + 1, .link = linkTo(index)}.pack(),
            std::memory_order_release, std::memory_order_relaxed));
    }

    /// The node at `index`, which must have been acquired from this pool at least once.
    [[nodiscard]] auto operator[](PoolIndex index) const noexcept -> Node& {
        const auto [chunk, offset] = locate(index);
        // acquire: pairs with ensureChunk's publication of the chunk.
        return m_chunks[chunk].load(std::memory_order_acquire)[offset];
    }

    /// Distinct nodes created so far (live and released).
    [[nodiscard]] auto createdCount() const noexcept -> std::size_t {
        // relaxed: a statistic, ordered with nothing else.
        return m_freshIndex.load(std::memory_order_relaxed);
    }

private:
    struct Location {
        std::size_t chunk;
        std::size_t offset;
    };

    static constexpr unsigned kFirstChunkShift = 6; // the first chunk holds 64 nodes
    static constexpr std::size_t kChunkCount =
        std::numeric_limits<std::uint32_t>::digits - kFirstChunkShift + 1;

    [[nodiscard]] static constexpr auto chunkSize(std::size_t chunk) noexcept -> std::size_t {
        return std::size_t{1} << (chunk + kFirstChunkShift);
    }

    /// Chunk `c` starts at index 64 * (2^c - 1).
    [[nodiscard]] static constexpr auto locate(PoolIndex index) noexcept -> Location {
        const std::uint64_t position = std::to_underlying(index);
        const std::uint64_t scaled = (position >> kFirstChunkShift) + 1;
        const auto chunk = static_cast<std::size_t>(std::bit_width(scaled)) - 1;
        const std::uint64_t first = ((std::uint64_t{1} << chunk) - 1) << kFirstChunkShift;
        return {.chunk = chunk, .offset = static_cast<std::size_t>(position - first)};
    }

    void ensureChunk(std::size_t chunk) {
        if (m_chunks[chunk].load(std::memory_order_acquire) != nullptr) {
            return;
        }
        auto fresh = std::make_unique<Node[]>(chunkSize(chunk));
        Node* expected = nullptr;
        // acq_rel: publishes the new chunk's nodes, or acquires the chunk another thread installed.
        if (m_chunks[chunk].compare_exchange_strong(expected, fresh.get(), std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            std::ignore = fresh.release(); // owned by m_chunks from now on
        }
        // Otherwise another thread installed the chunk first and `fresh` is freed here.
    }

    /// The TaggedLink of the first free node.
    alignas(kCacheLineSize) std::atomic<std::uint64_t> m_freeHead{TaggedLink{}.pack()};
    alignas(kCacheLineSize) std::atomic<std::uint32_t> m_freshIndex{0};
    std::array<std::atomic<Node*>, kChunkCount> m_chunks{};
};

} // namespace nx::jobs
