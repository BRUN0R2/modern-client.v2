// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: a WAV file built byte by byte, as the sound tests read and play it. Include after
// `import std;`.

namespace nx::test {

/// A WAV file of `samples` 8-bit mono samples at 11 025 Hz.
[[nodiscard]] inline auto wavFile(std::string_view samples) -> std::string {
    std::string file;
    const auto append32 = [&file](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            file.push_back(static_cast<char>((value >> shift) & 0xFFU));
        }
    };
    const auto append16 = [&file](std::uint16_t value) {
        file.push_back(static_cast<char>(value & 0xFFU));
        file.push_back(static_cast<char>(value >> 8));
    };
    file += "RIFF";
    append32(0); // the size, which the reader does not trust
    file += "WAVEfmt ";
    append32(16);
    append16(1);      // PCM
    append16(1);      // mono
    append32(11'025); // frames a second
    append32(11'025); // bytes a second
    append16(1);      // bytes a frame
    append16(8);      // bits a sample
    file += "data";
    append32(static_cast<std::uint32_t>(samples.size()));
    file += samples;
    return file;
}

} // namespace nx::test
