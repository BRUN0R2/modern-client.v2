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
using nx::platform::writeFile;

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

TEST(FileSystemTest, ReadsAPartOfAFile) {
    const nx::test::TemporaryDirectory directory;
    directory.write("tables.ttf", "0123456789");
    const std::filesystem::path path = directory.path() / "tables.ttf";

    const auto part = nx::platform::readFilePart(path, 3, 4);
    ASSERT_TRUE(part.has_value()) << nx::describe(part.error());
    EXPECT_EQ(*part, bytesOf("3456"));
    EXPECT_EQ(nx::platform::readFilePart(path, 0, 2).value_or(std::vector<std::byte>{}), bytesOf("01"));
    EXPECT_EQ(nx::platform::readFilePart(path, 8, 100).value_or(std::vector<std::byte>{}), bytesOf("89"))
        << "fewer where the file ends";
    const auto past = nx::platform::readFilePart(path, 20, 4);
    ASSERT_TRUE(past.has_value()) << nx::describe(past.error());
    EXPECT_TRUE(past->empty()) << "none past its end";

    const auto missing = nx::platform::readFilePart(directory.path() / "missing.ttf", 0, 4);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, FileError::NotFound);
}

TEST(FileSystemTest, WritesAWholeFileAndReplacesOne) {
    const nx::test::TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "screenshot.bmp";

    const auto created = writeFile(path, bytesOf("the first content"));
    ASSERT_TRUE(created.has_value()) << nx::describe(created.error());
    const auto replaced = writeFile(path, bytesOf("second"));
    ASSERT_TRUE(replaced.has_value()) << nx::describe(replaced.error());

    const auto bytes = readFile(path);
    ASSERT_TRUE(bytes.has_value()) << nx::describe(bytes.error());
    EXPECT_EQ(*bytes, bytesOf("second")) << "nothing of the longer content is left";
    EXPECT_EQ(listFiles(directory.path()).value_or(std::vector<std::filesystem::path>{}),
              (std::vector<std::filesystem::path>{"screenshot.bmp"}))
        << "nothing written beside it is left";
}

TEST(FileSystemTest, MakesADirectoryAndThoseAboveIt) {
    const nx::test::TemporaryDirectory directory;
    const std::filesystem::path nested = directory.path() / "a" / "b" / "c";

    const auto made = nx::platform::createDirectories(nested);
    ASSERT_TRUE(made.has_value()) << nx::describe(made.error());
    EXPECT_TRUE(std::filesystem::is_directory(nested));
    EXPECT_TRUE(nx::platform::createDirectories(nested).has_value()) << "one that exists already";

    directory.write("file.txt", "x");
    const auto blocked = nx::platform::createDirectories(directory.path() / "file.txt");
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, FileError::NotADirectory);
}

TEST(FileSystemTest, TheUserDirectoryIsTheGamesFolderOfTheUsersSettings) {
    const auto path = nx::platform::userDirectory();
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_TRUE(path->is_absolute());
#if defined(_WIN32)
    EXPECT_EQ(path->filename(), "Modern-Client.v2");
#else
    EXPECT_EQ(path->filename(), "modern-client.v2");
#endif
}

TEST(FileSystemTest, WritingWhereNoFileCanBeFails) {
    const nx::test::TemporaryDirectory directory;
    directory.makeDirectory("maps");

    const auto inMissingDirectory = writeFile(directory.path() / "missing" / "file.txt", bytesOf("x"));
    ASSERT_FALSE(inMissingDirectory.has_value());
    EXPECT_EQ(inMissingDirectory.error().code, FileError::NotFound);

    const auto folder = writeFile(directory.path() / "maps", bytesOf("x"));
    ASSERT_FALSE(folder.has_value());
    EXPECT_EQ(folder.error().code, FileError::NotAFile);
}

TEST(FileSystemTest, TheExecutablePathIsThisTestProgram) {
    const auto path = nx::platform::executablePath();
    ASSERT_TRUE(path.has_value()) << nx::describe(path.error());
    EXPECT_TRUE(path->is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(*path));
    EXPECT_EQ(path->stem(), "nxPlatformTests");
}

TEST(FileSystemTest, TheFontFoldersAreFoldersEachOnce) {
    const std::vector<std::filesystem::path> folders = nx::platform::fontFolders();
    for (const std::filesystem::path& folder : folders) {
        EXPECT_TRUE(std::filesystem::is_directory(folder)) << toUtf8(folder);
        EXPECT_EQ(std::ranges::count(folders, folder), 1) << toUtf8(folder);
    }
#if defined(_WIN32)
    EXPECT_FALSE(folders.empty()) << "Windows has its Fonts folder";
#endif
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
