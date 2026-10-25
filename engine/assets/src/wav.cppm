// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// WAV sounds (docs/06-assets-and-resources.md, section 3.1): the 8- and 16-bit PCM of the original
/// content and every other common uncompressed layout, at any rate and in any number of channels,
/// so that no sound is held to the original's limits (docs/11-audio.md, section 2.1). Cue points
/// mark where a looping sound loops.
export module nx.assets:wav;

import std;
import nx.core;

export namespace nx::assets {

struct SampleFramesTag {
    static constexpr std::string_view kSuffix = " frames";
};
struct ChannelsTag {
    static constexpr std::string_view kSuffix = " channels";
};

/// A position in a sound, or a length, in sample frames: one sample of every channel.
using SampleFrames = Quantity<std::uint32_t, SampleFramesTag>;

/// How many samples a frame has, interleaved.
using ChannelCount = Quantity<std::uint16_t, ChannelsTag>;

/// What can be wrong with a WAV file; the detail has the offending values.
enum class WavError : std::uint8_t {
    TooSmall,          ///< Smaller than the RIFF header.
    NotRiffWave,       ///< The file does not start with "RIFF" and "WAVE".
    ChunkOutOfBounds,  ///< A chunk before the "fmt " and "data" chunks does not fit in the file.
    MissingChunk,      ///< The "fmt " or the "data" chunk is missing.
    DuplicateChunk,    ///< Two "fmt ", "data" or "cue " chunks.
    UnsupportedFormat, ///< A compressed encoding (ADPCM, A-law...) or a sample size PCM and IEEE
                       ///< float do not define.
    BadFormat,         ///< The format chunk contradicts itself or the data.
};

/// How each sample is stored, little-endian.
enum class SampleFormat : std::uint8_t {
    Unsigned8, ///< One byte, 128 is silence.
    Signed16,
    Signed24,
    Signed32,
    Float32, ///< IEEE 754, silence at 0 and full scale at ±1.
    Float64, ///< IEEE 754, silence at 0 and full scale at ±1.
};

/// The bits a sample of a format takes in a file, which its name says.
struct SampleBits {
    SampleFormat format = SampleFormat::Unsigned8;
    std::uint16_t bits = 0;
};
inline constexpr std::array kSampleBits{
    SampleBits{.format = SampleFormat::Unsigned8, .bits = 8},
    SampleBits{.format = SampleFormat::Signed16, .bits = 16},
    SampleBits{.format = SampleFormat::Signed24, .bits = 24},
    SampleBits{.format = SampleFormat::Signed32, .bits = 32},
    SampleBits{.format = SampleFormat::Float32, .bits = 32},
    SampleBits{.format = SampleFormat::Float64, .bits = 64},
};

/// The bits a sample of `format` takes in a file (kSampleBits), and its bytes, an eighth of them.
[[nodiscard]] constexpr auto bitsPerSample(SampleFormat format) noexcept -> std::uint16_t {
    for (const SampleBits& entry : kSampleBits) {
        if (entry.format == format) {
            return entry.bits;
        }
    }
    return 0; // a value from outside the enumerators is no format
}
[[nodiscard]] constexpr auto bytesPerSample(SampleFormat format) noexcept -> std::size_t {
    return bitsPerSample(format) / std::size_t{std::numeric_limits<std::uint8_t>::digits};
}

/// An uncompressed sound: its format and views of its samples in the bytes it was parsed from,
/// which must outlive it.
struct WavSound {
    SampleFormat format = SampleFormat::Unsigned8;
    ChannelCount channels{1};
    SampleRate rate{0};
    SampleFrames length{0};
    std::span<const std::byte> samples;    ///< Frame after frame, channels interleaved.
    std::vector<SampleFrames> cuePoints{}; ///< In file order, each within the sound.
};

/// Reads a WAV file: its "fmt " (PCM, IEEE float or extensible), "data" and "cue " chunks; other
/// chunks are skipped. The size in the RIFF header is not trusted: chunks are read up to the end of
/// the bytes. Once the "fmt " and "data" chunks are read, what follows is metadata, which old
/// editors wrote wrongly: Cool Edit left out the pad byte of an odd "data" chunk, which puts the
/// next chunk a byte early, and GoldWave appended a "LIST" chunk past the end of the RIFF form. A
/// chunk that does not fit then ends the reading, as it ends the original's, and the chunks after
/// it are not read. A cue point past the end of the samples (a marker that resampling left behind)
/// marks no sample and is left out. Errors: every WavError.
[[nodiscard]] auto parseWav(std::span<const std::byte> bytes) -> Result<WavSound, WavError>;

} // namespace nx::assets
