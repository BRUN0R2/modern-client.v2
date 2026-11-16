// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where a sound is heard from (docs/11-audio.md, section 5): how loud a sound made somewhere is in
/// each ear of who hears it, as the original's stereo spatialization makes it (`s_spatial legacy`).
/// Its behavior was learned from the GPL engines that reimplement it (Quake's and Xash3D's, consulted,
/// not copied: docs/20-legal-and-licensing.md, section 3): the volume falls in a straight line with
/// the distance, and the ear the sound is on gets more of it, the other less.
export module nx.audio:spatial;

import std;
import nx.core;
import nx.math;
import :mixer;

export namespace nx::audio {

/// Who hears: where its ears are, and the way to its right, of length 1 (AngleVectors::right of the
/// view it sees by).
struct Listener {
    Vec3 position{};
    Vec3 right{};
};

/// How loud in each ear `listener` hears a sound of `volume`, at most kFullGain, made at `source`, that
/// fades with the distance by `attenuation` (none: heard everywhere at its volume), as the original's
/// legacy spatialization makes it: its volume times what is left of it at that distance (1 less the
/// distance over kNominalSoundReach times `attenuation`), times 1 more, in the ear it is on, or 1 less,
/// in the other, the share of the way to it that goes to the listener's right; each ear from none to
/// kFullGain. One with no attenuation is heard the same in both ears; one made where the listener is,
/// too.
[[nodiscard]] auto legacyGains(const Listener& listener, Vec3 source, Gain volume,
                               Attenuation attenuation) noexcept -> VoiceGains;

} // namespace nx::audio
