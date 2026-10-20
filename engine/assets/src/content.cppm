// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Content sources (ADR-0015, docs/06-assets-and-resources.md, section 2.4). Today: the user's
/// own Counter-Strike 1.6 installation, described by data/content/goldsrcCstrike.toml and checked
/// against it before the `classic` profile mounts it.
export module nx.assets:content;

import std;
import nx.core;
import nx.meta;
import :vfs;

export namespace nx::assets {

/// What can go wrong with a content source.
enum class ContentError : std::uint8_t {
    ConfigInvalid,         ///< A description breaks its schema or its rules (cause: the DataError or
                           ///< VfsError, when there is one); the detail names the file or entry.
    InstallNotFound,       ///< A directory of the installation is missing or unreadable (cause: the
                           ///< VfsError).
    MissingEssentialFiles, ///< The installation lacks essential files; the detail lists every one.
};

/// data/content/goldsrcCstrike.toml: the Counter-Strike 1.6 installation as a content source.
/// Only names: no Valve asset is part of the repository (ADR-0015).
struct GoldsrcInstallDesc {
    std::string id;                          ///< Names the source to servers and clients.
    bool redistributable = false;            ///< Whether servers may send its files to clients.
    std::uint32_t steamAppId = 0;            ///< The Steam application that installs it.
    std::string steamInstallFolder;          ///< Its folder under steamapps/common.
    std::vector<std::string> mounts;         ///< Directories of the installation, highest priority first.
    std::vector<std::string> essentialFiles; ///< `<mount>/<logical path>` of each file it must have.
};

/// Reads `text`, the description file `sourceName`. Errors: ConfigInvalid (cause: the DataError).
[[nodiscard]] auto readGoldsrcInstallDesc(std::string_view text, std::string_view sourceName)
    -> Result<GoldsrcInstallDesc, ContentError>;

/// A Counter-Strike 1.6 installation that has every essential file of its description.
class GoldsrcInstall {
public:
    /// Checks the installation in `folder` against `desc`: each mount is one directory name of
    /// the installation, and each essential file names one of the mounts and resolves in it,
    /// whatever the case of its letters (docs/06, section 2.2).
    /// Errors: ConfigInvalid, InstallNotFound, MissingEssentialFiles.
    [[nodiscard]] static auto open(const GoldsrcInstallDesc& desc, std::filesystem::path folder)
        -> Result<GoldsrcInstall, ContentError>;

    [[nodiscard]] inline auto folder() const noexcept -> const std::filesystem::path& { return m_folder; }

    /// The directories to mount, highest priority first.
    [[nodiscard]] inline auto mounts() const noexcept -> std::span<const std::filesystem::path> {
        return m_mounts;
    }

private:
    inline GoldsrcInstall(std::filesystem::path folder, std::vector<std::filesystem::path> mounts) noexcept
        : m_folder{std::move(folder)}, m_mounts{std::move(mounts)} {}

    std::filesystem::path m_folder;
    std::vector<std::filesystem::path> m_mounts;
};

} // namespace nx::assets

template <>
struct nx::meta::Description<nx::assets::GoldsrcInstallDesc> {
    static constexpr std::string_view kName = "nx::assets::GoldsrcInstallDesc";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::assets::GoldsrcInstallDesc::id>("id"),
        nx::meta::field<&nx::assets::GoldsrcInstallDesc::redistributable>("redistributable"),
        nx::meta::field<&nx::assets::GoldsrcInstallDesc::steamAppId>("steamAppId"),
        nx::meta::field<&nx::assets::GoldsrcInstallDesc::steamInstallFolder>("steamInstallFolder"),
        nx::meta::field<&nx::assets::GoldsrcInstallDesc::mounts>("mounts"),
        nx::meta::field<&nx::assets::GoldsrcInstallDesc::essentialFiles>("essentialFiles"),
    };
};
