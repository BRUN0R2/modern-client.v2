// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The native file system: listing the files of a directory and reading a file whole. Engine code
/// reads content through the VFS of nx.assets (docs/06-assets-and-resources.md, section 2), which
/// is built on these.
export module nx.platform:fileSystem;

import std;
import nx.core;

export namespace nx::platform {

/// Why the native file system refused an operation. The detail has the path, and the message of
/// the operating system when it reported one.
enum class FileError : std::uint8_t {
    NotFound,      ///< Nothing exists at the path.
    AccessDenied,  ///< The operating system refused access to the path.
    NotADirectory, ///< listFiles was given something other than a directory.
    NotAFile,      ///< readFile was given something other than a regular file.
    ListFailed,    ///< The operating system failed while listing a directory.
    ReadFailed,    ///< The operating system failed while opening or reading a file.
};

/// `path` as UTF-8 text in the generic format, with `/` as the separator: for diagnostics, and
/// for the logical names the VFS gives native files.
[[nodiscard]] auto toUtf8(const std::filesystem::path& path) -> std::string;

/// The native path that the UTF-8 `text` names; `/` separates its components.
[[nodiscard]] auto fromUtf8(std::string_view text) -> std::filesystem::path;

/// Every regular file under `directory` and its subdirectories, as paths relative to it, sorted.
/// A symbolic link to a file is listed; a symbolic link to a directory is not followed (a link
/// can point back up, into a cycle), and a dangling link is not a file.
/// Errors: NotFound, AccessDenied (to the directory or to one of its subdirectories),
/// NotADirectory, ListFailed.
[[nodiscard]] auto listFiles(const std::filesystem::path& directory)
    -> Result<std::vector<std::filesystem::path>, FileError>;

/// The whole content of the regular file at `path`, as it was when the file was opened. Other
/// processes may write, rename or delete the file meanwhile, as POSIX allows everywhere.
/// Errors: NotFound, AccessDenied, NotAFile, ReadFailed (also when the file shrinks while it is
/// read).
[[nodiscard]] auto readFile(const std::filesystem::path& path) -> Result<std::vector<std::byte>, FileError>;

/// Same as readFile, for a text file (TOML, KeyValues, `.cfg`): its bytes as they are, without
/// any decoding.
[[nodiscard]] auto readTextFile(const std::filesystem::path& path) -> Result<std::string, FileError>;

/// The path of the running executable: the programs find their `data/` next to it.
/// Errors: ReadFailed (the operating system did not tell it).
[[nodiscard]] auto executablePath() -> Result<std::filesystem::path, FileError>;

} // namespace nx::platform
