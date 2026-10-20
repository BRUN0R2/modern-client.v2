// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;

#include "temporaryDirectory.hpp"

namespace {

using nx::assets::kMaxResourcePathBytes;
using nx::assets::Vfs;
using nx::assets::VfsError;
using nx::assets::VfsPath;

/// A path the test knows to be valid.
[[nodiscard]] auto pathOf(std::string_view text) -> VfsPath {
    auto parsed = VfsPath::parse(text);
    EXPECT_TRUE(parsed.has_value()) << nx::describe(parsed.error());
    return std::move(parsed).value();
}

[[nodiscard]] auto textOf(std::span<const std::byte> bytes) -> std::string {
    return bytes | std::views::transform([](std::byte byte) { return static_cast<char>(byte); }) |
           std::ranges::to<std::string>();
}

/// The content of `path` in `vfs`, or the description of the failure.
[[nodiscard]] auto contentOf(const Vfs& vfs, std::string_view path) -> std::string {
    const auto bytes = vfs.readAll(pathOf(path));
    return bytes ? textOf(*bytes) : nx::describe(bytes.error());
}

[[nodiscard]] auto mount(std::initializer_list<std::filesystem::path> directories) -> Vfs {
    const std::vector<std::filesystem::path> mounts{directories};
    auto vfs = Vfs::create(mounts);
    EXPECT_TRUE(vfs.has_value()) << nx::describe(vfs.error());
    return std::move(vfs).value();
}

TEST(VfsPathTest, AcceptsLogicalPaths) {
    for (const std::string_view text :
         {"maps/de_dust2.bsp", "sound/weapons/ak47-1.wav", "liblist.gam", "Models/Player/GIGN/gign.MDL",
          "sprites/hud.txt", "gfx/env/desertup.tga", "sound/a\xC3\xA7\xC3\xA3o.ogg", "maps/my map.bsp",
          "a.b/c.d/e.toml"}) {
        const auto path = VfsPath::parse(text);
        ASSERT_TRUE(path.has_value()) << text << ": " << nx::describe(path.error());
        EXPECT_EQ(path->text(), text) << "the path keeps its spelling";
    }
}

TEST(VfsPathTest, RejectsEveryBrokenRule) {
    for (const std::string_view text : std::initializer_list<std::string_view>{
             "", "maps/de_dust2.bsp\n", std::string_view{"maps/\0.bsp", 10}, "maps\\de_dust2.bsp",
             "C:/maps/x.bsp", "maps/x.bsp:stream.txt", "/maps/x.bsp", "maps//x.bsp", "maps/", "../x.bsp",
             "maps/../../x.bsp", "./x.bsp", "maps/x", "maps/x.", "dlls/mp.dll", "x.exe", "scripts/x.sh"}) {
        const auto path = VfsPath::parse(text);
        ASSERT_FALSE(path.has_value()) << std::format("{:?}", text);
        EXPECT_EQ(path.error().code, VfsError::InvalidPath);
    }
}

TEST(VfsPathTest, AGoldsrcPathMaySeparateWithBackslashes) {
    const auto path = VfsPath::parseGoldsrc("models\\hostage01.mdl");
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_EQ(path->text(), "models/hostage01.mdl");

    for (const std::string_view text : {"..\\x.bsp", "\\maps\\x.bsp", "C:\\maps\\x.bsp"}) {
        const auto broken = VfsPath::parseGoldsrc(text);
        ASSERT_FALSE(broken.has_value()) << text;
        EXPECT_EQ(broken.error().code, VfsError::InvalidPath);
    }
}

TEST(VfsPathTest, TheCeilingIsInclusive) {
    const std::string extension = ".wav";
    EXPECT_TRUE(
        VfsPath::parse(std::string(kMaxResourcePathBytes - extension.size(), 'a') + extension).has_value());

    const std::string over = std::string(kMaxResourcePathBytes + 1 - extension.size(), 'a') + extension;
    const auto path = VfsPath::parse(over);
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, VfsError::InvalidPath);
}

