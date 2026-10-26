// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.assets;

namespace cs {

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

/// The bytes of the file sequence group `group` of the model of `files` is in, `sequence` named in an
/// error: none for its own file's. Errors: ReadFailed.
[[nodiscard]] auto groupBytesOf(const Vfs& vfs, const ModelFiles& files, std::uint32_t group,
                                nx::assets::SequenceIndex sequence)
    -> nx::Result<std::vector<std::byte>, ModelFileError> {
    if (group == 0) {
        return std::vector<std::byte>{};
    }
    const MdlView& model = *files.view;
    return readFile(vfs, VfsPath::parseGoldsrc(model.sequenceGroups()[group].fileName),
                    std::format("the sequence group file of {:?}", model.sequences()[sequence.value].label));
}

/// The decoded frames of sequence `sequence` of the model of `files`, from `groupBytes`, its group's
/// file, or from the model's own when it is empty. Errors: Invalid.
[[nodiscard]] auto decodedIn(const ModelFiles& files, nx::assets::SequenceIndex sequence,
                             std::span<const std::byte> groupBytes)
    -> nx::Result<nx::assets::AnimationFrames, ModelFileError> {
    const MdlView& model = *files.view;
    auto frames = model.decodeAnimation(sequence, groupBytes.empty() ? std::span<const std::byte>{files.bytes}
                                                                     : groupBytes);
    if (!frames) {
        return nx::fail(
            ModelFileError::Invalid,
            std::format("the animation of {:?} in {:?}", model.sequences()[sequence.value].label, files.path),
            std::move(frames.error()));
    }
    return std::move(*frames);
}

} // namespace

auto ModelFiles::texturesView() const -> const MdlView& {
    return textures ? *textures : *view;
}

auto readModelFiles(const Vfs& vfs, std::string_view path, ModelTextures textures)
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
    if (textures == ModelTextures::Read && files->view->textures().empty()) {
        auto texturesBytes = readFile(
            vfs, at.and_then([](const VfsPath& found) { return nx::assets::texturesFilePath(found); }),
            std::format("the textures file of {:?}", path));
        if (!texturesBytes) {
            return std::unexpected{std::move(texturesBytes.error())};
        }
        files->texturesBytes = std::move(*texturesBytes);
        auto texturesView = MdlView::parse(files->texturesBytes);
        if (!texturesView) {
            return nx::fail(ModelFileError::Invalid, std::format("the textures file of {:?}", path),
                            std::move(texturesView.error()));
        }
        files->textures = std::move(*texturesView);
    }
    return files;
}

auto animationOf(const Vfs& vfs, const ModelFiles& files, nx::assets::SequenceIndex sequence)
    -> nx::Result<nx::assets::AnimationFrames, ModelFileError> {
    auto groupBytes = groupBytesOf(vfs, files, files.view->sequences()[sequence.value].group.value, sequence);
    if (!groupBytes) {
        return std::unexpected{std::move(groupBytes.error())};
    }
    return decodedIn(files, sequence, *groupBytes);
}

auto animationsOf(const Vfs& vfs, const ModelFiles& files)
    -> nx::Result<std::vector<nx::assets::AnimationFrames>, ModelFileError> {
    const std::span<const nx::assets::StudioSequence> sequences = files.view->sequences();
    std::map<std::uint32_t, std::vector<std::byte>> groups; // by group, each read once
    std::vector<nx::assets::AnimationFrames> animations;
    animations.reserve(sequences.size());
    for (std::uint32_t index = 0; index < sequences.size(); ++index) {
        const nx::assets::SequenceIndex sequence{index};
        const std::uint32_t group = sequences[index].group.value;
        auto found = groups.find(group);
        if (found == groups.end()) {
            auto read = groupBytesOf(vfs, files, group, sequence);
            if (!read) {
                return std::unexpected{std::move(read.error())};
            }
            found = groups.emplace(group, std::move(*read)).first;
        }
        auto frames = decodedIn(files, sequence, found->second);
        if (!frames) {
            return std::unexpected{std::move(frames.error())};
        }
        animations.push_back(std::move(*frames));
    }
    return animations;
}

auto readAnimatedModel(const nx::assets::Vfs& vfs, std::string_view path, ModelTextures textures)
    -> nx::Result<AnimatedModel, ModelFileError> {
    auto files = readModelFiles(vfs, path, textures);
    if (!files) {
        return std::unexpected{std::move(files.error())};
    }
    auto animations = animationsOf(vfs, **files);
    if (!animations) {
        return std::unexpected{std::move(animations.error())};
    }
    return AnimatedModel{.files = std::move(*files), .animations = std::move(*animations)};
}

} // namespace cs
