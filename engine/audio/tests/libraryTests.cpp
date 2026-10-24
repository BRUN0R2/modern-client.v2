// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The game's sounds by their names: read from `sound/` once and kept, and what goes wrong reported.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.audio;

#include "temporaryDirectory.hpp"
#include "wavFile.hpp"

namespace {

using nx::SampleRate;
using nx::assets::Vfs;
using nx::audio::SoundError;
using nx::audio::SoundLibrary;
using nx::test::wavFile;

class SoundLibraryTest : public testing::Test {
protected:
    SoundLibraryTest() {
        m_folder.write("sound/weapons/click.wav", wavFile("\x80\xC0\x40"));
        m_folder.write("sound/broken.wav", "RIFF");
        const std::array mounts{m_folder.path()};
        auto vfs = Vfs::create(mounts);
        EXPECT_TRUE(vfs.has_value());
        m_vfs.emplace(std::move(*vfs));
    }

    nx::test::TemporaryDirectory m_folder;
    std::optional<Vfs> m_vfs;
};

TEST_F(SoundLibraryTest, ASoundIsReadFromItsFolderOnceAndKept) {
    SoundLibrary library{*m_vfs};
    const auto click = library.load("weapons/click.wav");
    ASSERT_TRUE(click.has_value()) << nx::describe(click.error());
    EXPECT_EQ((*click)->frames(), 3U);
    EXPECT_EQ((*click)->rate(), SampleRate{11'025});
    EXPECT_FLOAT_EQ((*click)->samples()[1], 0.5F);
    // The same sound, however its name is written.
    const auto again = library.load("Weapons\\CLICK.wav");
    ASSERT_TRUE(again.has_value()) << nx::describe(again.error());
    EXPECT_EQ(*again, *click);
    EXPECT_EQ(library.size(), 1U);
}

TEST_F(SoundLibraryTest, WhatCannotBeReadIsReportedAndNotKept) {
    SoundLibrary library{*m_vfs};
    const auto missing = library.load("weapons/missing.wav");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, SoundError::NotFound);
    const auto noPath = library.load("../outside.wav");
    ASSERT_FALSE(noPath.has_value());
    EXPECT_EQ(noPath.error().code, SoundError::NotFound);
    const auto broken = library.load("broken.wav");
    ASSERT_FALSE(broken.has_value());
    EXPECT_EQ(broken.error().code, SoundError::DecodeFailed);
    EXPECT_EQ(library.size(), 0U);
}

} // namespace
