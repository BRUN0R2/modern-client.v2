// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.audio;

import std;
import nx.core;
import nx.assets;

namespace nx::audio {

auto SoundLibrary::load(std::string_view name) -> Result<const Sound*, SoundError> {
    std::string key{name};
    std::ranges::transform(key, key.begin(),
                           [](char character) { return character == '\\' ? '/' : toLowerAscii(character); });
    if (const auto found = m_sounds.find(key); found != m_sounds.end()) {
        return found->second.get();
    }
    auto path = assets::VfsPath::parseGoldsrc(std::format("{}{}", kFolder, name));
    if (!path) {
        return fail(SoundError::NotFound, std::string{name}, std::move(path.error()));
    }
    auto bytes = m_vfs->readAll(*path);
    if (!bytes) {
        const SoundError code =
            bytes.error().code == assets::VfsError::NotFound ? SoundError::NotFound : SoundError::ReadFailed;
        return fail(code, std::string{path->text()}, std::move(bytes.error()));
    }
    auto wav = assets::parseWav(*bytes);
    if (!wav) {
        return fail(SoundError::DecodeFailed, std::string{path->text()}, std::move(wav.error()));
    }
    auto sound = std::make_unique<const Sound>(Sound::of(*wav));
    const Sound* const loaded = sound.get();
    m_sounds.emplace(std::move(key), std::move(sound));
    return loaded;
}

} // namespace nx::audio
