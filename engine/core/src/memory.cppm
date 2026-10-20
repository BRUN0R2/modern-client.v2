// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Allocators for memory with a known, short lifetime (docs/02-coding-standards.md, section 7).
export module nx.core:memory;

import std;
import :contracts;
import :failure;

export namespace nx {

/// Alignment that keeps data written by different threads on different cache lines (no false
/// sharing). 64 bytes on x86-64 and on the common ARM64 cores. The value affects performance only,
/// never correctness. std::hardware_destructive_interference_size is not used because GCC warns that
/// its value may change between compiler versions, which would change the layout of exported types.
inline constexpr std::size_t kCacheLineSize = 64;

/// `offset` rounded up to the next multiple of `alignment`, which must be a power of two.
[[nodiscard]] constexpr auto alignUp(std::size_t offset, std::size_t alignment) noexcept -> std::size_t {
    expects(std::has_single_bit(alignment), "alignUp: the alignment must be a power of two");
    return (offset + alignment - 1) & ~(alignment - 1);
}

/// Why an arena could not allocate.
enum class ArenaError : std::uint8_t {
    Exhausted, ///< Not enough room left before the next reset.
};

/// Linear allocator for data that lives until the next reset (one frame, one job, one load
/// step). Allocation is a pointer bump; reset releases everything at once. Objects are never
/// destroyed individually, so only trivially destructible types may be allocated.
class FrameArena {
public:
    /// Reserves `capacity` bytes once, up front; the arena never grows.
    explicit FrameArena(std::size_t capacity);

    FrameArena(const FrameArena&) = delete;
    auto operator=(const FrameArena&) -> FrameArena& = delete;
    FrameArena(FrameArena&& other) noexcept;
    auto operator=(FrameArena&& other) noexcept -> FrameArena&;
    ~FrameArena() = default;

    /// `size` bytes aligned to `alignment`, which must be a power of two. Error: Exhausted.
    [[nodiscard]] auto allocateBytes(std::size_t size, std::size_t alignment,
                                     std::source_location where = std::source_location::current())
        -> Result<std::span<std::byte>, ArenaError>;

    /// `count` value-initialized objects of type T. Error: Exhausted.
    template <class T>
        requires std::is_trivially_destructible_v<T> && std::is_default_constructible_v<T>
    [[nodiscard]] auto allocate(std::size_t count,
                                std::source_location where = std::source_location::current())
        -> Result<std::span<T>, ArenaError> {
        if (count == 0) {
            return std::span<T>{};
        }
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            return fail(ArenaError::Exhausted,
                        std::format("{} objects of {} bytes overflow the size type", count, sizeof(T)),
                        where);
        }
        return allocateBytes(count * sizeof(T), alignof(T), where)
            .transform([count](std::span<std::byte> bytes) {
                void* const storage = bytes.data();
                auto* const first = static_cast<T*>(storage);
                std::uninitialized_value_construct_n(first, count);
                return std::span<T>{std::launder(first), count};
            });
    }

    /// Releases every allocation. Spans handed out before the reset must not be used afterwards.
    inline void reset() noexcept { m_used = 0; }

    [[nodiscard]] inline auto capacity() const noexcept -> std::size_t { return m_capacity; }
    [[nodiscard]] inline auto used() const noexcept -> std::size_t { return m_used; }
    [[nodiscard]] inline auto remaining() const noexcept -> std::size_t { return m_capacity - m_used; }

private:
    std::unique_ptr<std::byte[]> m_storage;
    std::size_t m_capacity = 0;
    std::size_t m_used = 0;
};

} // namespace nx
