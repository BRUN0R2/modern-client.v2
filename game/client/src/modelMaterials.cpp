// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.data;
import nx.assets;
import nx.render;

namespace cs {

namespace {

constexpr std::string_view kMaterialExtension = ".material.toml";

/// The bytes of the file at `path` in `vfs`. Errors: those of the VFS.
[[nodiscard]] auto readBytes(const nx::assets::Vfs& vfs, std::string_view path)
    -> nx::Result<std::vector<std::byte>, nx::assets::VfsError> {
    return nx::assets::VfsPath::parse(path).and_then(
        [&vfs](const nx::assets::VfsPath& at) { return vfs.readAll(at); });
}

} // namespace

auto materialFileOf(std::string_view modelPath) -> std::string {
    const std::size_t dot = modelPath.rfind('.');
    const std::size_t slash = modelPath.rfind('/');
    const bool hasExtension =
        dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
    return std::format("{}{}", hasExtension ? modelPath.substr(0, dot) : modelPath, kMaterialExtension);
}

auto readModelMaterials(const nx::assets::Vfs& vfs, std::string_view modelPath,
                        const nx::assets::MdlView& textures)
    -> nx::Result<std::vector<std::optional<nx::render::StudioMaterial>>, ModelMaterialError> {
    const std::span<const nx::assets::StudioTexture> modelTextures = textures.textures();
    std::vector<std::optional<nx::render::StudioMaterial>> materials(modelTextures.size());
    const std::string file = materialFileOf(modelPath);
    auto bytes = readBytes(vfs, file);
    if (!bytes) {
        return materials; // no file: every texture as the original shades it
    }
    const std::string text = *bytes |
                             std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
                             std::ranges::to<std::string>();
    auto data = nx::data::parseToml<ModelMaterialsData>(text, file);
    if (!data) {
        return nx::fail(ModelMaterialError::FileInvalid, std::move(data.error()));
    }
    for (const TextureMaterialData& named : data->textures) {
        const auto found =
            std::ranges::find_if(modelTextures, [&named](const nx::assets::StudioTexture& texture) {
                return nx::equalsIgnoringCase(texture.name, named.texture);
            });
        if (found == modelTextures.end()) {
            return nx::fail(ModelMaterialError::FileInvalid,
                            std::format("{}: {:?} is no texture of the model", file, named.texture));
        }
        nx::render::StudioMaterial material{.isFullbright = named.fullbright};
        material.metallic = named.metallic.value_or(material.metallic);
        material.roughness = named.roughness.value_or(material.roughness);
        material.emissive = named.emissive.value_or(material.emissive);
        if (named.emissiveMask) {
            auto maskBytes = readBytes(vfs, *named.emissiveMask);
            if (!maskBytes) {
                return nx::fail(ModelMaterialError::MaskUnreadable, *named.emissiveMask,
                                std::move(maskBytes.error()));
            }
            auto mask = nx::assets::readTga(*maskBytes);
            if (!mask) {
                return nx::fail(ModelMaterialError::MaskUnreadable, *named.emissiveMask,
                                std::move(mask.error()));
            }
            material.emissiveMask = std::move(*mask);
        }
        materials[nx::narrow<std::size_t>(found - modelTextures.begin())] = std::move(material);
    }
    return materials;
}

} // namespace cs
