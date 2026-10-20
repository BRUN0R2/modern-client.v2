// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

using FourCc = std::array<std::byte, 4>;

[[nodiscard]] consteval auto fourCc(std::string_view text) -> FourCc {
    return FourCc{static_cast<std::byte>(text[0]), static_cast<std::byte>(text[1]),
                  static_cast<std::byte>(text[2]), static_cast<std::byte>(text[3])};
}

// The RIFF header: "RIFF", the size of what follows (not trusted), "WAVE"; then chunks, each an id,
// a size and its bytes, padded to an even size.
constexpr FourCc kRiff = fourCc("RIFF");
constexpr FourCc kWave = fourCc("WAVE");
constexpr std::size_t kWaveOffset = 8;
constexpr std::size_t kRiffHeaderBytes = 12;
constexpr std::size_t kChunkSizeOffset = 4;
constexpr std::size_t kChunkHeaderBytes = 8;
constexpr FourCc kFormatChunk = fourCc("fmt ");
constexpr FourCc kDataChunk = fourCc("data");
constexpr FourCc kCueChunk = fourCc("cue ");

// The format chunk: format tag, channels, frame rate, byte rate, bytes per frame, bits per sample.
// The extensible format adds the size of the extension, the valid bits per sample, the speaker
// mask and the subformat: a GUID whose first two bytes are the real format tag.
constexpr std::uint16_t kPcmFormatTag = 1;
constexpr std::uint16_t kFloatFormatTag = 3;
constexpr std::uint16_t kExtensibleFormatTag = 0xFFFE;
constexpr std::size_t kChannelsOffset = 2;
constexpr std::size_t kRateOffset = 4;
constexpr std::size_t kFrameBytesOffset = 12;
constexpr std::size_t kBitsOffset = 14;
constexpr std::size_t kFormatBytes = 16;
constexpr std::size_t kValidBitsOffset = 18;
constexpr std::size_t kSubformatOffset = 24;
constexpr std::size_t kSubformatTagBytes = 2;
constexpr std::size_t kExtensibleFormatBytes = 40;
constexpr std::uint16_t kBitsPerByte = 8;

// The rest of the subformat GUIDs of PCM and IEEE float (KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT,
// {0000tttt-0000-0010-8000-00AA00389B71}), as stored.
constexpr std::array kSubformatTail{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                    std::byte{0x10}, std::byte{0x00}, std::byte{0x80}, std::byte{0x00},
                                    std::byte{0x00}, std::byte{0xAA}, std::byte{0x00}, std::byte{0x38},
                                    std::byte{0x9B}, std::byte{0x71}};

/// A sample layout WAV defines: a format tag, the bits of a sample, and how they read.
struct SampleLayout {
    std::uint16_t tag = 0;
    std::uint16_t bits = 0;
    SampleFormat format = SampleFormat::Unsigned8;
};

constexpr std::array kSampleLayouts{
    SampleLayout{.tag = kPcmFormatTag, .bits = 8, .format = SampleFormat::Unsigned8},
    SampleLayout{.tag = kPcmFormatTag, .bits = 16, .format = SampleFormat::Signed16},
    SampleLayout{.tag = kPcmFormatTag, .bits = 24, .format = SampleFormat::Signed24},
    SampleLayout{.tag = kPcmFormatTag, .bits = 32, .format = SampleFormat::Signed32},
    SampleLayout{.tag = kFloatFormatTag, .bits = 32, .format = SampleFormat::Float32},
    SampleLayout{.tag = kFloatFormatTag, .bits = 64, .format = SampleFormat::Float64},
};

// The cue chunk: a count, then cue points of an id, a play order, a chunk id, a chunk start, a block
// start and the sample offset, in frames, that GoldSrc loops from.
constexpr std::size_t kCueCountBytes = 4;
constexpr std::size_t kCuePointBytes = 24;
constexpr std::size_t kCueSampleOffsetOffset = 20;

struct Chunks {
    std::optional<std::span<const std::byte>> format;
    std::optional<std::span<const std::byte>> data;
    std::optional<std::span<const std::byte>> cue;
};

