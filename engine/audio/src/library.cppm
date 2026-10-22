// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game's sounds by the names the original gives them (docs/11-audio.md, section 6): paths under
/// `sound/`, read the first time they are asked for and kept, so that the mixer plays them from
/// memory that stays where it is.
export module nx.audio:library;

import std;
import nx.core;
import nx.assets;
import :sound;

export namespace nx::audio {

/// What can go wrong reading a sound (docs/11-audio.md, section 9).
enum class SoundError : std::uint8_t {
    NotFound,     ///< No file has the sound's path, or its name is no path (cause: the VfsError).
    ReadFailed,   ///< Its file could not be read (cause: the VfsError).
    DecodeFailed, ///< Its file is not a WAV file the reader takes (cause: the WavError).
};

/// Sounds read from the game's content, each once, kept at one address until the library goes:
/// whatever plays them must stop first.
class SoundLibrary {
public:
    /// The folder of the content the original's sound names are relative to.
    static constexpr std::string_view kFolder = "sound/";

    /// A library reading from `vfs`, which must outlive it.
    explicit SoundLibrary(const assets::Vfs& vfs) noexcept : m_vfs{&vfs} {}

    /// The sound `name` names ("weapons/ak47-1.wav", `\` a separator too, any case), read from
    /// kFolder the first time. A sound that could not be read is tried again the next time.
    /// Errors: NotFound, ReadFailed, DecodeFailed.
    [[nodiscard]] auto load(std::string_view name) -> Result<const Sound*, SoundError>;

    /// How many sounds it has read.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return m_sounds.size(); }

private:
    const assets::Vfs* m_vfs;
    std::unordered_map<std::string, std::unique_ptr<const Sound>> m_sounds; ///< By name, in lowercase.
};

} // namespace nx::audio
