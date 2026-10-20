// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Generational handles to GPU resources (docs/05-rendering.md, section 3.2): a destroyed
/// resource's handle never names another, and using one is caught where contracts are enforced.
export module nx.rhi:handles;

import std;
import nx.core;

export namespace nx::rhi {

/// A resource of the kind `Tag`, by slot and generation. Devices issue them; a handle made up by
/// hand names nothing a device knows.
template <class Tag>
class Handle {
public:
    static constexpr std::uint32_t kNullIndex = std::numeric_limits<std::uint32_t>::max();

    /// The null handle, which names no resource.
    constexpr Handle() noexcept = default;
    constexpr Handle(std::uint32_t index, std::uint32_t generation) noexcept
        : m_index{index}, m_generation{generation} {}

    [[nodiscard]] constexpr auto index() const noexcept -> std::uint32_t { return m_index; }
    [[nodiscard]] constexpr auto generation() const noexcept -> std::uint32_t { return m_generation; }
    [[nodiscard]] constexpr auto isNull() const noexcept -> bool { return m_index == kNullIndex; }

    [[nodiscard]] constexpr auto operator==(const Handle&) const noexcept -> bool = default;

private:
    std::uint32_t m_index = kNullIndex;
    std::uint32_t m_generation = 0;
};

struct TextureTag;
using TextureHandle = Handle<TextureTag>;

/// The resources of one kind a backend issues handles for. Freed slots are reused with a new
/// generation; a slot whose generation reaches its limit is retired, so a stale handle never names
/// another resource.
template <class Tag, class Value>
class HandlePool {
public:
    [[nodiscard]] inline auto insert(Value value) -> Handle<Tag> {
        if (!m_free.empty()) {
            const std::uint32_t index = m_free.back();
            m_free.pop_back();
            Slot& slot = m_slots[index];
            slot.value.emplace(std::move(value));
            return Handle<Tag>{index, slot.generation};
        }
        const auto index = narrow<std::uint32_t>(m_slots.size());
        expects(index != Handle<Tag>::kNullIndex, "HandlePool::insert: every index is taken");
        m_slots.push_back(Slot{.value = std::move(value), .generation = 0});
        return Handle<Tag>{index, 0};
    }

    /// The resource of `handle`; null for the null handle and for a resource that was removed.
    [[nodiscard]] inline auto find(Handle<Tag> handle) noexcept -> Value* {
        if (handle.index() >= m_slots.size()) {
            return nullptr;
        }
        Slot& slot = m_slots[handle.index()];
        return slot.generation == handle.generation() && slot.value ? &*slot.value : nullptr;
    }

    /// The resource of `handle`, which must name one.
    [[nodiscard]] inline auto get(Handle<Tag> handle) noexcept -> Value& {
        Value* value = find(handle);
        expects(value != nullptr, "HandlePool::get: the handle names no resource (null, or removed)");
        return *value;
    }

    /// Takes the resource of `handle` out of the pool, for its owner to destroy; nullopt when the
    /// handle names none.
    inline auto remove(Handle<Tag> handle) -> std::optional<Value> {
        if (find(handle) == nullptr) {
            return std::nullopt;
        }
        Slot& slot = m_slots[handle.index()];
        std::optional<Value> value = std::exchange(slot.value, std::nullopt);
        if (slot.generation + 1 < kGenerationLimit) {
            ++slot.generation;
            m_free.push_back(handle.index());
        } else {
            ++m_retired;
        }
        return value;
    }

    /// The number of resources in the pool.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t {
        return m_slots.size() - m_free.size() - m_retired;
    }

private:
    /// Generations run from 0 to kGenerationLimit - 1.
    static constexpr std::uint32_t kGenerationLimit = std::numeric_limits<std::uint32_t>::max();

    struct Slot {
        std::optional<Value> value;
        std::uint32_t generation = 0;
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_free;
    std::size_t m_retired = 0;
};

} // namespace nx::rhi
