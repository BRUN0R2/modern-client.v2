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
/// Where Counter-Strike Online keeps the textures its models' #WWWHHH names stand in for.
constexpr std::string_view kTextureFolder = "models/texture/";
/// "#" and the six digits of a size: three of the width, three of the height.
constexpr std::size_t kNamedSizeLength = 7;
constexpr std::size_t kSideDigits = 3;

/// The bytes of the file at `path` in `vfs`. Errors: those of the VFS.
[[nodiscard]] auto readBytes(const nx::assets::Vfs& vfs, std::string_view path)
    -> nx::Result<std::vector<std::byte>, nx::assets::VfsError> {
    return nx::assets::VfsPath::parse(path).and_then(
        [&vfs](const nx::assets::VfsPath& at) { return vfs.readAll(at); });
}

/// The image of the TGA or BMP file at `path` in `vfs`. Errors: ImageUnreadable.
[[nodiscard]] auto readImage(const nx::assets::Vfs& vfs, std::string_view path)
    -> nx::Result<nx::assets::RgbaImage, ModelMaterialError> {
    auto bytes = readBytes(vfs, path);
    if (!bytes) {
        return nx::fail(ModelMaterialError::ImageUnreadable, std::string{path}, std::move(bytes.error()));
    }
    const std::string_view extension = path.substr(std::min(path.rfind('.'), path.size()));
    if (nx::equalsIgnoringCase(extension, ".bmp")) {
        auto image = nx::assets::readBmp(*bytes);
        if (!image) {
            return nx::fail(ModelMaterialError::ImageUnreadable, std::string{path}, std::move(image.error()));
        }
        return std::move(*image);
    }
    auto image = nx::assets::readTga(*bytes);
    if (!image) {
        return nx::fail(ModelMaterialError::ImageUnreadable, std::string{path}, std::move(image.error()));
    }
    return std::move(*image);
}

} // namespace

auto namedSizeOf(std::string_view textureName) -> std::optional<nx::rhi::Extent2d> {
    if (textureName.size() < kNamedSizeLength || textureName.front() != '#') {
        return std::nullopt;
    }
    const auto side = [&](std::size_t first) -> std::optional<std::uint32_t> {
        std::uint32_t value = 0;
        const char* const begin = textureName.data() + first;
        const auto [end, error] = std::from_chars(begin, begin + kSideDigits, value);
        return error == std::errc{} && end == begin + kSideDigits && value > 0 ? std::optional{value}
                                                                               : std::nullopt;
    };
    const std::optional<std::uint32_t> width = side(1);
    const std::optional<std::uint32_t> height = side(1 + kSideDigits);
    if (!width || !height) {
        return std::nullopt;
    }
    return nx::rhi::Extent2d{.width = *width, .height = *height};
}

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
    for (std::size_t index = 0; index < modelTextures.size(); ++index) {
        const std::string& name = modelTextures[index].name;
        if (const std::optional<nx::rhi::Extent2d> size = namedSizeOf(name)) {
            nx::render::StudioMaterial& material = materials[index].emplace();
            material.coordinateSize = size;
            if (auto image = readImage(vfs, std::format("{}{}", kTextureFolder, name)); image) {
                material.baseColor = std::move(*image);
            }
        }
    }
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
        std::optional<nx::render::StudioMaterial>& slot =
            materials[nx::narrow<std::size_t>(found - modelTextures.begin())];
        nx::render::StudioMaterial& material = slot ? *slot : slot.emplace();
        material.metallic = named.metallic.value_or(material.metallic);
        material.roughness = named.roughness.value_or(material.roughness);
        material.emissive = named.emissive.value_or(material.emissive);
        if (named.fullbright) {
            material.isFullbright = named.fullbright;
        }
        if (named.blend) {
            material.blend = named.blend;
        }
        for (const auto& [path, image] : {std::pair{&named.emissiveMask, &material.emissiveMask},
                                          std::pair{&named.baseColorMap, &material.baseColor}}) {
            if (*path) {
                auto read = readImage(vfs, **path);
                if (!read) {
                    return std::unexpected{std::move(read.error())};
                }
                *image = std::move(*read);
            }
        }
    }
    return materials;
}

} // namespace cs
