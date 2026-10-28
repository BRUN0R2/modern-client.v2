// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Random numbers the game can draw again (docs/07-simulation-and-physics.md, section 6): a stream
/// named for what it draws, seeded by what the server and the client's prediction both know (a
/// command's number), so that both draw the same. SplitMix64 (Steele, Lea and Flood, "Fast
/// splittable pseudorandom number generators", OOPSLA 2014): whole-number arithmetic only, so the
/// same numbers on every platform, and in constant expressions.
export module nx.core:random;

import std;
import :hash;

export namespace nx {

/// A stream of random numbers, the same for the same seed and name.
class Random {
public:
    /// The stream `stream` of `seed`: two streams of one seed draw unrelated numbers.
    constexpr Random(std::uint64_t seed, std::string_view stream) noexcept : m_state{seed ^ hash64(stream)} {}

    /// The next 64 random bits.
    [[nodiscard]] constexpr auto nextBits() noexcept -> std::uint64_t {
        m_state += kGamma;
        std::uint64_t mixed = m_state;
        mixed = (mixed ^ (mixed >> kFirstShift)) * kFirstMultiplier;
        mixed = (mixed ^ (mixed >> kSecondShift)) * kSecondMultiplier;
        return mixed ^ (mixed >> kLastShift);
    }

    /// A number from 0 up to 1, 1 left out: one of 2^53 evenly spaced values, each as likely.
    [[nodiscard]] constexpr auto nextUnit() noexcept -> double {
        return static_cast<double>(nextBits() >> kUnitDroppedBits) * kUnitStep;
    }

    /// A number from `least` to `most`: nextUnit's share of the way between them.
    [[nodiscard]] constexpr auto between(float least, float most) noexcept -> float {
        return least + (static_cast<float>(nextUnit()) * (most - least));
    }

    /// A whole number from 0 up to `count`, `count` left out, each as likely; 0 when `count` is 0.
    [[nodiscard]] constexpr auto below(std::uint64_t count) noexcept -> std::uint64_t {
        if (count == 0) {
            return 0;
        }
        // The lowest values of the bits that would make the smallest results likelier are drawn again.
        const std::uint64_t unfair = (std::uint64_t{0} - count) % count;
        std::uint64_t bits = nextBits();
        while (bits < unfair) {
            bits = nextBits();
        }
        return bits % count;
    }

private:
    static constexpr std::uint64_t kGamma = 0x9E37'79B9'7F4A'7C15ULL;
    static constexpr std::uint64_t kFirstMultiplier = 0xBF58'476D'1CE4'E5B9ULL;
    static constexpr std::uint64_t kSecondMultiplier = 0x94D0'49BB'1331'11EBULL;
    static constexpr int kFirstShift = 30;
    static constexpr int kSecondShift = 27;
    static constexpr int kLastShift = 31;
    /// A unit takes the top 53 bits, as many as a double's significand holds.
    static constexpr int kUnitDroppedBits = 11;
    static constexpr double kUnitStep = 0x1.0p-53;

    std::uint64_t m_state;
};

} // namespace nx
