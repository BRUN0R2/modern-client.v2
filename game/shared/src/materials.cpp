// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;

namespace cs {

namespace {

/// The letters materials.txt tags textures with, in the order of TextureMaterial.
constexpr nx::EnumArray<TextureMaterial, char> kMaterialLetters{nx::EnumArray<TextureMaterial, char>::Storage{
    'C', 'M', 'D', 'V', 'G', 'T', 'S', 'W', 'P', 'X', 'Y', 'F', 'N'}};

/// The prefixes of a tiled or toggled texture's name: the sign, then the digit.
constexpr std::size_t kTogglePrefixLength = 2;

[[nodiscard]] constexpr auto isSpace(char letter) noexcept -> bool {
    return letter == ' ' || (letter >= '\t' && letter <= '\r');
}

[[nodiscard]] constexpr auto isLetter(char letter) noexcept -> bool {
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z');
}

[[nodiscard]] constexpr auto lowered(char letter) noexcept -> char {
    constexpr char kCaseOffset = 'a' - 'A';
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + kCaseOffset) : letter;
}

[[nodiscard]] constexpr auto raised(char letter) noexcept -> char {
    constexpr char kCaseOffset = 'a' - 'A';
    return letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - kCaseOffset) : letter;
}

/// How `left` compares with `right` without case, as Q_stricmp compares them: by the first letter
/// that differs, a name before the longer names it starts.
[[nodiscard]] auto compareIgnoringCase(std::string_view left, std::string_view right) noexcept
    -> std::strong_ordering {
    const std::size_t common = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto leftLetter = static_cast<unsigned char>(lowered(left[index]));
        const auto rightLetter = static_cast<unsigned char>(lowered(right[index]));
        if (leftLetter != rightLetter) {
            return leftLetter <=> rightLetter;
        }
    }
    return left.size() <=> right.size();
}

/// What a texture tagged with `letter` is made of: concrete for a letter of no material.
[[nodiscard]] auto materialOfLetter(char letter) noexcept -> TextureMaterial {
    for (const TextureMaterial material : nx::EnumArray<TextureMaterial, char>::keys()) {
        if (kMaterialLetters[material] == letter) {
            return material;
        }
    }
    return TextureMaterial::Concrete;
}

} // namespace

auto TextureMaterials::read(std::string_view text) -> TextureMaterials {
    TextureMaterials materials;
    std::size_t start = 0;
    while (start < text.size() && materials.m_tagged.size() < kMostTaggedTextures) {
        // A line as the original's memfgets reads it: with its end of line, when it has one.
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
        const std::string_view line = text.substr(start, end - start);
        start = end;
        std::size_t at = 0;
        while (at < line.size() && isSpace(line[at])) {
            ++at;
        }
        if (at == line.size() || line[at] == '/' || !isLetter(line[at])) {
            continue; // blank, or a comment
        }
        const TextureMaterial material = materialOfLetter(raised(line[at++]));
        while (at < line.size() && isSpace(line[at])) {
            ++at;
        }
        std::size_t nameEnd = at;
        while (nameEnd < line.size() && !isSpace(line[nameEnd])) {
            ++nameEnd;
        }
        if (at == line.size() || nameEnd == line.size()) {
            continue; // no name, or one the line's end of line does not follow
        }
        materials.m_tagged.push_back(
            Tagged{.name = std::string{line.substr(at, std::min(nameEnd - at, kTaggedNameLength))},
                   .material = material});
    }
    // PM_SortTextures' own sort: every later name below an earlier one swaps with it.
    std::vector<Tagged>& tagged = materials.m_tagged;
    for (std::size_t first = 0; first < tagged.size(); ++first) {
        for (std::size_t second = first + 1; second < tagged.size(); ++second) {
            if (compareIgnoringCase(tagged[first].name, tagged[second].name) > 0) {
                std::swap(tagged[first], tagged[second]);
            }
        }
    }
    return materials;
}

auto TextureMaterials::materialOf(std::string_view name) const noexcept -> TextureMaterial {
    const std::string_view key = name.substr(0, std::min(name.size(), kTaggedNameLength));
    // PM_FindTextureType's search.
    std::ptrdiff_t left = 0;
    std::ptrdiff_t right = std::ssize(m_tagged) - 1;
    while (left <= right) {
        const std::ptrdiff_t pivot = (left + right) / 2;
        const std::strong_ordering order =
            compareIgnoringCase(key, m_tagged[static_cast<std::size_t>(pivot)].name);
        if (order == 0) {
            return m_tagged[static_cast<std::size_t>(pivot)].material;
        }
        if (order > 0) {
            left = pivot + 1;
        } else {
            right = pivot - 1;
        }
    }
    return TextureMaterial::Concrete;
}

auto lookedUpTextureName(std::string_view name) noexcept -> std::string_view {
    if (name.starts_with('-') || name.starts_with('+')) {
        name.remove_prefix(std::min(name.size(), kTogglePrefixLength));
    }
    if (!name.empty() &&
        (name.front() == '{' || name.front() == '!' || name.front() == '~' || name.front() == ' ')) {
        name.remove_prefix(1);
    }
    return name.substr(0, std::min(name.size(), kTaggedNameLength));
}

} // namespace cs
