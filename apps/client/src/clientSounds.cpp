// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.assets;
import nx.settings;
import cs.shared;
import cs.client;

namespace app {

namespace {

using nx::console::ConsoleError;

constexpr nx::log::Category kLog{"client"};
/// A sound that could not be played is reported once in this long, with how many others were not.
constexpr std::chrono::seconds kSoundFailureInterval{5};

/// `name` as the original's `play` reads it: a WAV file's when it has no extension.
[[nodiscard]] auto soundFileOf(std::string_view name) -> std::string {
    const std::size_t dot = name.rfind('.');
    const std::size_t separator = name.find_last_of("/\\");
    const bool hasExtension =
        dot != std::string_view::npos && (separator == std::string_view::npos || dot > separator);
    return hasExtension ? std::string{name} : std::format("{}.wav", name);
}

} // namespace

ClientSounds::ClientSounds(const nx::assets::Vfs& files, const nx::settings::AudioCvars& audioCvars)
    : vfs{&files}, audio{&audioCvars}, failures{kSoundFailureInterval} {
    if (auto opened = cs::ClientAudio::open(files, audioCvars.volume); opened) {
        output = std::move(*opened);
    } else {
        nx::log::warning(kLog, "no sound: {}", nx::describe(opened.error()));
    }
}

auto ClientSounds::registerCommands(nx::console::Console& console) -> nx::Result<void, ClientError> {
    using nx::console::CommandLine;
    using nx::console::Console;
    const auto add = [&console](nx::console::CommandDesc desc, nx::console::CommandHandler handler) {
        return console.registerCommand(desc, std::move(handler));
    };
    for (auto&& registered :
         {add({.name = "play", .description = "Plays a sound heard everywhere: play <sound>."},
              [this](Console& on, const CommandLine& line) -> nx::Result<void, ConsoleError> {
                  if (line.arguments().size() != 1) {
                      return nx::fail(ConsoleError::InvalidArguments, "usage: play <sound>");
                  }
                  return play(on, line.arguments().front(), kFullVolume);
              }),
          add({.name = "playvol",
               .description =
                   "Plays a sound heard everywhere at a volume from 0 to 1: playvol <sound> <volume>."},
              [this](Console& on, const CommandLine& line) -> nx::Result<void, ConsoleError> {
                  constexpr std::string_view kUsage = "usage: playvol <sound> <volume>";
                  constexpr std::size_t kSoundAndVolume = 2; // the words it takes
                  if (line.arguments().size() != kSoundAndVolume) {
                      return nx::fail(ConsoleError::InvalidArguments, std::string{kUsage});
                  }
                  const std::string& text = line.arguments()[1];
                  float volume = 0.0F;
                  if (const auto [end, error] =
                          std::from_chars(text.data(), text.data() + text.size(), volume);
                      error != std::errc{} || end != text.data() + text.size()) {
                      return nx::fail(ConsoleError::InvalidArguments, std::string{kUsage});
                  }
                  return play(on, line.arguments().front(), volume);
              }),
          add({.name = "s_devices", .description = "Lists the sound outputs s_device can name."},
              [](Console& on, const CommandLine&) -> nx::Result<void, ConsoleError> {
                  auto names = nx::platform::audioOutputNames();
                  if (!names) {
                      return nx::fail(ConsoleError::CommandFailed, "s_devices", std::move(names.error()));
                  }
                  for (const std::string& name : *names) {
                      on.print(std::format("\"{}\"", name));
                  }
                  return {};
              })}) {
        if (!registered) {
            return nx::fail(ClientError::SettingsUnavailable, registered.error());
        }
    }
    return {};
}

void ClientSounds::openNamedOutput() {
    const std::string& name = audio->device.value();
    if (name.empty()) {
        return;
    }
    auto opened = cs::ClientAudio::open(*vfs, audio->volume, name);
    if (!opened) {
        nx::log::warning(kLog, "s_device {:?}: {}; the default plays", name, nx::describe(opened.error()));
        return;
    }
    output = std::move(*opened);
}

void ClientSounds::update(bool isFocused) {
    if (output == nullptr) {
        return;
    }
    // The original is silent while its window is not focused.
    const bool isHeard = isFocused || audio->playUnfocused.value();
    output->setMuting(isHeard ? cs::Muting::Off : cs::Muting::On);
    output->update();
}

void ClientSounds::hear(Session& session, const cs::Hearing& hearing) {
    hearSession(session, output.get(), hearing, failures);
}

void ClientSounds::readWeapons(const cs::WeaponCatalog& weapons, const cs::Level& level,
                               std::span<const cs::WeaponIndex> changed) {
    if (output == nullptr) {
        return;
    }
    for (const cs::WeaponIndex weapon : changed) {
        if (!weapons.isComplete(weapon)) {
            continue; // it makes no sound until its values make a weapon, which changes it again
        }
        const cs::Weapon& made = weapons.weapon(weapon);
        for (const std::string_view file : cs::soundFilesOf(made, level.viewModel(weapon))) {
            if (auto read = output->preload(file); !read) {
                nx::log::warning(kLog, "a sound of {} cannot be read: {}", made.id,
                                 nx::describe(read.error()));
            }
        }
    }
}

void ClientSounds::readPlayer(const cs::GameContent& content) {
    if (output == nullptr) {
        return;
    }
    for (const auto& [what, files] :
         {std::pair{"footsteps", content.footsteps.soundFiles()},
          std::pair{"breath", content.breath.soundFiles()},
          std::pair{"harm", content.playerHarm.soundFiles()}, std::pair{"use", content.use.soundFiles()},
          std::pair{"purchases", content.buying.soundFiles()}}) {
        for (const std::string_view file : files) {
            if (auto read = output->preload(file); !read) {
                nx::log::warning(kLog, "a sound of the {} cannot be read: {}", what,
                                 nx::describe(read.error()));
            }
        }
    }
}

auto ClientSounds::play(nx::console::Console& console, std::string_view name, float volume)
    -> nx::Result<void, ConsoleError> {
    if (output == nullptr) {
        console.print("Can't play a sound, no sound output");
        return {};
    }
    const std::string file = soundFileOf(name);
    if (auto played = output->playLocal(file, volume); !played) {
        return nx::fail(ConsoleError::CommandFailed, std::format("play {}", file), std::move(played.error()));
    }
    return {};
}

} // namespace app
