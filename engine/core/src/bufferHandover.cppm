// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A vector a producer fills and a consumer reads, the two taking turns over the storage instead of
/// copying it out: what is made every tick or every frame is read without allocating
/// (docs/02-coding-standards.md, section 7).
export module nx.core:bufferHandover;

import std;

export namespace nx {

/// A `T` vector that goes from its producer to its consumer and back, the storage kept.
///
/// The producer appends to the buffer it last got back (`bufferForProducer`); the consumer takes what
/// was appended since its last take (`handToConsumer`) and reads it until it takes again. Each take
/// swaps the two buffers: what the consumer read goes back to the producer, empty, with the room it
/// grew to, so that a repeated production appends without allocating once the buffer is large enough.
///
/// One producer and one consumer, never writing at once: the producer writes the buffer the consumer
/// is not reading, which a take is what puts in its hands.
template <class T>
class BufferHandover {
public:
    BufferHandover() = default;

    /// A handover of what `other` has not yet handed over: a copy is a producer of its own, and what
    /// its consumer read is the consumer's, not copied.
    BufferHandover(const BufferHandover&) = default;
    auto operator=(const BufferHandover&) -> BufferHandover& = default;
    BufferHandover(BufferHandover&&) noexcept = default;
    auto operator=(BufferHandover&&) noexcept -> BufferHandover& = default;
    ~BufferHandover() = default;

    /// The buffer to append to what the consumer is to take next: the one it last returned, empty and
    /// with the room it grew to.
    [[nodiscard]] auto bufferForProducer() noexcept -> std::vector<T>& { return produced; }

    /// Hands what the producer appended since the last take to the consumer, replacing what it read
    /// before, and returns it: empty when nothing was appended. The span is valid until the next take.
    [[nodiscard]] auto handToConsumer() noexcept -> std::span<const T> {
        consumed.swap(produced);
        produced.clear();
        return consumed;
    }

private:
    std::vector<T> produced; ///< What the producer appends to, until the consumer takes it.
    std::vector<T> consumed; ///< What the consumer reads, until it takes again.
};

} // namespace nx
