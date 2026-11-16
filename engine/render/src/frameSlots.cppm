// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a renderer writes its frames to (docs/05-rendering.md, section 7.1): a ring of slots, one for
/// each prepare the frames in flight may still be reading, each prepare writing the next, which the GPU
/// read as many prepares ago as the ring has slots, and the draws after it reading the one it wrote.
export module nx.render:frameSlots;

import std;
import nx.core;

export namespace nx::render {

/// Which of a renderer's prepares: the first is 1, and PrepareNumber{} is before the first. What a
/// renderer stamps on what a prepare looked at, to look at it once in that prepare (BspWorld's faces).
enum class PrepareNumber : std::uint64_t {};

/// A renderer's frame slots and its prepares so far (see the module documentation): `Slot` is what a
/// prepare writes for its frame in flight, the GPU's buffers and bind groups it draws with.
template <class Slot>
class FrameSlots {
public:
    /// `count` slots, each as Slot{} makes it, to fill before the first prepare. Requires at least one:
    /// after a violation under observe, the ring has one.
    explicit FrameSlots(std::size_t count) : ring(count) {
        expects(count > 0, "FrameSlots: at least one slot");
        if (ring.empty()) {
            ring.resize(1); // the handler let execution continue ("observe")
        }
    }

    /// Starts a prepare: the slot it writes, the one after the last prepare's.
    [[nodiscard]] auto advance() noexcept -> Slot& {
        const std::size_t index = std::to_underlying(latest) % ring.size();
        latest = PrepareNumber{std::to_underlying(latest) + 1};
        return ring[index];
    }

    /// The slot the last prepare wrote, which the draws after it read. Requires a prepare: after a
    /// violation under observe, the slot the first prepare writes.
    [[nodiscard]] auto prepared() noexcept -> Slot& { return ring[preparedIndex()]; }
    /// The slot the last prepare wrote, as prepared() above.
    [[nodiscard]] auto prepared() const noexcept -> const Slot& { return ring[preparedIndex()]; }

    /// The number of the last prepare, the one under way while a prepare runs.
    [[nodiscard]] auto lastPrepare() const noexcept -> PrepareNumber { return latest; }

    /// Every slot, in the order the prepares take them: to fill them before the first, or to make each
    /// write again what it keeps from a frame to the next.
    [[nodiscard]] auto begin() noexcept { return ring.begin(); }
    [[nodiscard]] auto end() noexcept { return ring.end(); }

private:
    [[nodiscard]] auto preparedIndex() const noexcept -> std::size_t {
        if (latest == PrepareNumber{}) {
            expects(false, "FrameSlots::prepared: after a prepare");
            return 0;
        }
        return (std::to_underlying(latest) - 1) % ring.size();
    }

    std::vector<Slot> ring;
    PrepareNumber latest{};
};

} // namespace nx::render
