// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.audio;

import std;
import nx.core;
import nx.jobs;
import nx.assets;
import nx.platform;

namespace nx::audio {

namespace {

/// The kernel's table holds this many entries a sample.
constexpr int kResolution = 512;
/// The shape of the Kaiser window: about 80 dB below the sound for what the kernel lets through
/// outside its band.
constexpr double kKaiserBeta = 8.0;
/// The limiter comes back to a gain of 1 with this time constant.
constexpr double kLimiterReleaseSeconds = 0.1;
constexpr std::size_t kStereo = 2;

/// The modified Bessel function of the first kind and order 0, by its series, to a part in 10^12.
[[nodiscard]] auto besselI0(double x) noexcept -> double {
    constexpr double kPrecision = 1e-12;
    double sum = 1.0;
    double term = 1.0;
    const double quarterSquare = x * x / 4.0;
    for (int index = 1; term > sum * kPrecision; ++index) {
        term *= quarterSquare / (static_cast<double>(index) * static_cast<double>(index));
        sum += term;
    }
    return sum;
}

} // namespace

SincKernel::SincKernel() {
    const auto entries = static_cast<std::size_t>(kZeroCrossings * kResolution) + 2;
    m_table.reserve(entries);
    const double windowScale = 1.0 / besselI0(kKaiserBeta);
    for (std::size_t index = 0; index < entries; ++index) {
        const double t = static_cast<double>(index) / kResolution;
        if (t >= kZeroCrossings) {
            m_table.push_back(0.0F);
            continue;
        }
        const double sinc = t == 0.0 ? 1.0 : std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
        const double share = t / kZeroCrossings;
        const double window = besselI0(kKaiserBeta * std::sqrt(1.0 - (share * share))) * windowScale;
        m_table.push_back(static_cast<float>(sinc * window));
    }
}

auto SincKernel::at(float t) const noexcept -> float {
    const float distance = std::abs(t) * static_cast<float>(kResolution);
    if (!(distance < static_cast<float>(kZeroCrossings * kResolution))) {
        return 0.0F;
    }
    const auto index = static_cast<std::size_t>(distance);
    const float fraction = distance - static_cast<float>(index);
    return m_table[index] + ((m_table[index + 1] - m_table[index]) * fraction);
}

Mixer::Mixer(const MixerDesc& desc)
    : m_rate{desc.rate}, m_voices(desc.voices), m_playing{std::make_unique<std::atomic<bool>[]>(desc.voices)},
      m_commands{std::make_unique<jobs::SpscQueue<SentCommand, kCommandCapacity>>()},
      m_limiterRelease{static_cast<float>(
          1.0 -
          std::exp(-1.0 / (kLimiterReleaseSeconds * static_cast<double>(std::max(desc.rate.value(), 1U)))))} {
    expects(desc.rate.value() > 0, "Mixer: a rate to mix at");
    m_scheduled.reserve(kCommandCapacity); // the audio thread never makes room: send holds them to it
}

auto Mixer::send(MixerCommand command, Seconds delay) -> Result<void, MixerError> {
    expects(delay >= Seconds{}, "Mixer::send: a delay of no less than 0");
    // Counted before it is queued: the audio thread may do it, and count it done, at once.
    if (m_unfinished.fetch_add(1, std::memory_order_acq_rel) >= kCommandCapacity) {
        m_unfinished.fetch_sub(1, std::memory_order_acq_rel);
        return fail(MixerError::QueueFull, std::format("{} commands not done yet", kCommandCapacity));
    }
    const auto frames = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(std::max(delay.count(), 0.0F)) * static_cast<double>(m_rate.value())));
    if (auto sent = m_commands->push(SentCommand{.command = std::move(command), .delay = frames}); !sent) {
        m_unfinished.fetch_sub(1, std::memory_order_acq_rel);
        return fail(MixerError::QueueFull, std::format("{} commands not taken yet", kCommandCapacity),
                    std::move(sent.error()));
    }
    return {};
}

auto Mixer::isPlaying(std::uint32_t voice) const noexcept -> bool {
    return voice < m_voices.size() && m_playing[voice].load(std::memory_order_acquire);
}

void Mixer::render(std::span<float> out) noexcept {
    while (std::optional<SentCommand> sent = m_commands->pop()) {
        schedule(std::move(*sent));
    }
    std::ranges::fill(out, 0.0F);
    // The block mixed in parts, each from a frame a command is due at to the next.
    const std::uint64_t frames = out.size() / kStereo;
    std::uint64_t done = 0;
    while (done < frames) {
        doDue(m_mixed + done);
        const std::uint64_t until =
            m_scheduled.empty() ? frames : std::min(frames, m_scheduled.back().frame - m_mixed);
        mixFrames(out.subspan(done * kStereo, (until - done) * kStereo));
        done = until;
    }
    m_mixed += frames;
}

void Mixer::schedule(SentCommand sent) noexcept {
    ScheduledCommand scheduled{.command = std::move(sent.command), .frame = m_mixed + sent.delay};
    // Before those due at the same frame, which were sent first: the next due is the last.
    const auto at = std::ranges::lower_bound(m_scheduled, scheduled.frame, std::ranges::greater{},
                                             &ScheduledCommand::frame);
    m_scheduled.insert(at, std::move(scheduled));
}

