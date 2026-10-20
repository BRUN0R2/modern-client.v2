// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.core;

import std;

namespace nx {

FrameArena::FrameArena(std::size_t capacity)
    : m_storage{std::make_unique_for_overwrite<std::byte[]>(capacity)}, m_capacity{capacity} {}

FrameArena::FrameArena(FrameArena&& other) noexcept
    : m_storage{std::move(other.m_storage)}, m_capacity{std::exchange(other.m_capacity, 0)},
      m_used{std::exchange(other.m_used, 0)} {}

auto FrameArena::operator=(FrameArena&& other) noexcept -> FrameArena& {
    m_storage = std::move(other.m_storage);
    m_capacity = std::exchange(other.m_capacity, 0);
    m_used = std::exchange(other.m_used, 0);
    return *this;
}

auto FrameArena::allocateBytes(std::size_t size, std::size_t alignment, std::source_location where)
    -> Result<std::span<std::byte>, ArenaError> {
    if (!std::has_single_bit(alignment)) {
        // A bug in the caller: report it, then fail safely if the handler lets execution continue.
        expects(false, "FrameArena: alignment must be a power of two", where);
        return fail(ArenaError::InvalidAlignment, std::format("alignment {}", alignment), where);
    }

    if (size == 0) {
        return std::span<std::byte>{};
    }

    void* cursor = m_storage.get() + m_used;
    std::size_t space = remaining();
    if (std::align(alignment, size, cursor, space) == nullptr) {
        return fail(ArenaError::Exhausted,
                    std::format("requested {} bytes aligned to {}, {} of {} bytes left", size, alignment,
                                remaining(), m_capacity),
                    where);
    }

    auto* const first = static_cast<std::byte*>(cursor);
    m_used = static_cast<std::size_t>(first - m_storage.get()) + size;
    return std::span<std::byte>{first, size};
}

} // namespace nx
