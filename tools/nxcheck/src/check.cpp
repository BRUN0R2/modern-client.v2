// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module tool.nxcheck;

import std;
import nx.core;
import nx.jobs;
import nx.assets;
import nx.collision;

namespace nxcheck {

namespace {

using nx::fail;
using nx::Result;
using nx::assets::BspView;
using nx::assets::MdlView;
using nx::assets::SpriteView;
using nx::assets::Vfs;
using nx::assets::VfsPath;
using nx::assets::WadLumpType;
using nx::assets::WadView;

/// A sequence group file starts with "IDSQ"; a model, or its `*T.mdl`, with "IDST".

/// Reads a file that `model` joins (`role` says how), mapping every failure to DependencyFailed.
[[nodiscard]] auto readJoined(const Vfs& vfs, const nx::Result<VfsPath, nx::assets::VfsError>& path,
                              std::string_view role) -> Result<std::vector<std::byte>, CheckError> {
    if (!path) {
        return fail(CheckError::DependencyFailed, std::string{role}, path.error());
    }
    auto bytes = vfs.readAll(*path);
    if (!bytes) {
        return fail(CheckError::DependencyFailed, std::format("{} {:?}", role, path->text()),
                    std::move(bytes.error()));
    }
    return std::move(*bytes);
}

[[nodiscard]] auto checkBsp(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    auto map = BspView::parse(bytes);
    if (!map) {
        return fail(CheckError::Invalid, std::move(map.error()));
    }
    if (auto hulls = nx::collision::BrushHulls::create(*map); !hulls) {
        return fail(CheckError::Invalid, "the collision hulls", std::move(hulls.error()));
    }
    return {};
}

[[nodiscard]] auto checkWad(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    auto wad = WadView::parse(bytes);
    if (!wad) {
        return fail(CheckError::Invalid, std::move(wad.error()));
    }
    for (const nx::assets::WadEntry& entry : wad->entries()) {
        if (entry.type != WadLumpType::MipTexture) {
            continue; // pictures and fonts have no reader yet
        }
        if (auto texture = nx::assets::readTexture(entry); !texture) {
            return fail(CheckError::Invalid, std::format("texture {:?}", entry.name),
                        std::move(texture.error()));
        }
    }
    return {};
}

/// Checks the model at `path`: its textures, from its `*T.mdl` when it has none, and the animation
/// of every sequence, from the sequence group file it is in.
[[nodiscard]] auto checkMdl(const Vfs& vfs, const VfsPath& path, std::span<const std::byte> bytes)
    -> Result<void, CheckError> {
    auto model = MdlView::parse(bytes);
    if (!model) {
        return fail(CheckError::Invalid, std::move(model.error()));
    }

    if (model->textures().empty()) {
        auto texturesBytes = readJoined(vfs, nx::assets::texturesFilePath(path), "the textures file");
        if (!texturesBytes) {
            return std::unexpected{std::move(texturesBytes.error())};
        }
        auto texturesFile = MdlView::parse(*texturesBytes);
        if (!texturesFile) {
            return fail(CheckError::DependencyFailed, "the textures file", std::move(texturesFile.error()));
        }
        if (auto joined = model->checkTextures(*texturesFile); !joined) {
            return fail(CheckError::DependencyFailed, "the textures file", std::move(joined.error()));
        }
    }

    // Each sequence group file is read once, when its first sequence needs it.
    std::vector<std::optional<std::vector<std::byte>>> groupFiles(model->sequenceGroups().size());
    for (std::uint32_t index = 0; index < model->sequences().size(); ++index) {
        const nx::assets::StudioSequence& sequence = model->sequences()[index];
        std::span<const std::byte> groupFile = bytes;
        if (const std::uint32_t group = sequence.group.value; group != 0) {
            std::optional<std::vector<std::byte>>& file = groupFiles[group];
            if (!file) {
                const std::string_view fileName = model->sequenceGroups()[group].fileName;
                auto read = readJoined(vfs, VfsPath::parseGoldsrc(fileName), "the sequence group file");
                if (!read) {
                    return std::unexpected{std::move(read.error())};
                }
                file = std::move(*read);
            }
            groupFile = *file;
        }
        if (auto frames = model->decodeAnimation(nx::assets::SequenceIndex{index}, groupFile); !frames) {
            return fail(CheckError::Invalid, std::format("the animation of sequence {:?}", sequence.label),
                        std::move(frames.error()));
        }
    }
    return {};
}

[[nodiscard]] auto checkSpr(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    if (auto sprite = SpriteView::parse(bytes); !sprite) {
        return fail(CheckError::Invalid, std::move(sprite.error()));
    }
    return {};
}

[[nodiscard]] auto checkTga(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    if (auto image = nx::assets::readTga(bytes); !image) {
        return fail(CheckError::Invalid, std::move(image.error()));
    }
    return {};
}

[[nodiscard]] auto checkWav(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    if (auto sound = nx::assets::parseWav(bytes); !sound) {
        return fail(CheckError::Invalid, std::move(sound.error()));
    }
    return {};
}

/// The reader of the files with one extension.
struct Reader {
    std::string_view extension;
    Format format{};
    auto (*check)(const Vfs& vfs, const VfsPath& path, std::span<const std::byte> bytes)
        -> Result<void, CheckError>;
};

constexpr auto kReaders = std::to_array<Reader>({
    {.extension = "bsp",
     .format = Format::Bsp,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkBsp(bytes); }},
    {.extension = "wad",
     .format = Format::Wad,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkWad(bytes); }},
    {.extension = "mdl", .format = Format::Mdl, .check = checkMdl},
    {.extension = "spr",
     .format = Format::Spr,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkSpr(bytes); }},
    {.extension = "tga",
     .format = Format::Tga,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkTga(bytes); }},
    {.extension = "wav",
     .format = Format::Wav,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkWav(bytes); }},
});

