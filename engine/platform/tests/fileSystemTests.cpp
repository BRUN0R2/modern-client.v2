// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;

#include "temporaryDirectory.hpp"

namespace {

using nx::platform::FileError;
using nx::platform::fromUtf8;
using nx::platform::listFiles;
using nx::platform::readFile;
using nx::platform::toUtf8;

[[nodiscard]] auto bytesOf(std::string_view text) -> std::vector<std::byte> {
    return text | std::views::transform([](char character) { return static_cast<std::byte>(character); }) |
           std::ranges::to<std::vector>();
}

TEST(FileSystemTest, ToUtf8UsesSlashesAndKeepsTheText) {
    const std::filesystem::path path = std::filesystem::path{u8"sound"} / u8"ação.wav";
    EXPECT_EQ(toUtf8(path), "sound/a\xC3\xA7\xC3\xA3o.wav");
}

TEST(FileSystemTest, FromUtf8IsTheInverseOfToUtf8) {
    const std::string text = "sound/a\xC3\xA7\xC3\xA3o.wav";
    EXPECT_EQ(fromUtf8(text), std::filesystem::path{u8"sound"} / u8"a\u00e7\u00e3o.wav");
    EXPECT_EQ(toUtf8(fromUtf8(text)), text);
}

TEST(FileSystemTest, ListsTheFilesOfEverySubdirectorySorted) {
    const nx::test::TemporaryDirectory directory;
    directory.write("sound/weapons/ak47-1.wav", "a");
    directory.write("maps/de_dust2.bsp", "b");
    directory.write("liblist.gam", "c");
    directory.makeDirectory("empty");

    const auto files = listFiles(directory.path());
    ASSERT_TRUE(files.has_value()) << nx::describe(files.error());
    std::vector<std::string> listed;
    for (const auto& file : *files) {
        listed.push_back(toUtf8(file));
    }
    EXPECT_EQ(listed,
              (std::vector<std::string>{"liblist.gam", "maps/de_dust2.bsp", "sound/weapons/ak47-1.wav"}));
}

TEST(FileSystemTest, ListingSomethingOtherThanADirectoryFails) {
    const nx::test::TemporaryDirectory directory;
    directory.write("file.txt", "x");

    const auto missing = listFiles(directory.path() / "missing");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, FileError::NotFound);

    const auto file = listFiles(directory.path() / "file.txt");
    ASSERT_FALSE(file.has_value());
    EXPECT_EQ(file.error().code, FileError::NotADirectory);
}

TEST(FileSystemTest, ReadsAWholeFileByteForByte) {
    const nx::test::TemporaryDirectory directory;
    const std::string_view content{"\x00\x01\xFF\r\n binary", 11};
    directory.write("data.bin", content);
    directory.write("empty.txt", "");

    const auto bytes = readFile(directory.path() / "data.bin");
    ASSERT_TRUE(bytes.has_value()) << nx::describe(bytes.error());
    EXPECT_EQ(*bytes, bytesOf(content));

    const auto empty = readFile(directory.path() / "empty.txt");
    ASSERT_TRUE(empty.has_value()) << nx::describe(empty.error());
    EXPECT_TRUE(empty->empty());
}

TEST(FileSystemTest, ReadsATextFileAsItIs) {
    const nx::test::TemporaryDirectory directory;
    const std::string_view content{"id = \"x\"\r\n# a\xC3\xA7\xC3\xA3o\n"};
    directory.write("data.toml", content);

    const auto text = nx::platform::readTextFile(directory.path() / "data.toml");
    ASSERT_TRUE(text.has_value()) << nx::describe(text.error());
    EXPECT_EQ(*text, content) << "no line ending or encoding is converted";

    const auto missing = nx::platform::readTextFile(directory.path() / "missing.toml");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, FileError::NotFound);
}

TEST(FileSystemTest, TheExecutablePathIsThisTestProgram) {
    const auto path = nx::platform::executablePath();
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_TRUE(path->is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(*path));
    EXPECT_EQ(path->stem(), "nxPlatformTests");
}

TEST(FileSystemTest, ReadingSomethingOtherThanAFileFails) {
    const nx::test::TemporaryDirectory directory;
    directory.makeDirectory("maps");

    const auto missing = readFile(directory.path() / "missing.txt");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, FileError::NotFound);

    const auto inMissingDirectory = readFile(directory.path() / "missing" / "file.txt");
    ASSERT_FALSE(inMissingDirectory.has_value());
    EXPECT_EQ(inMissingDirectory.error().code, FileError::NotFound);

    const auto folder = readFile(directory.path() / "maps");
    ASSERT_FALSE(folder.has_value());
    EXPECT_EQ(folder.error().code, FileError::NotAFile);
}

} // namespace