struct Format {
    SampleFormat sampleFormat = SampleFormat::Unsigned8;
    ChannelCount channels{1};
    SampleRate rate{0};
    std::size_t frameBytes = 0;
};

/// The chunks after the RIFF header, up to the first one that does not fit once the format and the
/// data are found (parseWav). Errors: ChunkOutOfBounds, DuplicateChunk.
[[nodiscard]] auto findChunks(std::span<const std::byte> bytes) -> Result<Chunks, WavError> {
    Chunks chunks;
    std::size_t offset = kRiffHeaderBytes;
    while (offset < bytes.size()) {
        const std::size_t left = bytes.size() - offset;
        const bool hasHeader = left >= kChunkHeaderBytes;
        const auto size = hasHeader
                              ? loadLittleEndian<std::uint32_t>(bytes.subspan(offset + kChunkSizeOffset))
                              : std::uint32_t{0};
        if (!hasHeader || size > left - kChunkHeaderBytes) {
            if (chunks.format && chunks.data) {
                break; // metadata after the sound that cannot be placed
            }
            return fail(WavError::ChunkOutOfBounds,
                        hasHeader
                            ? std::format("a chunk of {} bytes at {} is outside the file's {}", size, offset,
                                          bytes.size())
                            : std::format("a chunk header at {} is cut by the end of the file", offset));
        }
        const auto id = bytes.subspan(offset, FourCc{}.size());
        const auto body = bytes.subspan(offset + kChunkHeaderBytes, size);
        for (const auto& [chunkId, slot] :
             {std::pair{kFormatChunk, &chunks.format}, std::pair{kDataChunk, &chunks.data},
              std::pair{kCueChunk, &chunks.cue}}) {
            if (std::ranges::equal(id, chunkId)) {
                if (slot->has_value()) {
                    return fail(WavError::DuplicateChunk, std::format("a second chunk at {}", offset));
                }
                *slot = body;
            }
        }
        offset += kChunkHeaderBytes + size + (size % 2); // the pad byte of an odd size
    }
    return chunks;
}

/// The format tag an extensible format chunk names. Errors: UnsupportedFormat, BadFormat.
[[nodiscard]] auto extensibleTag(std::span<const std::byte> chunk, std::uint16_t bits)
    -> Result<std::uint16_t, WavError> {
    if (chunk.size() < kExtensibleFormatBytes) {
        return fail(WavError::BadFormat, std::format("an extensible format chunk of {} bytes, less than {}",
                                                     chunk.size(), kExtensibleFormatBytes));
    }
    // Zero valid bits says nothing; more than the sample holds is a contradiction.
    const auto validBits = loadLittleEndian<std::uint16_t>(chunk.subspan(kValidBitsOffset));
    if (validBits > bits) {
        return fail(WavError::BadFormat, std::format("{} valid bits in samples of {}", validBits, bits));
    }
    const auto subformat = chunk.subspan(kSubformatOffset, kSubformatTagBytes + kSubformatTail.size());
    if (!std::ranges::equal(subformat.subspan(kSubformatTagBytes), kSubformatTail)) {
        return fail(WavError::UnsupportedFormat,
                    "an extensible format whose subformat is neither PCM nor IEEE float");
    }
    return loadLittleEndian<std::uint16_t>(subformat);
}

