// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.data;
import nx.platform;

namespace nx::assets {

namespace {

/// One essential file: its entry in the description, its mount's position there, and its logical
/// path in that mount.
struct EssentialFile {
    std::string_view entry;
    std::size_t mount = 0;
    VfsPath path;
};

/// The directories `desc` mounts, in `folder`. Errors: ConfigInvalid.
[[nodiscard]] auto mountsOf(const GoldsrcInstallDesc& desc, const std::filesystem::path& folder)
    -> Result<std::vector<std::filesystem::path>, ContentError> {
    std::vector<std::filesystem::path> mounts;
    for (const std::string& mount : desc.mounts) {
        if (!isDirectoryName(mount)) {
            return fail(ContentError::ConfigInvalid,
                        std::format("mount {:?} is not one directory name", mount));
        }
        mounts.push_back(folder / platform::fromUtf8(mount));
    }
    return mounts;
}

/// The essential files of `desc`, each split into its mount and its logical path. Errors:
/// ConfigInvalid.
[[nodiscard]] auto essentialFilesOf(const GoldsrcInstallDesc& desc)
    -> Result<std::vector<EssentialFile>, ContentError> {
    std::vector<EssentialFile> files;
    for (const std::string_view entry : desc.essentialFiles) {
        const std::size_t separator = entry.find('/');
        const auto mount = std::ranges::find(desc.mounts, entry.substr(0, separator));
        if (separator == std::string_view::npos || mount == desc.mounts.end()) {
            return fail(ContentError::ConfigInvalid,
                        std::format("essential file {:?} does not start with one of the mounts {}", entry,
                                    desc.mounts));
        }
        auto path = VfsPath::parse(entry.substr(separator + 1));
        if (!path) {
            return fail(ContentError::ConfigInvalid, std::format("essential file {:?}", entry),
                        std::move(path.error()));
        }
        files.push_back(
            EssentialFile{.entry = entry,
                          .mount = narrow<std::size_t>(std::ranges::distance(desc.mounts.begin(), mount)),
                          .path = std::move(*path)});
    }
    return files;
}

} // namespace

auto isDirectoryName(std::string_view name) noexcept -> bool {
    return !name.empty() && name != "." && name != ".." && std::ranges::none_of(name, [](char character) {
        return character == '/' || character == '\\' || character == ':' || isAsciiControl(character);
    });
}

auto readGoldsrcInstallDesc(std::string_view text, std::string_view sourceName)
    -> Result<GoldsrcInstallDesc, ContentError> {
    auto desc = data::parseToml<GoldsrcInstallDesc>(text, sourceName);
    if (!desc) {
        return fail(ContentError::ConfigInvalid, std::string{sourceName}, std::move(desc.error()));
    }
    return std::move(*desc);
}

auto GoldsrcInstall::open(const GoldsrcInstallDesc& desc, std::filesystem::path folder)
    -> Result<GoldsrcInstall, ContentError> {
    auto mounts = mountsOf(desc, folder);
    if (!mounts) {
        return std::unexpected(std::move(mounts.error()));
    }
    auto essentialFiles = essentialFilesOf(desc);
    if (!essentialFiles) {
        return std::unexpected(std::move(essentialFiles.error()));
    }

    // Each mount on its own, so that an essential file must be in the mount it names.
    std::vector<Vfs> indexes;
    for (const std::filesystem::path& mount : *mounts) {
        auto index = Vfs::create(std::span{&mount, 1});
        if (!index) {
            return fail(ContentError::InstallNotFound, platform::toUtf8(folder), std::move(index.error()));
        }
        indexes.push_back(std::move(*index));
    }
    std::vector<std::string_view> missing;
    for (const EssentialFile& file : *essentialFiles) {
        if (!indexes[file.mount].contains(file.path)) {
            missing.push_back(file.entry);
        }
    }
    if (!missing.empty()) {
        return fail(ContentError::MissingEssentialFiles,
                    std::format("{} lacks {}", platform::toUtf8(folder), missing));
    }
    return GoldsrcInstall{std::move(folder), std::move(*mounts)};
}

} // namespace nx::assets
