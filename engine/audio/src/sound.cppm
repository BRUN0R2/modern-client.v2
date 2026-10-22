// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Sounds ready to mix (docs/11-audio.md, section 6): a WAV file's samples, of any layout it holds,
/// made 32-bit floats, one channel or two, at the file's own rate, which the mixer resamples.
export module nx.audio:sound;

import std;
import nx.core;
import nx.assets;

export namespace nx::audio {

/// A sound's samples as floats, from 0 at silence to ±1 at full scale, one or two channels
/// interleaved, at its rate; and where a looping sound loops from.
class Sound {
public:
    /// The sound of `wav`: its samples made floats (an 8-bit unsigned one about 128); the first two
    /// channels of one of more, its front left and right; where it loops from its first cue point, as
    /// the original loops a sound that has one.
    [[nodiscard]] static auto of(const assets::WavSound& wav) -> Sound;

    /// A sound of `samples`, `channels` (1 or 2) interleaved, at `rate`, looping from `loopStart` when
    /// it loops.
    Sound(std::vector<float> samples, std::uint16_t channels, SampleRate rate,
          std::optional<assets::SampleFrames> loopStart = std::nullopt);

    [[nodiscard]] inline auto channels() const noexcept -> std::uint16_t { return m_channels; }
    [[nodiscard]] inline auto rate() const noexcept -> SampleRate { return m_rate; }
    /// Its length in frames: a sample of each channel.
    [[nodiscard]] inline auto frames() const noexcept -> std::size_t { return m_samples.size() / m_channels; }
    [[nodiscard]] inline auto samples() const noexcept -> std::span<const float> { return m_samples; }
    /// Where it loops from when it loops; none: from its start.
    [[nodiscard]] inline auto loopStart() const noexcept -> std::optional<assets::SampleFrames> {
        return m_loopStart;
    }

private:
    std::vector<float> m_samples;
    std::uint16_t m_channels;
    SampleRate m_rate;
    std::optional<assets::SampleFrames> m_loopStart;
};

} // namespace nx::audio
