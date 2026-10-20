// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;

namespace {

using nx::assets::ChannelCount;
using nx::assets::parseWav;
using nx::assets::SampleFormat;
using nx::assets::SampleFrames;
using nx::assets::SampleRate;
using nx::assets::WavError;

using Bytes = std::vector<std::byte>;

template <class Value>
void append(Bytes& bytes, Value value) {
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        bytes.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (index * 8)) & 0xFF));
    }
}

void appendId(Bytes& bytes, std::string_view id) {
    for (const char character : id) {
        bytes.push_back(static_cast<std::byte>(character));
    }
}

void appendChunk(Bytes& bytes, std::string_view id, const Bytes& body) {
    appendId(bytes, id);
    append(bytes, static_cast<std::uint32_t>(body.size()));
    bytes.insert(bytes.end(), body.begin(), body.end());
    if (body.size() % 2 != 0) {
        bytes.push_back(std::byte{0});
    }
}

struct FormatFields {
    std::uint16_t tag = 1;
    std::uint16_t channels = 1;
    std::uint32_t rate = 22050;
    std::uint16_t bits = 8;
    std::optional<std::uint16_t> frameBytes{}; // computed when absent
};

[[nodiscard]] auto formatChunk(FormatFields fields) -> Bytes {
    const std::uint16_t frameBytes =
        fields.frameBytes.value_or(static_cast<std::uint16_t>(fields.channels * (fields.bits / 8)));
    Bytes body;
    append(body, fields.tag);
    append(body, fields.channels);
    append(body, fields.rate);
    append(body, fields.rate * frameBytes);
    append(body, frameBytes);
    append(body, fields.bits);
    return body;
}

/// An extensible format chunk (WAVE_FORMAT_EXTENSIBLE) whose subformat GUID starts with `subformat`
/// (1, PCM, or 3, IEEE float, for the standard GUIDs).
[[nodiscard]] auto extensibleFormatChunk(FormatFields fields, std::uint16_t subformat,
                                         std::optional<std::uint16_t> validBits = std::nullopt,
                                         bool isStandardGuid = true) -> Bytes {
    Bytes body =
        formatChunk({.tag = 0xFFFE, .channels = fields.channels, .rate = fields.rate, .bits = fields.bits});
    append(body, std::uint16_t{22}); // the size of the extension
    append(body, validBits.value_or(fields.bits));
    append(body, std::uint32_t{0}); // the speaker mask
    append(body, subformat);
    for (const int byte :
         {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}) {
        body.push_back(static_cast<std::byte>(isStandardGuid ? byte : 0x42));
    }
    return body;
}

/// A cue chunk with a point at each of `offsets`.
[[nodiscard]] auto cueChunk(std::span<const std::uint32_t> offsets) -> Bytes {
    Bytes body;
    append(body, static_cast<std::uint32_t>(offsets.size()));
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        append(body, static_cast<std::uint32_t>(index)); // id
        append(body, std::uint32_t{0});                  // play order
        appendId(body, "data");
        append(body, std::uint32_t{0}); // chunk start
        append(body, std::uint32_t{0}); // block start
        append(body, offsets[index]);
    }
    return body;
}

/// A RIFF WAVE file with `chunks` (an id and a body each) in order.
[[nodiscard]] auto wavFile(const std::vector<std::pair<std::string_view, Bytes>>& chunks) -> Bytes {
    Bytes file;
    appendId(file, "RIFF");
    append(file, std::uint32_t{0}); // the size, which the reader does not trust
    appendId(file, "WAVE");
    for (const auto& [id, body] : chunks) {
        appendChunk(file, id, body);
    }
    return file;
}

[[nodiscard]] auto parseError(const Bytes& file) -> WavError {
    const auto sound = parseWav(file);
    EXPECT_FALSE(sound.has_value());
    return sound ? WavError{} : sound.error().code;
}

TEST(WavTest, ReadsEightBitMono) {
    const Bytes samples{std::byte{128}, std::byte{255}, std::byte{0}};
    const Bytes file = wavFile({{"fmt ", formatChunk({})}, {"data", samples}});

    const auto sound = parseWav(file);
    ASSERT_TRUE(sound.has_value()) << nx::describe(sound.error());
    EXPECT_EQ(sound->format, SampleFormat::Unsigned8);
    EXPECT_EQ(sound->channels, ChannelCount{1});
    EXPECT_EQ(sound->rate, SampleRate{22050});
    EXPECT_EQ(sound->length, SampleFrames{3});
    EXPECT_TRUE(std::ranges::equal(sound->samples, samples)) << "the samples are a view of the file";
    EXPECT_TRUE(sound->cuePoints.empty());
}

