// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.audio;
import cs.shared;

namespace cs {

auto voiceFor(std::span<const VoiceUse> voices, std::optional<PlayerIndex> maker, SoundChannel channel,
              nx::audio::MixFrames now) -> std::optional<std::size_t> {
    if (channel != SoundChannel::Auto) {
        const auto same = std::ranges::find_if(voices, [maker, channel](const VoiceUse& use) {
            return use.maker == maker && use.channel == channel;
        });
        if (same != voices.end()) {
            return nx::narrow<std::size_t>(std::ranges::distance(voices.begin(), same));
        }
    }
    // The least left to play is the soonest done, whatever the time now: those done already first. The
    // original keeps its view entity's sounds from the others' (SND_PickChannel).
    std::optional<std::size_t> chosen;
    for (std::size_t voice = 0; voice < voices.size(); ++voice) {
        const VoiceUse& use = voices[voice];
        const bool isPlayersOwn = !use.maker && use.doneAt > now;
        if (maker && isPlayersOwn) {
            continue;
        }
        if (!chosen || use.doneAt < voices[*chosen].doneAt) {
            chosen = voice;
        }
    }
    return chosen;
}

} // namespace cs
