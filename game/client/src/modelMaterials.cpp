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

/// The renderer's way of drawing a texture's meshes that a material file's `blend` names.
[[nodiscard]] auto studioBlendOf(MaterialBlend blend) noexcept -> nx::render::StudioBlend {
    switch (blend) {
    case MaterialBlend::Masked:
        return nx::render::StudioBlend::Masked;
    case MaterialBlend::Additive:
        return nx::render::StudioBlend::Additive;
    case MaterialBlend::Opaque:
    case MaterialBlend::Count:
        break;
    }
    return nx::render::StudioBlend::Opaque;
}

[[nodiscard]] auto metallicRoughnessOf(const MetallicRoughnessData& data)
    -> nx::render::StudioMetallicRoughness {
    nx::render::StudioMetallicRoughness surface;
    surface.metallic = data.metallic.value_or(surface.metallic);
    surface.roughness = data.roughness.value_or(surface.roughness);
    return surface;
}

/// The painted surface `data` gives, its images read from `vfs`. Errors: ImageUnreadable.
[[nodiscard]] auto paintedOf(const nx::assets::Vfs& vfs, const PaintedData& data)
    -> nx::Result<nx::render::StudioPainted, ModelMaterialError> {
    nx::render::StudioPainted surface;
    surface.roughness = data.roughness.value_or(surface.roughness);
    if (data.specularColor) {
        const auto& [red, green, blue] = *data.specularColor;
        surface.specularColor = {.x = red, .y = green, .z = blue};
    }
    surface.specularWeight = data.specularWeight.value_or(surface.specularWeight);
    if (data.specularMap) {
        auto map = readImage(vfs, *data.specularMap);
        if (!map) {
            return std::unexpected{std::move(map.error())};
        }
        surface.specularMap = std::move(*map);
    }
    if (data.light) {
        auto map = readImage(vfs, data.light->map);
        if (!map) {
            return std::unexpected{std::move(map.error())};
        }
        nx::render::StudioPaintedLight& light =
            surface.light.emplace(nx::render::StudioPaintedLight{.map = std::move(*map)});
        light.gain = data.light->gain.value_or(light.gain);
    }
    return surface;
}

/// The surface `named` gives its texture, its images read from `vfs`; none when it gives none.
/// `where` names it in failures. Errors: SurfaceAmbiguous, ImageUnreadable.
[[nodiscard]] auto surfaceOf(const nx::assets::Vfs& vfs, const TextureMaterialData& named,
                             std::string_view where)
    -> nx::Result<std::optional<nx::render::StudioSurface>, ModelMaterialError> {
    if (named.metallicRoughness && named.painted) {
        return nx::fail(ModelMaterialError::SurfaceAmbiguous,
                        std::format("{}: {:?} has both metallicRoughness and painted", where, named.texture));
    }
    if (named.metallicRoughness) {
        return std::optional<nx::render::StudioSurface>{metallicRoughnessOf(*named.metallicRoughness)};
    }
    if (named.painted) {
        return paintedOf(vfs, *named.painted).transform([](nx::render::StudioPainted&& painted) {
            return std::optional<nx::render::StudioSurface>{std::move(painted)};
        });
    }
    return std::optional<nx::render::StudioSurface>{};
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
    std::unordered_set<std::size_t> namedTextures;
    for (std::size_t entry = 0; entry < data->textures.size(); ++entry) {
        const TextureMaterialData& named = data->textures[entry];
        const std::string where = std::format("{}: textures[{}]", file, entry);
        const auto found =
            std::ranges::find_if(modelTextures, [&named](const nx::assets::StudioTexture& texture) {
                return nx::equalsIgnoringCase(texture.name, named.texture);
            });
        if (found == modelTextures.end()) {
            return nx::fail(ModelMaterialError::TextureUnknown,
                            std::format("{}: {:?} is no texture of the model", where, named.texture));
        }
        const auto index = nx::narrow<std::size_t>(found - modelTextures.begin());
        if (!namedTextures.insert(index).second) {
            return nx::fail(ModelMaterialError::TextureRepeated,
                            std::format("{}: {:?} is named before", where, named.texture));
        }
        auto surface = surfaceOf(vfs, named, where);
        if (!surface) {
            return std::unexpected{std::move(surface.error())};
        }
        nx::render::StudioMaterial& material =
            materials[index] ? *materials[index] : materials[index].emplace();
        if (*surface) {
            material.surface = std::move(*surface);
        }
        material.emissive = named.emissive.value_or(material.emissive);
        if (named.fullbright) {
            material.isFullbright = named.fullbright;
        }
        if (named.blend) {
            material.blend = studioBlendOf(*named.blend);
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
