// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.audio;

import std;
import nx.core;
import nx.assets;

namespace nx::audio {

namespace {

/// The most channels a sound keeps: its front left and right.
constexpr std::uint16_t kMaxChannels = 2;
/// Full scale of each integer layout, which ±1 is.
constexpr double kUnsigned8Middle = 128.0;
constexpr double kSigned8Scale = 128.0;
constexpr double kSigned16Scale = 32'768.0;
constexpr double kSigned24Scale = 8'388'608.0;
constexpr double kSigned32Scale = 2'147'483'648.0;
constexpr std::size_t kBytes24 = 3;
constexpr unsigned kBitsPerByte = 8;

/// The sample of `format` at the start of `bytes`, as a float.
[[nodiscard]] auto sampleAt(assets::SampleFormat format, std::span<const std::byte> bytes) noexcept -> float {
    switch (format) {
    case assets::SampleFormat::Unsigned8:
        return static_cast<float>(
            (static_cast<double>(std::to_integer<std::uint8_t>(bytes[0])) - kUnsigned8Middle) /
            kSigned8Scale);
    case assets::SampleFormat::Signed16:
        return static_cast<float>(static_cast<double>(loadLittleEndian<std::int16_t>(bytes)) /
                                  kSigned16Scale);
    case assets::SampleFormat::Signed24: {
        std::uint32_t bits = 0;
        for (std::size_t index = 0; index < kBytes24; ++index) {
            bits |= std::to_integer<std::uint32_t>(bytes[index]) << (index * kBitsPerByte);
        }
        // Signed: the top byte repeats the 24th bit.
        constexpr std::uint32_t kSignBit = 0x0080'0000U;
        constexpr std::uint32_t kSignExtension = 0xFF00'0000U;
        if ((bits & kSignBit) != 0) {
            bits |= kSignExtension;
        }
        return static_cast<float>(static_cast<double>(std::bit_cast<std::int32_t>(bits)) / kSigned24Scale);
    }
    case assets::SampleFormat::Signed32:
        return static_cast<float>(static_cast<double>(loadLittleEndian<std::int32_t>(bytes)) /
                                  kSigned32Scale);
    case assets::SampleFormat::Float32:
        return std::bit_cast<float>(loadLittleEndian<std::uint32_t>(bytes));
    case assets::SampleFormat::Float64:
        return static_cast<float>(std::bit_cast<double>(loadLittleEndian<std::uint64_t>(bytes)));
    }
    return 0.0F;
}

/// The bytes a sample of `format` takes.
[[nodiscard]] auto bytesOf(assets::SampleFormat format) noexcept -> std::size_t {
    switch (format) {
    case assets::SampleFormat::Unsigned8:
        return 1;
    case assets::SampleFormat::Signed16:
        return 2;
    case assets::SampleFormat::Signed24:
        return kBytes24;
    case assets::SampleFormat::Signed32:
    case assets::SampleFormat::Float32:
        return 4;
    case assets::SampleFormat::Float64:
        return 8;
    }
    return 1;
}

} // namespace

auto Sound::of(const assets::WavSound& wav) -> Sound {
    const std::uint16_t inFile = wav.channels.value();
    const std::uint16_t kept = std::min(inFile, kMaxChannels);
    const std::size_t sampleBytes = bytesOf(wav.format);
    const std::size_t frameBytes = sampleBytes * inFile;
    std::vector<float> samples;
    samples.reserve(std::size_t{wav.length.value()} * kept);
    for (std::size_t frame = 0; frame < wav.length.value(); ++frame) {
        for (std::size_t channel = 0; channel < kept; ++channel) {
            samples.push_back(
                sampleAt(wav.format, wav.samples.subspan((frame * frameBytes) + (channel * sampleBytes))));
        }
    }
    std::optional<assets::SampleFrames> loopStart;
    if (!wav.cuePoints.empty()) {
        loopStart = wav.cuePoints.front();
    }
    return Sound{std::move(samples), kept, wav.rate, loopStart};
}

Sound::Sound(std::vector<float> samples, std::uint16_t channels, SampleRate rate,
             std::optional<assets::SampleFrames> loopStart)
    : m_samples{std::move(samples)}, m_channels{channels == 0 ? std::uint16_t{1} : channels}, m_rate{rate},
      m_loopStart{loopStart} {
    expects(channels == 1 || channels == kMaxChannels, "Sound: one channel or two");
    expects(m_samples.size() % std::max<std::size_t>(channels, 1) == 0, "Sound: whole frames");
    if (m_loopStart && m_loopStart->value() >= frames()) {
        m_loopStart.reset(); // past the end: it loops from its start
    }
}

} // namespace nx::audio
