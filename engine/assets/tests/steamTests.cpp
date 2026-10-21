// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;

#include "logRecorder.hpp"
#include "temporaryDirectory.hpp"

namespace {

using nx::assets::ContentError;
using nx::assets::detectGoldsrcInstall;
using nx::assets::GoldsrcInstallDesc;
using nx::assets::readSteamLibraries;
using nx::platform::fromUtf8;
using nx::platform::toUtf8;

[[nodiscard]] auto desc() -> GoldsrcInstallDesc {
    return GoldsrcInstallDesc{
        .id = "cstrikeOriginal",
        .isRedistributable = false,
        .steamAppId = 10,
        .steamInstallFolder = "Half-Life",
        .mounts = {"cstrike", "valve"},
        .essentialFiles = {"cstrike/liblist.gam", "valve/halflife.wad"},
    };
}

/// The libraries `text` lists, or none after a failure the test reports.
[[nodiscard]] auto librariesIn(std::string_view text) -> std::vector<std::filesystem::path> {
    auto libraries = readSteamLibraries(text, "libraryfolders.vdf");
    EXPECT_TRUE(libraries.has_value()) << nx::describe(libraries.error());
    return libraries ? std::move(*libraries) : std::vector<std::filesystem::path>{};
}

/// A Steam library in `folder`: with `hasGame`, the manifest of app 10 and an installation, whose
/// essential files exist when `isComplete`.
struct LibraryContent {
    bool hasGame = false;
    bool isComplete = false;
};

void writeLibrary(const nx::test::TemporaryDirectory& folder, LibraryContent content) {
    folder.makeDirectory("steamapps");
    if (!content.hasGame) {
        return;
    }
    folder.write("steamapps/appmanifest_10.acf", "\"AppState\" { \"appid\" \"10\" }");
    folder.write("steamapps/common/Half-Life/cstrike/liblist.gam", "");
    folder.makeDirectory("steamapps/common/Half-Life/valve");
    if (content.isComplete) {
        folder.write("steamapps/common/Half-Life/valve/halflife.wad", "");
    }
}

/// libraryfolders.vdf, current layout, listing `libraries`.
void writeLibraryList(const nx::test::TemporaryDirectory& steam,
                      std::span<const std::filesystem::path> libraries) {
    std::string text = "\"libraryfolders\"\n{\n";
    for (std::size_t index = 0; index < libraries.size(); ++index) {
        text +=
            std::format("\t\"{}\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n", index, toUtf8(libraries[index]));
    }
    text += "}\n";
    steam.write("steamapps/libraryfolders.vdf", text);
}

TEST(SteamLibrariesTest, ReadsTheCurrentLayout) {
    const auto libraries = librariesIn(R"("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"contentid"		"4521305939541262836"
		"apps"
		{
			"10"		"542519808"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
	}
}
)");
    EXPECT_EQ(libraries, (std::vector<std::filesystem::path>{fromUtf8("C:\\Program Files (x86)\\Steam"),
                                                             fromUtf8("D:\\SteamLibrary")}));
}

TEST(SteamLibrariesTest, ReadsTheOldLayout) {
    const auto libraries = librariesIn(R"("LibraryFolders"
{
	"TimeNextStatsReport"		"1600000000"
	"ContentStatsID"		"-1"
	"1"		"D:\\SteamLibrary"
	"2"		"E:\\Games\\Steam"
}
)");
    EXPECT_EQ(libraries, (std::vector<std::filesystem::path>{fromUtf8("D:\\SteamLibrary"),
                                                             fromUtf8("E:\\Games\\Steam")}));
}

TEST(SteamLibrariesTest, ReadsCommentsBareWordsAndEscapes) {
    const auto libraries = librariesIn(R"(// written by hand
libraryfolders { // the list
	0 { path "/home/player/Steam \"main\"" }
	1 { path /mnt/games }
})");
    EXPECT_EQ(libraries, (std::vector<std::filesystem::path>{fromUtf8("/home/player/Steam \"main\""),
                                                             fromUtf8("/mnt/games")}));
}

TEST(SteamLibrariesTest, AMalformedFileNamesTheLine) {
    for (const std::string_view text : std::initializer_list<std::string_view>{
             "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n",             // not closed
             "\"libraryfolders\"\n{\n}\n}\n",                     // closes no block
             "\"libraryfolders\"\n{\n\t{\n\t}\n}\n",              // a block without a key
             "\"libraryfolders\"\n{\n\t\"0\"\n}\n",               // a key without a value
             "\"libraryfolders\"\n{\n\t\"0\" \"D:\\\\Steam\n}\n", // a quoted word not closed
             "\"other\" { }",                                     // no libraryfolders block
             "\"libraryfolders\" { \"0\" { \"label\" \"\" } }",   // a library without a path
         }) {
        const auto libraries = readSteamLibraries(text, "libraryfolders.vdf");
        ASSERT_FALSE(libraries.has_value()) << text;
        EXPECT_EQ(libraries.error().code, ContentError::ConfigInvalid);
        EXPECT_TRUE(libraries.error().detail.starts_with("libraryfolders.vdf")) << libraries.error().detail;
    }

    const auto unclosed = readSteamLibraries("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n", "libraryfolders.vdf");
    ASSERT_FALSE(unclosed.has_value());
    EXPECT_EQ(unclosed.error().detail, "libraryfolders.vdf:5: a block is not closed with '}'");
}

