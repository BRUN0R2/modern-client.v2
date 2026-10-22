// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The mixer (docs/11-audio.md, section 2): voices playing sounds at any rate and pitch, read
/// through a windowed sinc, summed in 32-bit float with headroom, and kept under full scale by a
/// limiter, so that nothing clips. The game sends it commands through a lock-free queue; the audio
/// thread mixes block after block, neither allocating nor waiting.
///
/// Today: voices with their gains and pitch, looping or not, a master gain and the limiter. Which
/// voice a sound takes (channels, priorities and stealing), spatialization, the room's DSP and
/// streaming come with what plays sounds (TODO.md).
export module nx.audio:mixer;

import std;
import nx.core;
import nx.jobs;
import nx.assets;
import nx.platform;
import :sound;

export namespace nx::audio {

/// What can go wrong with the mixer.
enum class MixerError : std::uint8_t {
    QueueFull, ///< The audio thread has not taken the commands sent yet: this one was not sent.
};

/// How loud a voice is in each ear, 1 the sound as it is.
struct VoiceGains {
    float left = 1.0F;
    float right = 1.0F;
};

/// Plays `sound`, which must outlive the play (until a StopVoice or another PlayVoice of the voice has
/// been mixed, or the end of a sound that does not loop), in voice `voice`, replacing what it played.
struct PlayVoice {
    std::uint32_t voice = 0;
    const Sound* sound = nullptr;
    VoiceGains gains{};
    float pitch = 1.0F; ///< Its speed: 2 plays it an octave higher, in half the time.
    bool loops = false; ///< From its loop start to its end, again and again.
};

/// Stops voice `voice`.
struct StopVoice {
    std::uint32_t voice = 0;
};

/// Changes the gains of voice `voice`.
struct SetVoiceGains {
    std::uint32_t voice = 0;
    VoiceGains gains{};
};

/// Changes the gain of everything mixed (`volume`).
struct SetMasterGain {
    float gain = 1.0F;
};

using MixerCommand = std::variant<PlayVoice, StopVoice, SetVoiceGains, SetMasterGain>;

/// The windowed sinc sounds are read through: sin(πt)/(πt), shaped by a Kaiser window (β 8) over 8
/// zero crossings each way, tabulated finely and read between its entries.
class SincKernel {
public:
    /// Zero crossings each way the kernel reaches.
    static constexpr int kZeroCrossings = 8;

    SincKernel();

    /// The kernel at `t`, the distance in samples from its middle; 0 at kZeroCrossings and beyond.
    [[nodiscard]] auto at(float t) const noexcept -> float;

private:
    std::vector<float> m_table; ///< At 0, 1/kResolution, ... kZeroCrossings, and one more.
};

/// What the mixer is made for: the rate it mixes at, the device's (platform::AudioDevice::rate), and
/// how many voices it has.
struct MixerDesc {
    SampleRate rate{48'000};
    std::uint32_t voices = 64;
};

/// Voices mixed into blocks of two channels: what the sound device plays.
class Mixer final : public platform::AudioSource {
public:
    /// The most commands sent and not yet taken by the audio thread.
    static constexpr std::size_t kCommandCapacity = 1024;
    /// Samples never go past this, by the limiter: a little under full scale.
    static constexpr float kCeiling = 0.98F;

    explicit Mixer(const MixerDesc& desc);

    Mixer(const Mixer&) = delete;
    auto operator=(const Mixer&) -> Mixer& = delete;
    Mixer(Mixer&&) = delete;
    auto operator=(Mixer&&) -> Mixer& = delete;
    ~Mixer() override = default;

    /// Sends `command` from the game's thread: the audio thread does it before the next block it
    /// mixes. A command of a voice the mixer does not have does nothing. Errors: QueueFull.
    [[nodiscard]] auto send(MixerCommand command) -> Result<void, MixerError>;

    /// Mixes the next `out.size() / 2` frames into `out`, left and right interleaved, from the audio
    /// thread: the commands sent first, then every voice playing, through the master gain and the
    /// limiter. Neither allocates nor waits.
    void render(std::span<float> out) noexcept override;

    /// Whether voice `voice` plays, as the audio thread last left it; from any thread.
    [[nodiscard]] auto isPlaying(std::uint32_t voice) const noexcept -> bool;

    [[nodiscard]] inline auto voices() const noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(m_voices.size());
    }
    [[nodiscard]] inline auto rate() const noexcept -> SampleRate { return m_rate; }

private:
    /// A voice: what it plays and where it is in it.
    struct Voice {
        const Sound* sound = nullptr;
        double position = 0.0; ///< In frames of its sound.
        double step = 0.0;     ///< Frames of its sound a frame of the mix.
        float cutoff = 1.0F;   ///< Of the kernel, under 1 when it speeds its sound up.
        VoiceGains gains{};
        bool loops = false;
    };

    void apply(const MixerCommand& command) noexcept;
    /// Adds voice `voice`'s next frames to `out`; stops it when its sound ends.
    void mixVoice(std::size_t voice, std::span<float> out) noexcept;
    /// The sample of `channel` of `voice`'s sound at frame `frame`, read as it loops or 0 outside it.
    [[nodiscard]] auto sampleOf(const Voice& voice, std::int64_t frame, std::size_t channel) const noexcept
        -> float;

    SampleRate m_rate;
    SincKernel m_kernel;
    std::vector<Voice> m_voices;
    std::unique_ptr<std::atomic<bool>[]> m_playing; ///< Of each voice, for the other threads.
    std::unique_ptr<jobs::SpscQueue<MixerCommand, kCommandCapacity>> m_commands;
    float m_masterGain = 1.0F;
    float m_limiterGain = 1.0F;
    float m_limiterRelease; ///< The share of the way back to a gain of 1 the limiter goes a frame.
};

} // namespace nx::audio