TEST(WavTest, ReadsSixteenBitStereoWithCuePointsAndOtherChunks) {
    const Bytes samples(4 * 10, std::byte{1}); // ten frames of two 16-bit samples
    const std::array<std::uint32_t, 2> cues{4, 10};
    const Bytes file = wavFile({{"fmt ", formatChunk({.channels = 2, .rate = 44100, .bits = 16})},
                                {"LIST", Bytes(5, std::byte{7})}, // skipped, with its pad byte
                                {"data", samples},
                                {"cue ", cueChunk(cues)}});

    const auto sound = parseWav(file);
    ASSERT_TRUE(sound.has_value()) << nx::describe(sound.error());
    EXPECT_EQ(sound->format, SampleFormat::Signed16);
    EXPECT_EQ(sound->channels, ChannelCount{2});
    EXPECT_EQ(sound->rate, SampleRate{44100});
    EXPECT_EQ(sound->length, SampleFrames{10});
    EXPECT_EQ(sound->cuePoints, (std::vector<SampleFrames>{SampleFrames{4}, SampleFrames{10}}));
}

TEST(WavTest, ReadsEveryUncompressedLayoutAtAnyRateAndChannelCount) {
    struct Layout {
        std::uint16_t tag = 0;
        std::uint16_t bits = 0;
        SampleFormat format = SampleFormat::Unsigned8;
    };
    for (const Layout layout : {Layout{1, 8, SampleFormat::Unsigned8}, Layout{1, 16, SampleFormat::Signed16},
                                Layout{1, 24, SampleFormat::Signed24}, Layout{1, 32, SampleFormat::Signed32},
                                Layout{3, 32, SampleFormat::Float32}, Layout{3, 64, SampleFormat::Float64}}) {
        const FormatFields fields{.tag = layout.tag, .channels = 6, .rate = 96000, .bits = layout.bits};
        const Bytes samples(std::size_t{6} * (layout.bits / 8) * 5, std::byte{0}); // five frames
        for (const Bytes& format : {formatChunk(fields), extensibleFormatChunk(fields, layout.tag)}) {
            const auto sound = parseWav(wavFile({{"fmt ", format}, {"data", samples}}));
            ASSERT_TRUE(sound.has_value())
                << layout.tag << "/" << layout.bits << ": " << nx::describe(sound.error());
            EXPECT_EQ(sound->format, layout.format);
            EXPECT_EQ(sound->channels, ChannelCount{6});
            EXPECT_EQ(sound->rate, SampleRate{96000});
            EXPECT_EQ(sound->length, SampleFrames{5});
        }
    }

    const FormatFields twentyFourInThirtyTwo{.channels = 2, .rate = 48000, .bits = 32};
    const auto padded =
        parseWav(wavFile({{"fmt ", extensibleFormatChunk(twentyFourInThirtyTwo, 1, std::uint16_t{24})},
                          {"data", Bytes(8 * 3, std::byte{0})}}));
    ASSERT_TRUE(padded.has_value()) << nx::describe(padded.error());
    EXPECT_EQ(padded->format, SampleFormat::Signed32) << "24 valid bits in 32-bit samples read as 32";
}

TEST(WavTest, RejectsWhatIsNotAnUncompressedSound) {
    EXPECT_EQ(parseError(Bytes(11, std::byte{0})), WavError::TooSmall);

    Bytes avi = wavFile({{"fmt ", formatChunk({})}, {"data", Bytes(2, std::byte{0})}});
    avi[10] = std::byte{'X'}; // "WAXE"
    EXPECT_EQ(parseError(avi), WavError::NotRiffWave);

    EXPECT_EQ(parseError(wavFile({{"data", Bytes(2, std::byte{0})}})), WavError::MissingChunk);
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({})}})), WavError::MissingChunk);
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({})}, {"fmt ", formatChunk({})}, {"data", Bytes(2)}})),
              WavError::DuplicateChunk);

    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.tag = 2})}, {"data", Bytes(2)}})),
              WavError::UnsupportedFormat)
        << "ADPCM";
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.bits = 12})}, {"data", Bytes(2)}})),
              WavError::UnsupportedFormat)
        << "12-bit PCM";
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.tag = 3, .bits = 16})}, {"data", Bytes(2)}})),
              WavError::UnsupportedFormat)
        << "16-bit float";
    EXPECT_EQ(parseError(wavFile({{"fmt ", extensibleFormatChunk({.bits = 16}, 1, std::nullopt, false)},
                                  {"data", Bytes(2)}})),
              WavError::UnsupportedFormat)
        << "an unknown subformat";
}

