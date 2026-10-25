// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The mixer (docs/11-audio.md, section 2): voices playing sounds at any rate and pitch, read
/// through a windowed sinc, summed in 32-bit float with headroom, and kept under full scale by a
/// limiter, so that nothing clips. The game sends it commands through a lock-free queue, each done
/// at once or a delay later, to the frame; the audio thread mixes block after block, neither
/// allocating nor waiting.
///
/// Today: voices with their gains and pitch, looping or not, a master gain, the limiter, and
/// commands done after a delay. Which
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
    /// The mixer holds as many commands sent and not yet done as it can, waiting for the audio thread
    /// or for their time: this one was not sent.
    QueueFull,
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

/// The rate a mixer mixes at unless told, the usual device's, and the voices it has until the voice
/// pool's `s_max_voices` comes (docs/11-audio.md, section 4).
inline constexpr SampleRate kDefaultMixRate{48'000};
inline constexpr std::uint32_t kDefaultVoices = 64;

/// What the mixer is made for: the rate it mixes at, the device's (platform::AudioDevice::rate), and
/// how many voices it has.
struct MixerDesc {
    SampleRate rate{kDefaultMixRate};
    std::uint32_t voices = kDefaultVoices;
};

} // namespace nx::audio

namespace nx::audio {

/// A command sent to the mixer, and the frames of the mix it waits from the start of the block that
/// takes it. The mixer's own, out of it: the queue it goes through checks, where the mixer names it,
/// that it is made without throwing, which a struct nested in the mixer cannot tell before the mixer
/// ends (its member initializers are read there).
struct SentCommand {
    MixerCommand command;
    std::uint64_t delay = 0;
};

/// A command the mixer took, and the frame of the mix it is due at.
struct ScheduledCommand {
    MixerCommand command;
    std::uint64_t frame = 0;
};

} // namespace nx::audio

export namespace nx::audio {

/// Voices mixed into blocks of two channels: what the sound device plays.
class Mixer final : public platform::AudioSource {
public:
    /// The most commands sent and not yet done: not taken by the audio thread yet, or waiting for
    /// their time.
    static constexpr std::size_t kCommandCapacity = 1024;
    /// Samples never go past this, by the limiter: a little under full scale.
    static constexpr float kCeiling = 0.98F;

    explicit Mixer(const MixerDesc& desc);

    Mixer(const Mixer&) = delete;
    auto operator=(const Mixer&) -> Mixer& = delete;
    Mixer(Mixer&&) = delete;
    auto operator=(Mixer&&) -> Mixer& = delete;
    ~Mixer() override = default;

    /// Sends `command` from the game's thread: the audio thread does it `delay` after the start of
    /// the next block it mixes, to the nearest frame of the mix, at once with none; commands due at
    /// the same frame are done in the order sent. A command of a voice the mixer does not have does
    /// nothing. Errors: QueueFull.
    [[nodiscard]] auto send(MixerCommand command, Seconds delay = Seconds{}) -> Result<void, MixerError>;

    /// Mixes the next `out.size() / 2` frames into `out`, left and right interleaved, from the audio
    /// thread: every voice playing, through the master gain and the limiter, each command done at the
    /// frame it is due. Neither allocates nor waits.
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

    /// Keeps `sent`, taken as the block that starts at m_mixed is mixed, until it is due.
    void schedule(SentCommand sent) noexcept;
    /// Does the commands due by `frame`, in their order.
    void doDue(std::uint64_t frame) noexcept;
    void apply(const MixerCommand& command) noexcept;
    /// Adds every voice playing to `out`, then puts it through the master gain and the limiter.
    void mixFrames(std::span<float> out) noexcept;
    /// Adds voice `voice`'s next frames to `out`; stops it when its sound ends.
    void mixVoice(std::size_t voice, std::span<float> out) noexcept;
    /// The sample of `channel` of `voice`'s sound at frame `frame`, read as it loops or 0 outside it.
    [[nodiscard]] auto sampleOf(const Voice& voice, std::int64_t frame, std::size_t channel) const noexcept
        -> float;

    SampleRate m_rate;
    SincKernel m_kernel;
    std::vector<Voice> m_voices;
    std::unique_ptr<std::atomic<bool>[]> m_playing; ///< Of each voice, for the other threads.
    std::unique_ptr<jobs::SpscQueue<SentCommand, kCommandCapacity>> m_commands;
    /// Taken and not yet due, the next due last: never more than kCommandCapacity, its room made once.
    std::vector<ScheduledCommand> m_scheduled;
    /// Sent and not yet done: what send holds to kCommandCapacity.
    std::atomic<std::size_t> m_unfinished{0};
    std::uint64_t m_mixed = 0; ///< Frames mixed so far.
    float m_masterGain = 1.0F;
    float m_limiterGain = 1.0F;
    float m_limiterRelease; ///< The share of the way back to a gain of 1 the limiter goes a frame.
};

} // namespace nx::audio