TEST(VfsPathTest, HostileTextIsNotRepeatedInTheFailure) {
    constexpr std::size_t kHostileBytes = 1 << 20;
    const auto path = VfsPath::parse(std::string(kHostileBytes, '/'));
    ASSERT_FALSE(path.has_value());
    EXPECT_LT(path.error().detail.size(), kMaxResourcePathBytes) << path.error().detail;
}

TEST(VfsTest, ReadsAFileWhateverTheCase) {
    const nx::test::TemporaryDirectory cstrike;
    cstrike.write("Maps/DE_Dust2.BSP", "dust2");

    const Vfs vfs = mount({cstrike.path()});
    EXPECT_EQ(contentOf(vfs, "maps/de_dust2.bsp"), "dust2");
    EXPECT_EQ(contentOf(vfs, "MAPS/de_dust2.bsp"), "dust2");
    EXPECT_EQ(contentOf(vfs, "Maps/DE_Dust2.BSP"), "dust2");
}

TEST(VfsTest, ThePathResolvesToTheFirstMountThatHasIt) {
    const nx::test::TemporaryDirectory addons;
    const nx::test::TemporaryDirectory cstrike;
    addons.write("sound/radio/go.wav", "custom go");
    cstrike.write("sound/radio/go.wav", "original go");
    cstrike.write("sound/radio/fallback.wav", "fall back");

    const Vfs vfs = mount({addons.path(), cstrike.path()});
    EXPECT_EQ(contentOf(vfs, "sound/radio/go.wav"), "custom go");
    EXPECT_EQ(contentOf(vfs, "sound/radio/fallback.wav"), "fall back");

    const Vfs reversed = mount({cstrike.path(), addons.path()});
    EXPECT_EQ(contentOf(reversed, "sound/radio/go.wav"), "original go");
}

TEST(VfsTest, ListsEveryPathOnceAsItsMountSpellsIt) {
    const nx::test::TemporaryDirectory addons;
    const nx::test::TemporaryDirectory cstrike;
    addons.write("Sound/Radio/Go.wav", "custom go");
    cstrike.write("sound/radio/go.wav", "original go");
    cstrike.write("maps/de_dust2.bsp", "dust2");
    cstrike.write("readme", "not content");

    const Vfs vfs = mount({addons.path(), cstrike.path()});
    const auto texts = vfs.paths() |
                       std::views::transform([](const VfsPath& path) { return std::string{path.text()}; }) |
                       std::ranges::to<std::vector>();
    EXPECT_EQ(texts, (std::vector<std::string>{"maps/de_dust2.bsp", "Sound/Radio/Go.wav"}));

    const auto native = vfs.nativePath(pathOf("sound/radio/go.wav"));
    ASSERT_TRUE(native.has_value()) << nx::describe(native.error());
    EXPECT_EQ(*native, addons.path() / "Sound/Radio/Go.wav");
    const auto missing = vfs.nativePath(pathOf("maps/de_nuke.bsp"));
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, VfsError::NotFound);
}

TEST(VfsTest, APathNoMountHasIsNotFound) {
    const nx::test::TemporaryDirectory cstrike;
    cstrike.write("maps/de_dust2.bsp", "dust2");

    const Vfs vfs = mount({cstrike.path()});
    const auto bytes = vfs.readAll(pathOf("maps/de_nuke.bsp"));
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, VfsError::NotFound);
    EXPECT_NE(bytes.error().detail.find("maps/de_nuke.bsp"), std::string::npos) << bytes.error().detail;
}

TEST(VfsTest, FilesThatAreNotContentAreLeftOut) {
    const nx::test::TemporaryDirectory install;
    install.write("dlls/mp.dll", "binary");
    install.write("dlls/MP.DLL", "binary"); // a case collision among files that are not content
    install.write("readme", "text");
    install.write("liblist.gam", "game \"Counter-Strike\"");

    const Vfs vfs = mount({install.path()});
    EXPECT_EQ(contentOf(vfs, "liblist.gam"), "game \"Counter-Strike\"");
}

