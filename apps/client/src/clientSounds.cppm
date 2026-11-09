// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the client plays on the system's sound output (docs/11-audio.md).
export module app.client:sounds;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.settings;
import cs.shared;
import cs.client;
import :client;
import :session;

namespace app {

/// What the client plays on the system's sound output: what the map played makes heard, the sounds the
/// console's `play` and `playvol` play, and the output `s_device` names, its gain `volume`'s; none without an
/// output, the game playing on, silent. It stays where it is made: its commands point into it.
class ClientSounds {
public:
    /// The system's default output, playing the sounds of `files` as `audioCvars` say; none, logged, without
    /// one. `files` and `audioCvars` must outlive it.
    ClientSounds(const nx::assets::Vfs& files, const nx::settings::AudioCvars& audioCvars);

    ClientSounds(const ClientSounds&) = delete;
    auto operator=(const ClientSounds&) -> ClientSounds& = delete;
    ClientSounds(ClientSounds&&) = delete;
    auto operator=(ClientSounds&&) -> ClientSounds& = delete;
    ~ClientSounds() = default;

    /// Registers in `console` `play`, `playvol` and `s_devices`. Errors: SettingsUnavailable, with the cause.
    [[nodiscard]] auto registerCommands(nx::console::Console& console) -> nx::Result<void, ClientError>;

    /// Plays on the output `s_device` names, when it names one: the default stays when none has that name,
    /// logged. Once the player's files and the command line ran.
    void openNamedOutput();
    /// Each frame: silent while the window is not `isFocused`, as the original is, but with
    /// `s_play_unfocused`; the gain of `volume` as it is now.
    void update(bool isFocused);
    /// Plays what the frame played of `session` made heard, as the player hears it (`hearing`).
    void hear(Session& session, const cs::Hearing& hearing);

    /// Reads the sounds the player hears of the weapons `changed` of `weapons`, whose view models `level`
    /// read (cs::soundFilesOf), so that no frame waits on a file of one it plays. One that cannot be read is
    /// logged, and the game plays on.
    void readWeapons(const cs::WeaponCatalog& weapons, const cs::Level& level,
                     std::span<const cs::WeaponIndex> changed);
    /// Reads the sounds of the player's feet, breath, harm, use and purchases of `content`, so that none
    /// waits on its file. One that cannot be read is logged, and the game plays on.
    void readPlayer(const cs::GameContent& content);

private:
    /// Plays the sound `name` names at `volume`, heard everywhere, as `play` and `playvol` do; without an
    /// output, `console` says so. Errors: CommandFailed.
    [[nodiscard]] auto play(nx::console::Console& console, std::string_view name, float volume)
        -> nx::Result<void, nx::console::ConsoleError>;

    const nx::assets::Vfs* vfs;
    const nx::settings::AudioCvars* audio;
    std::unique_ptr<cs::ClientAudio> output; ///< None without a sound output.
    nx::log::RateLimit failures;             ///< Of the sounds the map played makes heard.
};

} // namespace app