/// The format chunk. Errors: UnsupportedFormat, BadFormat.
[[nodiscard]] auto readFormat(std::span<const std::byte> chunk) -> Result<Format, WavError> {
    if (chunk.size() < kFormatBytes) {
        return fail(WavError::BadFormat,
                    std::format("a format chunk of {} bytes, less than {}", chunk.size(), kFormatBytes));
    }
    auto tag = loadLittleEndian<std::uint16_t>(chunk);
    const auto channels = loadLittleEndian<std::uint16_t>(chunk.subspan(kChannelsOffset));
    const auto rate = loadLittleEndian<std::uint32_t>(chunk.subspan(kRateOffset));
    const auto frameBytes = loadLittleEndian<std::uint16_t>(chunk.subspan(kFrameBytesOffset));
    const auto bits = loadLittleEndian<std::uint16_t>(chunk.subspan(kBitsOffset));
    if (tag == kExtensibleFormatTag) {
        const auto named = extensibleTag(chunk, bits);
        if (!named) {
            return std::unexpected(named.error());
        }
        tag = *named;
    }
    const auto layout = std::ranges::find_if(kSampleLayouts, [tag, bits](const SampleLayout& candidate) {
        return candidate.tag == tag && candidate.bits == bits;
    });
    if (layout == kSampleLayouts.end()) {
        return fail(
            WavError::UnsupportedFormat,
            std::format("format tag {:#06x} with samples of {} bits; WAV is read as PCM of 8, 16, 24 or 32 "
                        "bits and as IEEE float of 32 or 64 bits",
                        tag, bits));
    }
    if (channels == 0 || rate == 0 || frameBytes != channels * (bits / kBitsPerByte)) {
        return fail(WavError::BadFormat,
                    std::format("{} frames per second of {} bytes, for {} channels of {} bits", rate,
                                frameBytes, channels, bits));
    }
    return Format{
        .sampleFormat = layout->format,
        .channels = ChannelCount{channels},
        .rate = SampleRate{rate},
        .frameBytes = frameBytes,
    };
}

/// The sample offsets of the cue chunk that are at most `length`. Errors: BadFormat.
[[nodiscard]] auto readCuePoints(std::span<const std::byte> chunk, SampleFrames length)
    -> Result<std::vector<SampleFrames>, WavError> {
    if (chunk.size() < kCueCountBytes) {
        return fail(WavError::BadFormat, "a cue chunk without its count");
    }
    const auto count = loadLittleEndian<std::uint32_t>(chunk);
    if (std::uint64_t{count} * kCuePointBytes > chunk.size() - kCueCountBytes) {
        return fail(WavError::BadFormat,
                    std::format("{} cue points in a cue chunk of {} bytes", count, chunk.size()));
    }
    std::vector<SampleFrames> cuePoints;
    cuePoints.reserve(count); // bounded by the chunk's size, checked above
    for (std::size_t index = 0; index < count; ++index) {
        const SampleFrames offset{loadLittleEndian<std::uint32_t>(
            chunk.subspan(kCueCountBytes + (index * kCuePointBytes) + kCueSampleOffsetOffset))};
        if (offset <= length) {
            cuePoints.push_back(offset);
        }
    }
    return cuePoints;
}

} // namespace

auto parseWav(std::span<const std::byte> bytes) -> Result<WavSound, WavError> {
    if (bytes.size() < kRiffHeaderBytes) {
        return fail(WavError::TooSmall, std::format("{} bytes, less than the {} of the RIFF header",
                                                    bytes.size(), kRiffHeaderBytes));
    }
    if (!std::ranges::equal(bytes.first(kRiff.size()), kRiff) ||
        !std::ranges::equal(bytes.subspan(kWaveOffset, kWave.size()), kWave)) {
        return fail(WavError::NotRiffWave, "the file does not start with \"RIFF\" and \"WAVE\"");
    }
    const auto chunks = findChunks(bytes);
    if (!chunks) {
        return std::unexpected(chunks.error());
    }
    if (!chunks->format || !chunks->data) {
        return fail(WavError::MissingChunk, !chunks->format ? "no \"fmt \" chunk" : "no \"data\" chunk");
    }
    const auto format = readFormat(*chunks->format);
    if (!format) {
        return std::unexpected(format.error());
    }
    const std::span<const std::byte> samples = *chunks->data;
    if (samples.size() % format->frameBytes != 0) {
        return fail(WavError::BadFormat, std::format("{} bytes of samples are not whole frames of {} bytes",
                                                     samples.size(), format->frameBytes));
    }
    WavSound sound{
        .format = format->sampleFormat,
        .channels = format->channels,
        .rate = format->rate,
        .length = SampleFrames{narrow<std::uint32_t>(samples.size() / format->frameBytes)},
        .samples = samples,
        .cuePoints = {},
    };
    if (chunks->cue) {
        auto cuePoints = readCuePoints(*chunks->cue, sound.length);
        if (!cuePoints) {
            return std::unexpected(std::move(cuePoints.error()));
        }
        sound.cuePoints = std::move(*cuePoints);
    }
    return sound;
}

} // namespace nx::assets
