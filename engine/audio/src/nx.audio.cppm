// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.audio — the game's sound (docs/11-audio.md, layer 3 in docs/01-architecture.md): one mixer for
/// everything, on its own thread, fed by commands.
///
/// Today: sounds made of WAV files, read by the original's names, and the mixer: voices at any rate
/// and pitch through a windowed sinc, float mixing with headroom, and a limiter, played by the sound
/// device of nx.platform. Spatialization, the room's DSP, streaming and voice come next (TODO.md).
export module nx.audio;

export import :sound;
export import :library;
export import :mixer;
