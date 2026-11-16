// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Which voice of the mixer a sound the client plays takes (docs/11-audio.md, section 3): as the
/// original's engine chooses a channel for one, a sound on a channel of its maker replaces the one its
/// maker plays there, any other takes the voice with the least left to play, and another's sound never
/// cuts short one of the player's own. Unlike the original, whose dynamic channels are few, every voice
/// of the mixer is one to choose from.
export module cs.client:voices;

import std;
import nx.audio;
import cs.shared;

export namespace cs {

/// What a voice of the client's mixer was last given to play: whose sound, none for the player's own;
/// the channel of its maker it plays on; and the frame of the mix by which it is done
/// (nx::audio::Mixer::framesMixed), none for one never given any.
struct VoiceUse {
    std::optional<PlayerIndex> maker{};
    SoundChannel channel = SoundChannel::Auto;
    nx::audio::MixFrames doneAt{};
};

/// The voice of `voices` that a sound of `maker`, none for the player's own, on `channel` takes at frame
/// `now` of the mix, as the original's engine chooses a channel for a sound: the voice that last played
/// `maker`'s `channel`, whose sound it replaces, but on SoundChannel::Auto, where each sound takes one of
/// its own; else the voice with the least left to play, a done one before any that plays, the first of
/// those done the same, but one still playing the player's own sound for another's. None when every
/// voice plays the player's own and the sound is another's.
[[nodiscard]] auto voiceFor(std::span<const VoiceUse> voices, std::optional<PlayerIndex> maker,
                            SoundChannel channel, nx::audio::MixFrames now) -> std::optional<std::size_t>;

} // namespace cs
