// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Typed events with a life of two ticks (docs/03-ecs.md, section 9).
export module nx.ecs:event;

import std;

export namespace nx::ecs {

template <class Type>
concept Event = std::is_object_v<Type> && !std::is_const_v<Type> &&
                std::is_nothrow_move_constructible_v<Type> && std::is_nothrow_destructible_v<Type>;

/// Double-buffered queue of one event type, stored as a world resource. Events written during
/// tick N stay readable until the end of tick N+1; World::advanceEvents() marks the tick boundary.
/// Every event gets a sequence number, so a reader never sees the same event twice.
template <Event Type>
class Events {
public:
    void send(Type event) { m_current.push_back(std::move(event)); }

    /// Ends a tick: the previous buffer is dropped and the current one takes its place.
    void advance() noexcept {
        m_previousFirst = m_currentFirst;
        m_currentFirst += m_current.size();
        std::swap(m_previous, m_current);
        m_current.clear();
    }

    /// Sequence number the next event will get.
    [[nodiscard]] auto nextSequence() const noexcept -> std::uint64_t {
        return m_currentFirst + m_current.size();
    }
    /// Sequence number of the oldest event still stored.
    [[nodiscard]] auto oldestSequence() const noexcept -> std::uint64_t { return m_previousFirst; }

    /// Calls `function` on every stored event with a sequence number from `first` on, oldest first,
    /// and returns the sequence number after the last one.
    template <class Function>
        requires std::invocable<Function&, const Type&>
    auto forEachFrom(std::uint64_t first, Function&& function) const -> std::uint64_t {
        visit(m_previous, m_previousFirst, first, function);
        visit(m_current, m_currentFirst, first, function);
        return nextSequence();
    }

private:
    template <class Function>
    static void visit(const std::vector<Type>& events, std::uint64_t firstSequence, std::uint64_t from,
                      Function& function) {
        const std::uint64_t skip = from > firstSequence ? from - firstSequence : 0;
        for (std::size_t index = static_cast<std::size_t>(std::min<std::uint64_t>(skip, events.size()));
             index < events.size(); ++index) {
            std::invoke(function, events[index]);
        }
    }

    std::vector<Type> m_previous;
    std::vector<Type> m_current;
    std::uint64_t m_previousFirst = 0; // sequence number of m_previous.front()
    std::uint64_t m_currentFirst = 0;  // sequence number of m_current.front()
};

/// System parameter that sends events of one type.
template <Event Type>
class EventWriter {
public:
    explicit EventWriter(Events<Type>& events) noexcept : m_events{&events} {}

    void send(Type event) { m_events->send(std::move(event)); }

private:
    Events<Type>* m_events;
};

/// System parameter that reads the events of one type sent since this system last read them.
template <Event Type>
class EventReader {
public:
    EventReader(const Events<Type>& events, std::uint64_t& cursor) noexcept
        : m_events{&events}, m_cursor{&cursor} {}

    /// Calls `function` on each unread event, oldest first, and marks them read.
    template <class Function>
        requires std::invocable<Function&, const Type&>
    void forEach(Function&& function) {
        *m_cursor = m_events->forEachFrom(*m_cursor, function);
    }

    [[nodiscard]] auto hasUnread() const noexcept -> bool {
        return m_events->nextSequence() > std::max(*m_cursor, m_events->oldestSequence());
    }

private:
    const Events<Type>* m_events;
    std::uint64_t* m_cursor;
};

} // namespace nx::ecs
