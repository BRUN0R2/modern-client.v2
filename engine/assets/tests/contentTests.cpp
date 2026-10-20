// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;

#include "temporaryDirectory.hpp"

namespace {

using nx::assets::ContentError;
using nx::assets::GoldsrcInstall;
using nx::assets::GoldsrcInstallDesc;
using nx::assets::readGoldsrcInstallDesc;

/// A description like the shipped one, smaller.
[[nodiscard]] auto smallDesc() -> GoldsrcInstallDesc {
    return GoldsrcInstallDesc{
        .id = "cstrikeOriginal",
        .redistributable = false,
        .steamAppId = 10,
        .steamInstallFolder = "Half-Life",
        .mounts = {"cstrike", "valve"},
        .essentialFiles = {"cstrike/liblist.gam", "valve/halflife.wad", "cstrike/maps/de_dust2.bsp"},
    };
}

/// An installation with an empty file for each essential file of `desc`: names only, no content.
void writeInstallation(const nx::test::TemporaryDirectory& folder, const GoldsrcInstallDesc& desc) {
    for (const std::string& file : desc.essentialFiles) {
        folder.write(nx::platform::fromUtf8(file), "");
    }
}

[[nodiscard]] auto openError(const GoldsrcInstallDesc& desc, const std::filesystem::path& folder)
    -> nx::Failure<ContentError> {
    auto install = GoldsrcInstall::open(desc, folder);
    EXPECT_FALSE(install.has_value());
    return install ? nx::Failure<ContentError>{} : std::move(install.error());
}

TEST(GoldsrcInstallTest, TheShippedDescriptionIsValid) {
    const auto text =
        nx::platform::readTextFile(nx::platform::fromUtf8(NX_DATA_DIRECTORY) / "content/goldsrcCstrike.toml");
    ASSERT_TRUE(text.has_value()) << nx::describe(text.error());
    const auto desc = readGoldsrcInstallDesc(*text, "goldsrcCstrike.toml");
    ASSERT_TRUE(desc.has_value()) << nx::describe(desc.error());
    EXPECT_FALSE(desc->redistributable) << "the user's installation never leaves the machine (ADR-0015)";
    EXPECT_EQ(desc->mounts, (std::vector<std::string>{"cstrike", "valve"}));

    const nx::test::TemporaryDirectory folder;
    writeInstallation(folder, *desc);
    const auto install = GoldsrcInstall::open(*desc, folder.path());
    ASSERT_TRUE(install.has_value()) << nx::describe(install.error());
}

TEST(GoldsrcInstallTest, OpensAnInstallationWithItsMountsInOrder) {
    const nx::test::TemporaryDirectory folder;
    writeInstallation(folder, smallDesc());

    const auto install = GoldsrcInstall::open(smallDesc(), folder.path());
    ASSERT_TRUE(install.has_value()) << nx::describe(install.error());
    EXPECT_EQ(install->folder(), folder.path());
    const std::vector<std::filesystem::path> expected{folder.path() / "cstrike", folder.path() / "valve"};
    EXPECT_TRUE(std::ranges::equal(install->mounts(), expected));
}

TEST(GoldsrcInstallTest, EssentialFilesResolveWhateverTheCase) {
    const nx::test::TemporaryDirectory folder;
    folder.write("cstrike/LibList.gam", "");
    folder.write("valve/HALFLIFE.WAD", "");
    folder.write("cstrike/Maps/de_dust2.bsp", "");

    const auto install = GoldsrcInstall::open(smallDesc(), folder.path());
    EXPECT_TRUE(install.has_value()) << nx::describe(install.error());
}

TEST(GoldsrcInstallTest, EveryMissingFileIsListed) {
    const nx::test::TemporaryDirectory folder;
    folder.write("cstrike/liblist.gam", "");
    folder.makeDirectory("valve");

    const auto failure = openError(smallDesc(), folder.path());
    EXPECT_EQ(failure.code, ContentError::MissingEssentialFiles);
    EXPECT_NE(failure.detail.find("valve/halflife.wad"), std::string::npos) << failure.detail;
    EXPECT_NE(failure.detail.find("cstrike/maps/de_dust2.bsp"), std::string::npos) << failure.detail;
    EXPECT_EQ(failure.detail.find("liblist.gam"), std::string::npos) << failure.detail;
}

TEST(GoldsrcInstallTest, AnEssentialFileMustBeInTheMountItNames) {
    const nx::test::TemporaryDirectory folder;
    writeInstallation(folder, smallDesc());
    std::error_code error;
    std::filesystem::rename(folder.path() / "valve/halflife.wad", folder.path() / "cstrike/halflife.wad",
                            error);
    ASSERT_FALSE(error) << error.message();

    const auto failure = openError(smallDesc(), folder.path());
    EXPECT_EQ(failure.code, ContentError::MissingEssentialFiles);
    EXPECT_NE(failure.detail.find("valve/halflife.wad"), std::string::npos) << failure.detail;
}

TEST(GoldsrcInstallTest, AMissingMountIsNotAnInstallation) {
    const nx::test::TemporaryDirectory folder;
    folder.write("cstrike/liblist.gam", "");

    const auto failure = openError(smallDesc(), folder.path());
    EXPECT_EQ(failure.code, ContentError::InstallNotFound);
    ASSERT_FALSE(failure.cause.empty());
    EXPECT_EQ(failure.cause.records()[0].code, "MountFailed");
}

TEST(GoldsrcInstallTest, ADescriptionThatBreaksItsRulesIsInvalid) {
    const nx::test::TemporaryDirectory folder;
    writeInstallation(folder, smallDesc());
    const auto withMounts = [](std::vector<std::string> mounts) {
        GoldsrcInstallDesc desc = smallDesc();
        desc.mounts = std::move(mounts);
        return desc;
    };
    const auto withEssentialFile = [](std::string file) {
        GoldsrcInstallDesc desc = smallDesc();
        desc.essentialFiles.push_back(std::move(file));
        return desc;
    };

    for (const GoldsrcInstallDesc& desc :
         {withMounts({"cstrike", "../valve"}), withMounts({"cstrike", "valve/sub"}),
          withMounts({"cstrike", ""}), withEssentialFile("liblist.gam"),
          withEssentialFile("czero/liblist.gam"), withEssentialFile("cstrike/dlls/mp.dll"),
          withEssentialFile("cstrike/../valve/halflife.wad")}) {
        const auto failure = openError(desc, folder.path());
        EXPECT_EQ(failure.code, ContentError::ConfigInvalid) << failure.detail;
    }
}

TEST(GoldsrcInstallTest, AnUnknownKeyIsInvalid) {
    const auto desc =
        readGoldsrcInstallDesc("id = \"x\"\nkind = \"goldsrcInstall\"\n", "goldsrcCstrike.toml");
    ASSERT_FALSE(desc.has_value());
    EXPECT_EQ(desc.error().code, ContentError::ConfigInvalid);
    ASSERT_FALSE(desc.error().cause.empty());
    EXPECT_EQ(desc.error().cause.records()[0].code, "UnknownKey");
}

} // namespace
