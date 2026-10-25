// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the player hears: a sound read before it is played plays without reading its file again, so
// that the frame that plays it never waits on the disk; one that cannot be read is told.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.console;
import cs.shared;
import cs.client;

#include "consoleFixtures.hpp"
#include "temporaryDirectory.hpp"
#include "wavFile.hpp"

namespace {

constexpr std::string_view kShot = "weapons/shot.wav";
constexpr std::string_view kClick = "weapons/click.wav";

/// The sounds kShot and kClick of the game's content, and `volume` in a console.
class ClientAudioTest : public testing::Test {
protected:
    ClientAudioTest() {
        for (const std::string_view sound : {kShot, kClick}) {
            m_folder.write(fileOf(sound), nx::test::wavFile("\x80\xC0\x40"));
        }
        const std::array mounts{m_folder.path()};
        auto vfs = nx::assets::Vfs::create(mounts);
        EXPECT_TRUE(vfs.has_value()) << (vfs ? "" : nx::describe(vfs.error()));
        if (vfs) {
            m_vfs.emplace(std::move(*vfs));
        }
        auto volume =
            m_console.registerCvar(nx::console::CvarDesc<float>{.name = "volume", .defaultValue = 1.0F});
        EXPECT_TRUE(volume.has_value());
        if (volume) {
            m_volume.emplace(*volume);
        }
    }

    /// The file of the sound `name` names in the content's folder.
    [[nodiscard]] static auto fileOf(std::string_view name) -> std::filesystem::path {
        return std::filesystem::path{"sound"} / name;
    }

    /// Takes the file of the sound `name` names away from the disk.
    void removeFile(std::string_view name) const {
        std::error_code error;
        EXPECT_TRUE(std::filesystem::remove(m_folder.path() / fileOf(name), error)) << error.message();
    }

    [[nodiscard]] auto open() const -> std::unique_ptr<cs::ClientAudio> {
        auto audio = cs::ClientAudio::open(*m_vfs, *m_volume);
        EXPECT_TRUE(audio.has_value()) << (audio ? "" : nx::describe(audio.error()));
        return audio ? std::move(*audio) : nullptr;
    }

    nx::test::TemporaryDirectory m_folder;
    std::optional<nx::assets::Vfs> m_vfs;
    nx::test::RecordingOutput m_output;
    nx::test::MemoryFiles m_files;
    nx::console::Console m_console{m_output, m_files};
    std::optional<nx::console::Cvar<float>> m_volume;
};

TEST_F(ClientAudioTest, ASoundReadBeforeItIsPlayedPlaysWithoutItsFile) {
    const std::unique_ptr<cs::ClientAudio> audio = open();
    ASSERT_NE(audio, nullptr);
    const auto read = audio->preload(kShot);
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    removeFile(kShot);
    removeFile(kClick);
    const auto shot =
        audio->play(cs::GameSound{.files = {std::string{kShot}}, .channel = cs::SoundChannel::Weapon}, 1.0F);
    EXPECT_TRUE(shot.has_value()) << "its file is not read again: " << nx::describe(shot.error());
    // One not read before is read as it plays, and its file is gone.
    const auto click = audio->playLocal(kClick, 1.0F);
    ASSERT_FALSE(click.has_value());
    EXPECT_EQ(click.error().code, cs::PlayError::NoSound);
}

TEST_F(ClientAudioTest, ASoundThatCannotBeReadIsTold) {
    const std::unique_ptr<cs::ClientAudio> audio = open();
    ASSERT_NE(audio, nullptr);
    const auto read = audio->preload("weapons/missing.wav");
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, cs::PlayError::NoSound);
}

} // namespace
