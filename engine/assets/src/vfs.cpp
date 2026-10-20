// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.platform;

namespace nx::assets {

namespace {

/// The first byte after the ASCII control characters, and DEL, the one control byte after it.
constexpr unsigned char kFirstPrintableByte = 0x20;
constexpr unsigned char kDeleteByte = 0x7F;

[[nodiscard]] auto isControlByte(char character) noexcept -> bool {
    const auto byte = static_cast<unsigned char>(character);
    return byte < kFirstPrintableByte || byte == kDeleteByte;
}

/// The text after the last `.` of the last component of `path`; empty when there is none.
[[nodiscard]] auto extensionOf(std::string_view path) noexcept -> std::string_view {
    const std::string_view name = path.substr(path.rfind('/') + 1); // npos + 1 is 0: no `/`
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1);
}

/// The path rule (docs/06-assets-and-resources.md, section 2.2) that `text` breaks; nullopt when
/// `text` is a valid logical path.
[[nodiscard]] auto brokenRule(std::string_view text) noexcept -> std::optional<std::string_view> {
    if (text.empty()) {
        return "is empty";
    }
    if (text.size() > kMaxResourcePathBytes) {
        return "exceeds kMaxResourcePathBytes";
    }
    if (std::ranges::any_of(text, isControlByte)) {
        return "contains a control byte";
    }
    if (text.contains('\\')) {
        return "contains '\\' (the separator is '/')";
    }
    if (text.contains(':')) {
        return "contains ':' (a drive or a stream)";
    }
    if (text.front() == '/') {
        return "is absolute";
    }
    for (const auto component : std::views::split(text, '/')) {
        const std::string_view name{component};
        if (name.empty()) {
            return "has an empty component";
        }
        if (name == "." || name == "..") {
            return "has a '.' or '..' component";
        }
    }
    const std::string_view extension = extensionOf(text);
    if (extension.empty()) {
        return "has no extension";
    }
    if (std::ranges::none_of(kAllowedExtensions, [extension](std::string_view allowed) {
            return equalsIgnoringCase(extension, allowed);
        })) {
        return "has an extension the VFS does not serve";
    }
    return std::nullopt;
}

/// `text` with ASCII capitals in lowercase: the key lookups compare.
[[nodiscard]] auto lookupKey(std::string_view text) -> std::string {
    return text | std::views::transform(toLowerAscii) | std::ranges::to<std::string>();
}

} // namespace

auto VfsPath::parse(std::string_view text) -> Result<VfsPath, VfsError> {
    if (const auto rule = brokenRule(text)) {
        // Text over the ceiling may be hostile input of any size: its size is enough to report.
        const std::string subject = text.size() > kMaxResourcePathBytes
                                        ? std::format("a path of {} bytes", text.size())
                                        : std::format("{:?}", text);
        return fail(VfsError::InvalidPath, std::format("{} {}", subject, *rule));
    }
    return VfsPath{std::string{text}, lookupKey(text)};
}

auto Vfs::create(std::span<const std::filesystem::path> mounts) -> Result<Vfs, VfsError> {
    Vfs vfs;
    for (std::size_t mount = 0; mount < mounts.size(); ++mount) {
        const std::filesystem::path& root = mounts[mount];
        auto listed = platform::listFiles(root);
        if (!listed) {
            return fail(VfsError::MountFailed, platform::toUtf8(root), std::move(listed.error()));
        }
        for (const std::filesystem::path& relative : *listed) {
            const std::string text = platform::toUtf8(relative);
            if (brokenRule(text)) {
                continue; // not content
            }
            const auto [entry, isNew] =
                vfs.m_files.try_emplace(lookupKey(text), File{.native = root / relative, .mount = mount});
            // A higher mount's file shadows these; two of this mount's would make a path ambiguous.
            if (!isNew && entry->second.mount == mount) {
                return fail(VfsError::MountFailed, std::format("{} and {} differ only in case",
                                                               platform::toUtf8(entry->second.native),
                                                               platform::toUtf8(root / relative)));
            }
        }
    }
    return vfs;
}

auto Vfs::readAll(const VfsPath& path) const -> Result<std::vector<std::byte>, VfsError> {
    const auto found = m_files.find(path.m_key);
    if (found == m_files.end()) {
        return fail(VfsError::NotFound, std::format("{:?}", path.text()));
    }
    auto bytes = platform::readFile(found->second.native);
    if (!bytes) {
        return fail(VfsError::ReadFailed, std::format("{:?}", path.text()), std::move(bytes.error()));
    }
    return std::move(*bytes);
}

} // namespace nx::assets