void Mixer::doDue(std::uint64_t frame) noexcept {
    while (!m_scheduled.empty() && m_scheduled.back().frame <= frame) {
        apply(m_scheduled.back().command);
        m_scheduled.pop_back();
        m_unfinished.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void Mixer::mixFrames(std::span<float> out) noexcept {
    for (std::size_t voice = 0; voice < m_voices.size(); ++voice) {
        if (m_voices[voice].sound != nullptr) {
            mixVoice(voice, out);
        }
    }
    // The master gain, then the limiter: its gain drops at once to what the louder ear needs, and goes
    // back up to 1 over its release.
    for (std::size_t frame = 0; frame + 1 < out.size(); frame += kStereo) {
        const float left = out[frame] * m_masterGain;
        const float right = out[frame + 1] * m_masterGain;
        const float peak = std::max(std::abs(left), std::abs(right));
        const float needed = peak > kCeiling ? kCeiling / peak : 1.0F;
        m_limiterGain = std::min(m_limiterGain + ((1.0F - m_limiterGain) * m_limiterRelease), needed);
        out[frame] = left * m_limiterGain;
        out[frame + 1] = right * m_limiterGain;
    }
}

void Mixer::apply(const MixerCommand& command) noexcept {
    const auto voiceOf = [this](std::uint32_t index) {
        return index < m_voices.size() ? &m_voices[index] : nullptr;
    };
    if (const auto* play = std::get_if<PlayVoice>(&command)) {
        Voice* const voice = voiceOf(play->voice);
        if (voice == nullptr) {
            return;
        }
        const bool isPlayable = play->sound != nullptr && play->sound->frames() > 0 &&
                                play->sound->rate().value() > 0 && play->pitch > 0.0F;
        const double step = isPlayable
                                ? static_cast<double>(play->sound->rate().value()) /
                                      static_cast<double>(m_rate.value()) * static_cast<double>(play->pitch)
                                : 0.0;
        *voice = Voice{.sound = isPlayable ? play->sound : nullptr,
                       .position = 0.0,
                       .step = step,
                       .cutoff = step > 1.0 ? static_cast<float>(1.0 / step) : 1.0F,
                       .gains = play->gains,
                       .loops = play->loops};
        m_playing[play->voice].store(isPlayable, std::memory_order_release);
    } else if (const auto* stop = std::get_if<StopVoice>(&command)) {
        if (Voice* const voice = voiceOf(stop->voice)) {
            voice->sound = nullptr;
            m_playing[stop->voice].store(false, std::memory_order_release);
        }
    } else if (const auto* gains = std::get_if<SetVoiceGains>(&command)) {
        if (Voice* const voice = voiceOf(gains->voice)) {
            voice->gains = gains->gains;
        }
    } else if (const auto* master = std::get_if<SetMasterGain>(&command)) {
        m_masterGain = master->gain;
    }
}

auto Mixer::sampleOf(const Voice& voice, std::int64_t frame, std::size_t channel) const noexcept -> float {
    const Sound& sound = *voice.sound;
    const auto frames = static_cast<std::int64_t>(sound.frames());
    if (frame < 0) {
        return 0.0F;
    }
    if (frame >= frames) {
        if (!voice.loops) {
            return 0.0F;
        }
        const std::int64_t loopStart = sound.loopStart() ? sound.loopStart()->value() : 0;
        frame = loopStart + ((frame - frames) % (frames - loopStart));
    }
    const std::size_t channels = sound.channels();
    return sound.samples()[(static_cast<std::size_t>(frame) * channels) + std::min(channel, channels - 1)];
}

void Mixer::mixVoice(std::size_t index, std::span<float> out) noexcept {
    Voice& voice = m_voices[index];
    const Sound& sound = *voice.sound;
    const auto frames = static_cast<double>(sound.frames());
    const double reach = SincKernel::kZeroCrossings / static_cast<double>(voice.cutoff);
    for (std::size_t frame = 0; frame + 1 < out.size(); frame += kStereo) {
        // The sound read between its samples through the kernel, narrowed when it is sped up.
        const auto first = static_cast<std::int64_t>(std::floor(voice.position - reach)) + 1;
        const auto last = static_cast<std::int64_t>(std::floor(voice.position + reach));
        float left = 0.0F;
        float right = 0.0F;
        for (std::int64_t tap = first; tap <= last; ++tap) {
            const float weight =
                voice.cutoff * m_kernel.at(static_cast<float>((voice.position - static_cast<double>(tap)) *
                                                              static_cast<double>(voice.cutoff)));
            left += sampleOf(voice, tap, 0) * weight;
            right += sampleOf(voice, tap, 1) * weight;
        }
        out[frame] += left * voice.gains.left;
        out[frame + 1] += right * voice.gains.right;
        voice.position += voice.step;
        if (voice.position >= frames) {
            if (!voice.loops) {
                voice.sound = nullptr;
                m_playing[index].store(false, std::memory_order_release);
                return;
            }
            const double loopStart =
                sound.loopStart() ? static_cast<double>(sound.loopStart()->value()) : 0.0;
            voice.position = loopStart + std::fmod(voice.position - frames, frames - loopStart);
        }
    }
}

} // namespace nx::audio
