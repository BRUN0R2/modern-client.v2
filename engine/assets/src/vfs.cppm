// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The virtual file system: logical paths, the GoldSrc search path, resolved through the mounted
/// directories by priority (docs/06-assets-and-resources.md, section 2).
export module nx.assets:vfs;

import std;
import nx.core;

export namespace nx::assets {

/// The longest logical path, in bytes. Content has no path limit of its own (the original had 64
/// characters); this is a safety ceiling against hostile input (docs/06, section 3.4).
inline constexpr std::size_t kMaxResourcePathBytes = 1024;

/// The file extensions the VFS serves, in lowercase: the formats of docs/06, section 3. Anything
/// else (executables, libraries, scripts) is never content.
inline constexpr auto kAllowedExtensions = std::to_array<std::string_view>({
    // GoldSrc (docs/06, section 3.1)
    "bsp",
    "wad",
    "mdl",
    "spr",
    "wav",
    "txt",
    "res",
    "cfg",
    "gam",
    "bmp",
    "tga",
    "lmp",
    // Modern (docs/06, section 3.2)
    "gltf",
    "glb",
    "ktx2",
    "png",
    "nxspr",
    "ogg",
    "opus",
    "flac",
    "ttf",
    "otf",
    "toml",
});

/// What can go wrong in the VFS.
enum class VfsError : std::uint8_t {
    InvalidPath, ///< The text breaks a path rule of docs/06, section 2.2; the detail names it.
    NotFound,    ///< No mount has a file at the path.
    MountFailed, ///< A mounted directory could not be indexed (cause: the FileError), or two of
                 ///< its files have paths that differ only in case.
    ReadFailed,  ///< An indexed file could not be read (cause: the FileError).
};

/// A logical path, valid by construction: relative, `/` as the separator, no empty, `.` or `..`
/// component, no `\`, `:` or control byte, at most kMaxResourcePathBytes, and an extension of
/// kAllowedExtensions. Lookups ignore the case of ASCII letters; other bytes compare exactly.
class VfsPath {
public:
    /// Errors: InvalidPath.
    [[nodiscard]] static auto parse(std::string_view text) -> Result<VfsPath, VfsError>;

    /// A path as GoldSrc content writes it, where `\` is a separator too ("models\player01.mdl"
    /// in a model's sequence groups): parse with every `\` read as `/`. Errors: InvalidPath.
    [[nodiscard]] static auto parseGoldsrc(std::string_view text) -> Result<VfsPath, VfsError>;

    /// The path as written.
    [[nodiscard]] inline auto text() const noexcept -> std::string_view { return m_text; }

private:
    friend class Vfs;
    inline VfsPath(std::string text, std::string key) noexcept
        : m_text{std::move(text)}, m_key{std::move(key)} {}

    std::string m_text;
    std::string m_key; // m_text with ASCII capitals in lowercase: what lookups compare
};

/// Resolves logical paths to the files of mounted directories. Immutable once created, so any
/// thread may read through it at any time; another set of mounts (a content profile, an enabled
/// mod) is another Vfs.
class Vfs {
public:
    /// Indexes the files of each directory in `mounts`, highest priority first: a path resolves
    /// to the first mount that has it. Files whose relative paths are not valid logical paths
    /// (another extension, a `:` in a name) are not content and are left out.
    /// Errors: MountFailed.
    [[nodiscard]] static auto create(std::span<const std::filesystem::path> mounts) -> Result<Vfs, VfsError>;

    /// Whether a mount has a file at `path`.
    [[nodiscard]] auto contains(const VfsPath& path) const -> bool;

    /// The whole file at `path`. Errors: NotFound, ReadFailed.
    [[nodiscard]] auto readAll(const VfsPath& path) const -> Result<std::vector<std::byte>, VfsError>;

    /// The path of every file, as its mount spells it, in the order of their lookup keys.
    [[nodiscard]] auto paths() const -> std::vector<VfsPath>;

    /// The native file `path` resolves to, for diagnostics: content is read with readAll.
    /// Errors: NotFound.
    [[nodiscard]] auto nativePath(const VfsPath& path) const -> Result<std::filesystem::path, VfsError>;

private:
    struct File {
        std::string text; // its VfsPath text
        std::filesystem::path native;
        std::size_t mount = 0; // its mount's position in the priority order
    };

    Vfs() = default;

    std::unordered_map<std::string, File> m_files; // by VfsPath key
};

} // namespace nx::assets
