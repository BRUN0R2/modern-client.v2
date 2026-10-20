// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Unbounded FIFO queue of intrusive nodes for submissions from threads that are not workers
/// (docs/04-job-system.md, section 2: the global queue).
///
/// Producers never wait: a push is one atomic exchange (Dmitry Vyukov's intrusive MPSC queue).
/// Consumers take turns through a try-lock: a consumer that finds another one active gets nothing
/// back at once instead of waiting, and the scheduler looks for work elsewhere.
export module nx.jobs:intrusiveQueue;

import std;
import nx.core;

export namespace nx::jobs {

/// Link embedded in every node that can sit in an IntrusiveQueue.
struct QueueLink {
    std::atomic<QueueLink*> next{nullptr};
};

/// The queue itself: nodes in push order, linked through their QueueLink.
template <class Node>
    requires std::derived_from<Node, QueueLink>
class IntrusiveQueue {
public:
    IntrusiveQueue() noexcept = default;
    IntrusiveQueue(const IntrusiveQueue&) = delete;
    auto operator=(const IntrusiveQueue&) -> IntrusiveQueue& = delete;
    IntrusiveQueue(IntrusiveQueue&&) = delete;
    auto operator=(IntrusiveQueue&&) -> IntrusiveQueue& = delete;
    ~IntrusiveQueue() = default;

    /// Any thread. The node must stay alive and unused until it is popped.
    void push(Node& node) noexcept { pushLink(node); }

    /// Any thread. The oldest node, or nullptr when the queue is empty, another consumer is active,
    /// or the next node is still being linked by its producer. The last two cases end on their own,
    /// so callers that find hasVisibleNodes() true simply try again later.
    [[nodiscard]] auto tryPop() noexcept -> Node* {
        // acquire/release: the try-lock that gives m_tail one user at a time.
        if (m_consuming.exchange(true, std::memory_order_acquire)) {
            return nullptr;
        }
        QueueLink* const link = popExclusive();
        m_consuming.store(false, std::memory_order_release);
        // Every link except the stub is the base of a Node.
        return link != nullptr ? static_cast<Node*>(link) : nullptr;
    }

    /// Whether the newest link is a node rather than the internal stub: true while nodes wait,
    /// except during the moment a consumer takes the last node and puts the stub back, when a node
    /// pushed meanwhile stays queued behind the stub unseen. A scheduler may treat false as "no
    /// work" only because every push is followed by a wake-up its sleepers watch (docs/04-job-system.md,
    /// section 2), so that node is never missed, only noticed one wake-up later.
    [[nodiscard]] auto hasVisibleNodes() const noexcept -> bool {
        // acquire: pairs with the producers' exchange, as any reader of the head does.
        return m_head.load(std::memory_order_acquire) != &m_stub;
    }

private:
    void pushLink(QueueLink& link) noexcept {
        // relaxed: the acq_rel exchange below orders it before the node becomes reachable.
        link.next.store(nullptr, std::memory_order_relaxed);
        // acq_rel: release publishes `link` (and its node) to the next producer, which links after
        // it; acquire makes `previous` safe to write through.
        QueueLink* const previous = m_head.exchange(&link, std::memory_order_acq_rel);
        // Between the exchange and this store the queue is briefly cut: consumers see the nodes
        // before `link` but not `link` itself until the producer finishes. release: pairs with the
        // consumer's acquire load of `next`, which then sees the whole node.
        previous->next.store(&link, std::memory_order_release);
    }

    /// Runs with m_consuming held, so m_tail has a single user at a time. Its acquire loads pair
    /// with the release stores of pushLink.
    auto popExclusive() noexcept -> QueueLink* {
        QueueLink* tail = m_tail;
        QueueLink* next = tail->next.load(std::memory_order_acquire);
        if (tail == &m_stub) {
            if (next == nullptr) {
                return nullptr; // empty
            }
            m_tail = next;
            tail = next;
            next = next->next.load(std::memory_order_acquire);
        }
        if (next != nullptr) {
            m_tail = next;
            return tail;
        }
        if (tail != m_head.load(std::memory_order_acquire)) {
            return nullptr; // a producer has not linked its node yet
        }
        // `tail` is the last node: put the stub behind it so `tail` can leave the queue.
        pushLink(m_stub);
        next = tail->next.load(std::memory_order_acquire);
        if (next != nullptr) {
            m_tail = next;
            return tail;
        }
        return nullptr; // another producer slipped in before the stub; it finishes shortly
    }

    alignas(kCacheLineSize) std::atomic<QueueLink*> m_head{&m_stub}; // newest link, written by producers
    alignas(kCacheLineSize) QueueLink* m_tail{&m_stub};              // oldest link, consumers only
    std::atomic<bool> m_consuming{false};
    QueueLink m_stub;
};

} // namespace nx::jobs
