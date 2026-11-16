// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Which voice of the mixer a sound the client plays takes (docs/11-audio.md, section 3): as the
/// original's engine chooses a channel for one, a sound on a channel of its maker replaces the one its
/// maker plays there, and any other takes the voice with the least left to play. Unlike the original,
/// whose dynamic channels are few, every voice of the mixer is one to choose from.
export module cs.client:voices;

import std;
import nx.audio;
import cs.shared;

export namespace cs {

/// What a voice of the client's mixer was last given to play: the channel it plays on, and the frame of
/// the mix by which its sound is done (nx::audio::Mixer::framesMixed), none for one never given any.
struct VoiceUse {
    SoundChannel channel = SoundChannel::Auto;
    nx::audio::MixFrames doneAt{};
};

/// The voice of `voices`, which holds one at least, that a sound on `channel` takes, as the original's
/// engine chooses a channel for a sound: the voice that last played on `channel`, whose sound it
/// replaces, but on SoundChannel::Auto, where each sound takes one of its own; else the voice with the
/// least left to play, a done one before any that plays, the first of those done the same.
[[nodiscard]] auto voiceFor(std::span<const VoiceUse> voices, SoundChannel channel) -> std::size_t;

} // namespace cs