/// The reader of the file at `path`, by its extension; null when no reader reads it yet.
[[nodiscard]] auto readerOf(const VfsPath& path) -> const Reader* {
    const std::string_view text = path.text();
    const std::string_view extension = text.substr(text.rfind('.') + 1); // every VfsPath has one
    const auto found = std::ranges::find_if(kReaders, [extension](const Reader& reader) {
        return nx::equalsIgnoringCase(extension, reader.extension);
    });
    return found == kReaders.end() ? nullptr : &*found;
}

/// Reads the file at `path` and checks it with `reader`. A sequence group file has the extension
/// of a model: its content tells them apart.
[[nodiscard]] auto checkFile(const Vfs& vfs, const VfsPath& path, const Reader& reader, FileResult& result)
    -> Result<void, CheckError> {
    result.format = reader.format;
    auto bytes = vfs.readAll(path);
    if (!bytes) {
        return fail(CheckError::ReadFailed, std::move(bytes.error()));
    }
    result.bytes = bytes->size();
    if (reader.format == Format::Mdl && std::ranges::starts_with(*bytes, nx::assets::kSequenceGroupMagic)) {
        result.format = Format::SequenceGroup;
        return {}; // read by the models whose sequences are in it
    }
    return reader.check(vfs, path, *bytes);
}

} // namespace

auto checkContent(const Vfs& vfs, nx::jobs::JobSystem& jobs) -> std::vector<FileResult> {
    const std::vector<VfsPath> paths = vfs.paths();
    std::vector<FileResult> results(paths.size());
    const std::span<const VfsPath> pathView = paths;
    const std::span<FileResult> resultView = results;
    const nx::jobs::JobHandle done =
        jobs.parallelFor({.label = "nxcheck.file"}, std::views::iota(std::size_t{0}, paths.size()),
                         nx::jobs::Grain::automatic(), [&vfs, pathView, resultView](std::size_t index) {
                             const VfsPath& path = pathView[index];
                             FileResult& result = resultView[index];
                             result.path = std::string{path.text()};
                             const Reader* reader = readerOf(path);
                             if (reader == nullptr) {
                                 return;
                             }
                             if (auto checked = checkFile(vfs, path, *reader, result); !checked) {
                                 result.failure = std::move(checked.error());
                             }
                         });
    jobs.wait(done);
    return results;
}

} // namespace nxcheck