TEST(VfsTest, AMissingDirectoryFailsToMount) {
    const nx::test::TemporaryDirectory parent;
    const std::vector<std::filesystem::path> mounts{parent.path() / "missing"};

    const auto vfs = Vfs::create(mounts);
    ASSERT_FALSE(vfs.has_value());
    EXPECT_EQ(vfs.error().code, VfsError::MountFailed);
    ASSERT_EQ(vfs.error().cause.size(), 1U);
    EXPECT_EQ(vfs.error().cause.records()[0].domain, "FileError");
    EXPECT_EQ(vfs.error().cause.records()[0].code, "NotFound");
}

TEST(VfsTest, TwoFilesThatDifferOnlyInCaseFailToMount) {
    const nx::test::TemporaryDirectory cstrike;
    cstrike.write("maps/de_dust2.bsp", "lower");
    cstrike.write("maps/DE_DUST2.bsp", "upper");
    const auto files = nx::platform::listFiles(cstrike.path());
    ASSERT_TRUE(files.has_value()) << nx::describe(files.error());
    if (files->size() == 1) {
        GTEST_SKIP() << "the file system ignores case: two such files cannot exist";
    }

    const std::vector<std::filesystem::path> mounts{cstrike.path()};
    const auto vfs = Vfs::create(mounts);
    ASSERT_FALSE(vfs.has_value());
    EXPECT_EQ(vfs.error().code, VfsError::MountFailed);
    EXPECT_NE(vfs.error().detail.find("differ only in case"), std::string::npos) << vfs.error().detail;

    const nx::test::TemporaryDirectory addons;
    addons.write("maps/de_dust2.bsp", "custom");
    const Vfs shadowed = mount({addons.path(), cstrike.path()});
    EXPECT_EQ(contentOf(shadowed, "maps/de_dust2.bsp"), "custom") << "a higher mount resolves the path";
}

TEST(VfsTest, AFileRemovedAfterMountingFailsToRead) {
    const nx::test::TemporaryDirectory cstrike;
    cstrike.write("maps/de_dust2.bsp", "dust2");
    const Vfs vfs = mount({cstrike.path()});
    std::error_code error;
    std::filesystem::remove(cstrike.path() / "maps/de_dust2.bsp", error);
    ASSERT_FALSE(error) << error.message();

    const auto bytes = vfs.readAll(pathOf("maps/de_dust2.bsp"));
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, VfsError::ReadFailed);
    ASSERT_EQ(bytes.error().cause.size(), 1U);
    EXPECT_EQ(bytes.error().cause.records()[0].code, "NotFound");
}

TEST(VfsTest, ManyThreadsReadAtOnce) {
    constexpr int kThreads = 4;
    constexpr int kReadsPerThread = 50;
    const nx::test::TemporaryDirectory cstrike;
    cstrike.write("maps/de_dust2.bsp", "dust2");
    cstrike.write("maps/de_inferno.bsp", "inferno");
    const Vfs vfs = mount({cstrike.path()});

    std::array<int, kThreads> mismatches{};
    {
        std::vector<std::jthread> threads;
        for (int thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back([&vfs, &mismatches, thread] {
                for (int index = 0; index < kReadsPerThread; ++index) {
                    const bool isDust = (index + thread) % 2 == 0;
                    const std::string content =
                        contentOf(vfs, isDust ? "maps/de_dust2.bsp" : "maps/de_inferno.bsp");
                    if (content != (isDust ? "dust2" : "inferno")) {
                        ++mismatches.at(static_cast<std::size_t>(thread));
                    }
                }
            });
        }
    }
    EXPECT_EQ(mismatches, (std::array<int, kThreads>{}));
}

} // namespace
