// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the player hears: a sound read before it is played plays without reading its file again, so
// that the frame that plays it never waits on the disk; one that cannot be read is told.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.audio;
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
    const auto shot = audio->play(
        cs::GameSound{.files = {std::string{kShot}}, .channel = cs::SoundChannel::Weapon}, nx::kFullGain);
    EXPECT_TRUE(shot.has_value()) << "its file is not read again: " << nx::describe(shot.error());
    // One not read before is read as it plays, and its file is gone.
    const auto click = audio->playLocal(kClick, nx::kFullGain);
    ASSERT_FALSE(click.has_value());
    EXPECT_EQ(click.error().code, cs::PlayError::NoSound);
}

/// At the origin, looking along +x: its right is -y.
constexpr nx::audio::Listener kListener{.position = {}, .right = {.y = -1.0F}};
constexpr float kTolerance = 1e-5F;

/// A sound of full volume and ATTN_NORM's made at `position`, heard as kListener hears it there.
[[nodiscard]] auto placedAt(nx::Vec3 position) -> cs::PlacedSound {
    cs::PlacedSound sound{.position = position};
    sound.gains = nx::audio::legacyGains(kListener, position, sound.volume, sound.attenuation);
    return sound;
}

TEST(FollowSoundTest, ASoundIsHeardWhereItsMakerIsNow) {
    cs::PlacedSound sound = placedAt({.y = 250.0F}); // to the left
    EXPECT_EQ(sound.gains.right.value(), 0.0F);
    const std::optional<nx::audio::VoiceGains> gains =
        cs::followSound(sound, nx::Vec3{.y = -250.0F}, kListener);
    ASSERT_TRUE(gains.has_value());
    EXPECT_EQ(gains->left.value(), 0.0F) << "now to the right";
    EXPECT_NEAR(gains->right.value(), 1.0F, kTolerance);
    EXPECT_EQ(sound.position, (nx::Vec3{.y = -250.0F}));
    EXPECT_EQ(sound.gains, *gains);
}

TEST(FollowSoundTest, ASoundWhoseMakerIsGoneStaysWhereItWasAsTheListenerTurns) {
    cs::PlacedSound sound = placedAt({.y = 250.0F});
    const nx::audio::Listener turned{.position = {}, .right = {.y = 1.0F}}; // facing -x
    const std::optional<nx::audio::VoiceGains> gains = cs::followSound(sound, std::nullopt, turned);
    ASSERT_TRUE(gains.has_value());
    EXPECT_NEAR(gains->right.value(), 1.0F, kTolerance) << "on the right of who turned about";
    EXPECT_EQ(gains->left.value(), 0.0F);
    EXPECT_EQ(sound.position, (nx::Vec3{.y = 250.0F}));
}

TEST(FollowSoundTest, AChangeNoEarOfTheOriginalTellsApartIsNotSentUntilItAddsUp) {
    // Ahead, 1000 units away: a unit further fades it by 0.0008, less than a 255th.
    cs::PlacedSound sound = placedAt({.x = 1000.0F});
    EXPECT_FALSE(cs::followSound(sound, nx::Vec3{.x = 1001.0F}, kListener).has_value());
    EXPECT_FLOAT_EQ(sound.gains.left.value(), 0.2F) << "the gains it was last heard at kept";
    EXPECT_FALSE(cs::followSound(sound, nx::Vec3{.x = 1004.0F}, kListener).has_value());
    const std::optional<nx::audio::VoiceGains> gains =
        cs::followSound(sound, nx::Vec3{.x = 1005.0F}, kListener);
    ASSERT_TRUE(gains.has_value()) << "5 units further: 0.004, more than a 255th";
    EXPECT_NEAR(gains->left.value(), 0.196F, kTolerance);
}

TEST_F(ClientAudioTest, ASoundThatCannotBeReadIsTold) {
    const std::unique_ptr<cs::ClientAudio> audio = open();
    ASSERT_NE(audio, nullptr);
    const auto read = audio->preload("weapons/missing.wav");
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, cs::PlayError::NoSound);
}

} // namespace
