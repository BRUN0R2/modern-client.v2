// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module tool.nxview;

import std;
import nx.core;
import nx.assets;

namespace nxview {

namespace {

using nx::assets::MdlView;
using nx::assets::Vfs;
using nx::assets::VfsPath;

/// The bytes of the file at `path`, which `what` names. Errors: ReadFailed.
[[nodiscard]] auto readFile(const Vfs& vfs, const nx::Result<VfsPath, nx::assets::VfsError>& path,
                            std::string_view what) -> nx::Result<std::vector<std::byte>, ModelFileError> {
    auto bytes = path.and_then([&vfs](const VfsPath& found) { return vfs.readAll(found); });
    if (!bytes) {
        return nx::fail(ModelFileError::ReadFailed, std::string{what}, std::move(bytes.error()));
    }
    return std::move(*bytes);
}

} // namespace

auto ModelFiles::texturesView() const -> const MdlView& {
    return textures ? *textures : *view;
}

auto readModelFiles(const Vfs& vfs, std::string_view path)
    -> nx::Result<std::unique_ptr<ModelFiles>, ModelFileError> {
    // The engine's file system reads a leading slash as the game's folder.
    const std::string_view relative = path.substr(std::min(path.find_first_not_of("/\\"), path.size()));
    const auto at = VfsPath::parseGoldsrc(relative);
    auto files = std::make_unique<ModelFiles>(ModelFiles{.path = std::string{path}});
    auto bytes = readFile(vfs, at, path);
    if (!bytes) {
        return std::unexpected{std::move(bytes.error())};
    }
    files->bytes = std::move(*bytes);
    auto view = MdlView::parse(files->bytes);
    if (!view) {
        return nx::fail(ModelFileError::Invalid, std::string{path}, std::move(view.error()));
    }
    files->view = std::move(*view);
    if (files->view->textures().empty()) {
        auto texturesBytes = readFile(
            vfs, at.and_then([](const VfsPath& found) { return nx::assets::texturesFilePath(found); }),
            std::format("the textures file of {:?}", path));
        if (!texturesBytes) {
            return std::unexpected{std::move(texturesBytes.error())};
        }
        files->texturesBytes = std::move(*texturesBytes);
        auto textures = MdlView::parse(files->texturesBytes);
        if (!textures) {
            return nx::fail(ModelFileError::Invalid, std::format("the textures file of {:?}", path),
                            std::move(textures.error()));
        }
        files->textures = std::move(*textures);
    }
    return files;
}

auto animationOf(const Vfs& vfs, const ModelFiles& files, nx::assets::SequenceIndex sequence)
    -> nx::Result<nx::assets::AnimationFrames, ModelFileError> {
    const MdlView& model = *files.view;
    std::vector<std::byte> groupBytes;
    if (const std::uint32_t group = model.sequences()[sequence.value].group.value; group != 0) {
        auto read =
            readFile(vfs, VfsPath::parseGoldsrc(model.sequenceGroups()[group].fileName),
                     std::format("the sequence group file of {:?}", model.sequences()[sequence.value].label));
        if (!read) {
            return std::unexpected{std::move(read.error())};
        }
        groupBytes = std::move(*read);
    }
    auto frames =
        model.decodeAnimation(sequence, groupBytes.empty() ? std::span<const std::byte>{files.bytes}
                                                           : std::span<const std::byte>{groupBytes});
    if (!frames) {
        return nx::fail(
            ModelFileError::Invalid,
            std::format("the animation of {:?} in {:?}", model.sequences()[sequence.value].label, files.path),
            std::move(frames.error()));
    }
    return std::move(*frames);
}

} // namespace nxview
