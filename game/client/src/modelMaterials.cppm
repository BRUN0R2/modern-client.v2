// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A studio model's materials for the enhanced look (docs/adr/0025-enhanced-rendering.md): the file
/// beside it, its path with `.material.toml` for `.mdl`, names its textures and what each is made of:
///
///     [[textures]]
///     texture = "gun_v01.bmp"     # a texture of the model, in any case
///     metallic = 1.0              # 0 a dielectric, 1 a metal
///     roughness = 0.35            # 0 a mirror, 1 rough
///     fullbright = false          # whether the original's light reaches it, in place of its flag
///
///     [[textures]]
///     texture = "gun_glow.bmp"
///     emissive = 3.0              # what it emits, its color times this: beyond 1 it blooms
///     emissiveMask = "models/gun_glow_mask.tga" # where it emits, by its texels; none: everywhere
///     blend = "additive"          # opaque, masked or additive, in place of its flags' way
///
///     [[textures]]
///     texture = "hand.bmp"
///     baseColorMap = "models/texture/hand_512.bmp" # its texels, TGA or BMP, of any size
///
/// A texture it does not name, or a model without the file, is shaded as the original shades it,
/// but for Counter-Strike Online's textures: one named `#WWWHHH...` has texture coordinates that
/// count in WWW x HHH texels, its pixels in the model only standing in for those of
/// `models/texture/<its name>`, read when there is one.
export module cs.client:modelMaterials;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.assets;
import nx.rhi;
import nx.render;

export namespace cs {

/// Why a model's materials could not be read.
enum class ModelMaterialError : std::uint8_t {
    FileInvalid,     ///< The material file breaks its schema, or names a texture the model lacks (cause).
    ImageUnreadable, ///< An image it names (a base color, an emissive mask) cannot be read (cause).
};

/// A texture's material in a material file.
struct TextureMaterialData {
    std::string texture{};
    std::optional<float> metallic{};
    std::optional<float> roughness{};
    std::optional<float> emissive{};
    std::optional<std::string> emissiveMask{};
    std::optional<bool> fullbright{};
    std::optional<std::string> baseColorMap{};
    std::optional<nx::render::StudioBlend> blend{};
};

/// A material file: data of TextureMaterialData.
struct ModelMaterialsData {
    std::vector<TextureMaterialData> textures{};
};

/// The material file of the model at `modelPath`: its path with `.material.toml` for its extension.
[[nodiscard]] auto materialFileOf(std::string_view modelPath) -> std::string;

/// The size a Counter-Strike Online texture's coordinates count in, from its name, "#WWWHHH...";
/// none for another name.
[[nodiscard]] auto namedSizeOf(std::string_view textureName) -> std::optional<nx::rhi::Extent2d>;

/// The materials of the textures of `textures` (the model at `modelPath`, or its `*T.mdl`), by their
/// index, in `vfs`: Counter-Strike Online's textures' sizes and images, then the model's material
/// file's; none for a texture neither gives. Errors: FileInvalid, ImageUnreadable.
[[nodiscard]] auto readModelMaterials(const nx::assets::Vfs& vfs, std::string_view modelPath,
                                      const nx::assets::MdlView& textures)
    -> nx::Result<std::vector<std::optional<nx::render::StudioMaterial>>, ModelMaterialError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::TextureMaterialData> {
    static constexpr std::string_view kName = "cs::TextureMaterialData";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::TextureMaterialData::texture>("texture"),
        nx::meta::field<&cs::TextureMaterialData::metallic>("metallic",
                                                            nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::TextureMaterialData::roughness>("roughness",
                                                             nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::TextureMaterialData::emissive>("emissive", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::TextureMaterialData::emissiveMask>("emissiveMask"),
        nx::meta::field<&cs::TextureMaterialData::fullbright>("fullbright"),
        nx::meta::field<&cs::TextureMaterialData::baseColorMap>("baseColorMap"),
        nx::meta::field<&cs::TextureMaterialData::blend>("blend"),
    };
};

template <>
struct nx::meta::Description<cs::ModelMaterialsData> {
    static constexpr std::string_view kName = "cs::ModelMaterialsData";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::ModelMaterialsData::textures>("textures"),
    };
};
