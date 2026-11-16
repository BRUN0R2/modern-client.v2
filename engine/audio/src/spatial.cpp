// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.audio;

import std;
import nx.core;
import nx.math;

namespace nx::audio {

auto legacyGains(const Listener& listener, Vec3 source, Gain volume, Attenuation attenuation) noexcept
    -> VoiceGains {
    const Vec3 toSource = source - listener.position;
    const HammerUnits distance{length(toSource)};
    const float left = 1.0F - (distance / kNominalSoundReach * attenuation.value());
    // A sound that does not fade is not panned either: whether the original pans it is to be measured
    // on it (TODO.md), and no sound of the game is made so yet.
    const float side = attenuation > Attenuation{} && distance > HammerUnits{}
                           ? dot(listener.right, toSource) / distance.value()
                           : 0.0F;
    const auto earOf = [volume, left](float share) {
        return std::clamp(volume * (left * share), Gain{}, kFullGain);
    };
    return VoiceGains{.left = earOf(1.0F - side), .right = earOf(1.0F + side)};
}

} // namespace nx::audio