TEST(SteamLibrariesTest, BlocksNestUpToTheBound) {
    std::string deepest = "libraryfolders { "; // then kMaxKeyValuesDepth - 1 blocks inside
    for (std::size_t depth = 1; depth < nx::assets::kMaxKeyValuesDepth; ++depth) {
        deepest += "k { ";
    }
    const std::string closing(nx::assets::kMaxKeyValuesDepth, '}');
    EXPECT_TRUE(readSteamLibraries(deepest + closing, "libraryfolders.vdf").has_value());

    const auto tooDeep = readSteamLibraries(deepest + "k { }" + closing, "libraryfolders.vdf");
    ASSERT_FALSE(tooDeep.has_value());
    EXPECT_EQ(tooDeep.error().detail, "libraryfolders.vdf:1: blocks nest deeper than kMaxKeyValuesDepth");
}

TEST(GoldsrcDetectionTest, FindsTheGameInAnotherLibrary) {
    const nx::test::TemporaryDirectory steam;
    const nx::test::TemporaryDirectory library;
    writeLibrary(steam, {});
    writeLibrary(library, {.hasGame = true, .isComplete = true});
    writeLibraryList(steam, std::vector<std::filesystem::path>{steam.path(), library.path()});

    const std::vector<std::filesystem::path> steamFolders{steam.path()};
    const auto install = detectGoldsrcInstall(desc(), steamFolders);
    ASSERT_TRUE(install.has_value()) << nx::describe(install.error());
    EXPECT_EQ(install->folder(), library.path() / "steamapps" / "common" / "Half-Life");
}

TEST(GoldsrcDetectionTest, AnIncompleteInstallationGivesWayToACompleteOneAndIsReported) {
    const nx::test::TemporaryDirectory steam;
    const nx::test::TemporaryDirectory complete;
    writeLibrary(steam, {.hasGame = true, .isComplete = false});
    writeLibrary(complete, {.hasGame = true, .isComplete = true});
    writeLibraryList(steam, std::vector<std::filesystem::path>{steam.path(), complete.path()});
    const nx::test::LogRecorder log;

    const std::vector<std::filesystem::path> steamFolders{steam.path()};
    const auto install = detectGoldsrcInstall(desc(), steamFolders);
    ASSERT_TRUE(install.has_value()) << nx::describe(install.error());
    EXPECT_EQ(install->folder(), complete.path() / "steamapps" / "common" / "Half-Life");
    const std::vector<std::string> warnings = log.messagesAt(nx::log::Level::Warning);
    ASSERT_EQ(warnings.size(), 1U) << "never skipped in silence";
    EXPECT_NE(warnings.front().find("valve/halflife.wad"), std::string::npos) << warnings.front();
}

TEST(GoldsrcDetectionTest, AnIncompleteInstallationIsReported) {
    const nx::test::TemporaryDirectory steam;
    writeLibrary(steam, {.hasGame = true, .isComplete = false});

    const std::vector<std::filesystem::path> steamFolders{steam.path()};
    const auto install = detectGoldsrcInstall(desc(), steamFolders);
    ASSERT_FALSE(install.has_value());
    EXPECT_EQ(install.error().code, ContentError::MissingEssentialFiles);
    EXPECT_NE(install.error().detail.find("valve/halflife.wad"), std::string::npos) << install.error().detail;
}

TEST(GoldsrcDetectionTest, NoLibraryWithTheGameIsInstallNotFound) {
    const nx::test::TemporaryDirectory steam;
    writeLibrary(steam, {});
    // An installation folder without the manifest: Steam does not have the game there.
    steam.write("steamapps/common/Half-Life/cstrike/liblist.gam", "");
    steam.write("steamapps/common/Half-Life/valve/halflife.wad", "");

    const std::vector<std::filesystem::path> steamFolders{steam.path()};
    const auto install = detectGoldsrcInstall(desc(), steamFolders);
    ASSERT_FALSE(install.has_value());
    EXPECT_EQ(install.error().code, ContentError::InstallNotFound);

    const auto noSteam = detectGoldsrcInstall(desc(), {});
    ASSERT_FALSE(noSteam.has_value());
    EXPECT_EQ(noSteam.error().code, ContentError::InstallNotFound);
}

TEST(GoldsrcDetectionTest, AnUnreadableLibraryListIsInvalid) {
    const nx::test::TemporaryDirectory steam;
    writeLibrary(steam, {.hasGame = true, .isComplete = true});
    steam.write("steamapps/libraryfolders.vdf", "\"libraryfolders\" {");

    const std::vector<std::filesystem::path> steamFolders{steam.path()};
    const auto install = detectGoldsrcInstall(desc(), steamFolders);
    ASSERT_FALSE(install.has_value());
    EXPECT_EQ(install.error().code, ContentError::ConfigInvalid);
}

TEST(SteamFoldersTest, AreFoldersThatExist) {
    for (const std::filesystem::path& folder : nx::platform::steamFolders()) {
        std::error_code error;
        EXPECT_TRUE(std::filesystem::is_directory(folder, error)) << toUtf8(folder);
    }
}

} // namespace
