// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.jobs;
import nx.platform;
import nx.assets;
import tool.nxcheck;

#include "temporaryDirectory.hpp"

namespace {

using nxcheck::CheckError;
using nxcheck::FileResult;
using nxcheck::Format;

/// The seeds of the fuzz targets: one small valid file of every format, and a few broken ones.
const std::filesystem::path kCorpus{NX_FUZZ_CORPUS_DIRECTORY};

/// Where the header of an MDL keeps its texture count.
constexpr std::size_t kTextureCountOffset = 180;

[[nodiscard]] auto textOf(std::span<const std::byte> bytes) -> std::string {
    return bytes | std::views::transform([](std::byte byte) { return static_cast<char>(byte); }) |
           std::ranges::to<std::string>();
}

[[nodiscard]] auto readSeed(std::string_view seed) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(kCorpus / seed);
    EXPECT_TRUE(bytes.has_value()) << nx::describe(bytes.error());
    return bytes.value_or(std::vector<std::byte>{});
}

/// Writes the fuzz seed `seed` ("mdl/triangle.mdl") to `relative` in `directory`.
void writeSeed(const nx::test::TemporaryDirectory& directory, std::string_view seed,
               const std::filesystem::path& relative) {
    directory.write(relative, textOf(readSeed(seed)));
}

/// Checks the files of `directory`, its only mount, on two workers.
[[nodiscard]] auto checkDirectory(const nx::test::TemporaryDirectory& directory) -> std::vector<FileResult> {
    const std::array mounts{directory.path()};
    auto vfs = nx::assets::Vfs::create(mounts);
    auto jobs = nx::jobs::JobSystem::create({.workerCount = 2});
    if (!vfs || !jobs) {
        ADD_FAILURE() << "cannot mount the directory or start the workers";
        return {};
    }
    return nxcheck::checkContent(*vfs, **jobs);
}

[[nodiscard]] auto resultOf(std::span<const FileResult> results, std::string_view path) -> const FileResult* {
    const auto found = std::ranges::find(results, path, &FileResult::path);
    return found == results.end() ? nullptr : &*found;
}

/// The domain and code of the first cause of `result`'s failure: "BspError::TooSmall".
[[nodiscard]] auto firstCauseOf(const FileResult& result) -> std::string {
    if (!result.failure || result.failure->cause.empty()) {
        return {};
    }
    const nx::CauseRecord& cause = result.failure->cause.records().front();
    return std::format("{}::{}", cause.domain, cause.code);
}

TEST(CheckContentTest, ReadsEveryFormatInPathOrder) {
    const nx::test::TemporaryDirectory directory;
    writeSeed(directory, "bsp/boxEmbedded.bsp", "maps/boxEmbedded.bsp");
    writeSeed(directory, "bsp/boxInWad.bsp", "maps/boxInWad.bsp");
    writeSeed(directory, "wad/textures.wad", "textures.wad");
    writeSeed(directory, "mdl/triangle.mdl", "models/triangle.mdl");
    writeSeed(directory, "spr/additiveSingle.spr", "sprites/additiveSingle.spr");
    writeSeed(directory, "wav/stereo16Cue.wav", "sound/stereo16Cue.wav");
    directory.write("readme.txt", "no reader reads text yet");

    const std::vector<FileResult> results = checkDirectory(directory);

    const auto paths = results | std::views::transform(&FileResult::path) | std::ranges::to<std::vector>();
    EXPECT_EQ(paths, (std::vector<std::string>{"maps/boxEmbedded.bsp", "maps/boxInWad.bsp",
                                               "models/triangle.mdl", "readme.txt", "sound/stereo16Cue.wav",
                                               "sprites/additiveSingle.spr", "textures.wad"}));
    for (const FileResult& result : results) {
        EXPECT_FALSE(result.failure.has_value()) << result.path << ": " << nx::describe(*result.failure);
    }
    const auto formats =
        results | std::views::transform(&FileResult::format) | std::ranges::to<std::vector>();
    EXPECT_EQ(formats,
              (std::vector<std::optional<Format>>{Format::Bsp, Format::Bsp, Format::Mdl, std::nullopt,
                                                  Format::Wav, Format::Spr, Format::Wad}));
    ASSERT_EQ(results.size(), 7U);
    EXPECT_EQ(results[0].bytes, readSeed("bsp/boxEmbedded.bsp").size());
    EXPECT_EQ(results[3].bytes, 0U); // read by no reader
}

TEST(CheckContentTest, ARejectedFileCarriesItsReadersFailure) {
    const nx::test::TemporaryDirectory directory;
    directory.write("maps/broken.bsp", "not a map");
    writeSeed(directory, "wad/truncatedTexture.wad", "truncated.wad");

    const std::vector<FileResult> results = checkDirectory(directory);

    const FileResult* map = resultOf(results, "maps/broken.bsp");
    ASSERT_NE(map, nullptr);
    ASSERT_TRUE(map->failure.has_value());
    EXPECT_EQ(map->failure->code, CheckError::Invalid);
    EXPECT_EQ(firstCauseOf(*map), "BspError::TooSmall");

    const FileResult* wad = resultOf(results, "truncated.wad");
    ASSERT_NE(wad, nullptr);
    ASSERT_TRUE(wad->failure.has_value());
    EXPECT_EQ(wad->failure->code, CheckError::Invalid);
    EXPECT_TRUE(firstCauseOf(*wad).starts_with("WadError::")) << nx::describe(*wad->failure);
}

TEST(CheckContentTest, AModelWithoutTexturesNeedsItsTexturesFile) {
    std::vector<std::byte> model = readSeed("mdl/triangle.mdl");
    ASSERT_GE(model.size(), kTextureCountOffset + sizeof(std::int32_t));
    std::ranges::fill(std::span{model}.subspan(kTextureCountOffset, sizeof(std::int32_t)), std::byte{0});
    const nx::test::TemporaryDirectory directory;
    directory.write("models/triangle.mdl", textOf(model));

    const std::vector<FileResult> alone = checkDirectory(directory);
    ASSERT_EQ(alone.size(), 1U);
    ASSERT_TRUE(alone[0].failure.has_value());
    EXPECT_EQ(alone[0].failure->code, CheckError::DependencyFailed);
    EXPECT_EQ(firstCauseOf(alone[0]), "VfsError::NotFound");

    writeSeed(directory, "mdl/textures.mdl", "models/triangleT.mdl");
    const std::vector<FileResult> joined = checkDirectory(directory);
    ASSERT_EQ(joined.size(), 2U);
    for (const FileResult& result : joined) {
        EXPECT_FALSE(result.failure.has_value()) << result.path << ": " << nx::describe(*result.failure);
        EXPECT_EQ(result.format, Format::Mdl);
    }
}

TEST(CheckContentTest, TellsSequenceGroupFilesFromModels) {
    const nx::test::TemporaryDirectory directory;
    directory.write("models/hostage01.mdl", std::string_view{"IDSQ\x0a\0\0\0", 8});

    const std::vector<FileResult> results = checkDirectory(directory);

    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].format, Format::SequenceGroup);
    EXPECT_EQ(results[0].bytes, 8U);
    EXPECT_FALSE(results[0].failure.has_value());
}

} // namespace