TEST(WavTest, RejectsAFormatThatContradictsItselfOrTheData) {
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.rate = 0})}, {"data", Bytes(2)}})),
              WavError::BadFormat);
    EXPECT_EQ(
        parseError(wavFile({{"fmt ", formatChunk({.frameBytes = std::uint16_t{3}})}, {"data", Bytes(3)}})),
        WavError::BadFormat);
    EXPECT_EQ(parseError(wavFile({{"fmt ", Bytes(12)}, {"data", Bytes(2)}})), WavError::BadFormat)
        << "a format chunk too short";
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.tag = 0xFFFE, .bits = 16})}, {"data", Bytes(2)}})),
              WavError::BadFormat)
        << "an extensible format chunk without its extension";
    EXPECT_EQ(parseError(wavFile(
                  {{"fmt ", extensibleFormatChunk({.bits = 16}, 1, std::uint16_t{20})}, {"data", Bytes(2)}})),
              WavError::BadFormat)
        << "more valid bits than the sample holds";
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.channels = 0})}, {"data", Bytes(2)}})),
              WavError::BadFormat)
        << "no channel";
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({.bits = 16})}, {"data", Bytes(3)}})),
              WavError::BadFormat)
        << "half a frame";

    const std::array<std::uint32_t, 1> pastTheEnd{3};
    Bytes cutCue = cueChunk(pastTheEnd);
    cutCue.resize(cutCue.size() - 1);
    EXPECT_EQ(parseError(wavFile({{"fmt ", formatChunk({})}, {"data", Bytes(4)}, {"cue ", cutCue}})),
              WavError::BadFormat);
}

TEST(WavTest, LeavesOutCuePointsPastTheEnd) {
    const std::array<std::uint32_t, 4> cues{0, 3, 2,
                                            135345}; // 135345: a marker of the sound before resampling
    const auto sound =
        parseWav(wavFile({{"fmt ", formatChunk({})}, {"data", Bytes(2)}, {"cue ", cueChunk(cues)}}));
    ASSERT_TRUE(sound.has_value()) << nx::describe(sound.error());
    EXPECT_EQ(sound->cuePoints, (std::vector<SampleFrames>{SampleFrames{0}, SampleFrames{2}}));
}

TEST(WavTest, RejectsAChunkOutsideTheFileBeforeTheSound) {
    Bytes file = wavFile({{"fmt ", formatChunk({})}, {"data", Bytes(4)}});
    file.resize(file.size() - 1); // the data chunk claims one byte more than is left
    EXPECT_EQ(parseError(file), WavError::ChunkOutOfBounds);

    Bytes cutHeader = wavFile({{"fmt ", formatChunk({})}});
    appendId(cutHeader, "da");
    EXPECT_EQ(parseError(cutHeader), WavError::ChunkOutOfBounds);
}

TEST(WavTest, StopsAtMetadataThatDoesNotFitAfterTheSound) {
    const Bytes samples{std::byte{1}, std::byte{2}, std::byte{3}};
    const Bytes list(6, std::byte{'x'});
    const std::array<std::uint32_t, 1> cues{1};

    // Cool Edit: no pad byte after the odd data chunk, so the LIST starts where the pad would be.
    Bytes withoutPad = wavFile({{"fmt ", formatChunk({})}});
    appendId(withoutPad, "data");
    append(withoutPad, static_cast<std::uint32_t>(samples.size()));
    withoutPad.insert(withoutPad.end(), samples.begin(), samples.end());
    appendChunk(withoutPad, "LIST", list);
    appendChunk(withoutPad, "cue ", cueChunk(cues)); // after the misplaced chunk: not read
    const auto coolEdit = parseWav(withoutPad);
    ASSERT_TRUE(coolEdit.has_value()) << nx::describe(coolEdit.error());
    EXPECT_TRUE(std::ranges::equal(coolEdit->samples, samples));
    EXPECT_TRUE(coolEdit->cuePoints.empty());

    // A few bytes after the sound, too few for a chunk header.
    Bytes trailing = wavFile({{"fmt ", formatChunk({})}, {"data", samples}});
    appendId(trailing, "LI");
    const auto cut = parseWav(trailing);
    ASSERT_TRUE(cut.has_value()) << nx::describe(cut.error());
    EXPECT_EQ(cut->length, SampleFrames{3});
}

} // namespace
