// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.audio;
import cs.shared;

namespace cs {

auto voiceFor(std::span<const VoiceUse> voices, SoundChannel channel) -> std::size_t {
    nx::expects(!voices.empty(), "voiceFor: a voice to choose from");
    if (channel != SoundChannel::Auto) {
        if (const auto same = std::ranges::find(voices, channel, &VoiceUse::channel); same != voices.end()) {
            return nx::narrow<std::size_t>(std::ranges::distance(voices.begin(), same));
        }
    }
    // The least left to play is the soonest done, whatever the time now: those done already first.
    return nx::narrow<std::size_t>(
        std::ranges::distance(voices.begin(), std::ranges::min_element(voices, {}, &VoiceUse::doneAt)));
}

} // namespace cs
