// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.render;
import cs.client;

#include "temporaryDirectory.hpp"
#include "triangleModel.hpp"

namespace {

using cs::ModelMaterialError;
using nx::render::StudioBlend;
using nx::render::StudioMaterial;
using nx::render::StudioMetallicRoughness;
using nx::render::StudioPainted;

using Materials = std::vector<std::optional<StudioMaterial>>;

constexpr std::string_view kModelPath = "models/triangle.mdl";
constexpr std::string_view kMaterialPath = "models/triangle.material.toml";
/// A texel of four different channels: a conversion of its colors, or a lost alpha, shows.
constexpr std::array<std::uint8_t, 4> kDataTexel{32, 96, 224, 64};
constexpr nx::Vec3 kDielectricReflectance{.x = 0.04F, .y = 0.04F, .z = 0.04F};

class ModelMaterialsTest : public testing::Test {
protected:
    nx::test::TemporaryDirectory files;
    nx::test::TriangleModel model;

    [[nodiscard]] auto read() const -> nx::Result<Materials, ModelMaterialError> {
        auto vfs = nx::assets::Vfs::create(std::array{files.path()});
        EXPECT_TRUE(vfs.has_value()) << (vfs ? "" : nx::describe(vfs.error()));
        const auto view = nx::assets::MdlView::parse(model.bytes);
        EXPECT_TRUE(view.has_value()) << (view ? "" : nx::describe(view.error()));
        if (!vfs || !view) {
            return Materials{};
        }
        return cs::readModelMaterials(*vfs, kModelPath, *view);
    }

    [[nodiscard]] auto read(std::string_view text) const -> nx::Result<Materials, ModelMaterialError> {
        files.write(kMaterialPath, text);
        return read();
    }

    /// A TGA file of one texel, `rgba`.
    void writeImage(std::string_view path, std::array<std::uint8_t, 4> rgba) const {
        constexpr std::size_t kHeaderBytes = 18;
        constexpr std::size_t kImageTypeOffset = 2;
        constexpr std::size_t kWidthOffset = 12;
        constexpr std::size_t kHeightOffset = 14;
        constexpr std::size_t kDepthOffset = 16;
        constexpr std::size_t kDescriptorOffset = 17;
        constexpr char kTrueColor = 2;
        constexpr char kBitsPerTexel = 32;
        constexpr char kTopLeftWithAlpha = 0x28;
        std::string tga(kHeaderBytes, '\0');
        tga[kImageTypeOffset] = kTrueColor;
        tga[kWidthOffset] = 1;
        tga[kHeightOffset] = 1;
        tga[kDepthOffset] = kBitsPerTexel;
        tga[kDescriptorOffset] = kTopLeftWithAlpha;
        for (const std::uint8_t value : {rgba[2], rgba[1], rgba[0], rgba[3]}) {
            tga.push_back(static_cast<char>(value));
        }
        files.write(path, tga);
    }

    /// The failure of reading `text`, which must fail.
    [[nodiscard]] auto failureOf(std::string_view text) const -> nx::Failure<ModelMaterialError> {
        auto materials = read(text);
        EXPECT_FALSE(materials.has_value());
        return materials ? nx::Failure<ModelMaterialError>{} : std::move(materials.error());
    }
};

[[nodiscard]] auto pixelsOf(std::array<std::uint8_t, 4> rgba) -> std::vector<std::byte> {
    return {std::byte{rgba[0]}, std::byte{rgba[1]}, std::byte{rgba[2]}, std::byte{rgba[3]}};
}

/// The painted surface of the only texture's material, which it must have.
[[nodiscard]] auto paintedOf(const Materials& materials) -> const StudioPainted& {
    static const StudioPainted kNone{};
    if (materials.empty() || !materials.front() || !materials.front()->surface) {
        ADD_FAILURE() << "no surface";
        return kNone;
    }
    const auto* const painted = std::get_if<StudioPainted>(&*materials.front()->surface);
    EXPECT_NE(painted, nullptr) << "another surface";
    return painted != nullptr ? *painted : kNone;
}

TEST_F(ModelMaterialsTest, AModelWithoutAMaterialFileHasNoMaterials) {
    const auto materials = read();
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    ASSERT_EQ(materials->size(), 1U);
    EXPECT_FALSE(materials->front().has_value());
}

TEST_F(ModelMaterialsTest, AMetallicRoughnessSurfaceIsReadWithHowItsTextureIsDrawn) {
    const auto materials = read(R"(
[[textures]]
texture = "skin.bmp"
metallicRoughness = { metallic = 0.9, roughness = 0.32 }
emissive = 2.5
fullbright = false
blend = "masked"
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioMaterial& material = materials->front().value();
    ASSERT_TRUE(material.surface.has_value());
    const auto* const surface = std::get_if<StudioMetallicRoughness>(&*material.surface);
    ASSERT_NE(surface, nullptr);
    EXPECT_FLOAT_EQ(surface->metallic, 0.9F);
    EXPECT_FLOAT_EQ(surface->roughness, 0.32F);
    EXPECT_FLOAT_EQ(material.emissive, 2.5F);
    EXPECT_EQ(material.isFullbright, false);
    EXPECT_EQ(material.blend, StudioBlend::Masked);
}

TEST_F(ModelMaterialsTest, ATextureWithoutASurfaceHasNone) {
    const auto materials = read(R"(
[[textures]]
texture = "skin.bmp"
emissive = 1.0
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioMaterial& material = materials->front().value();
    EXPECT_FALSE(material.surface.has_value()) << "shaded as the original shades it, what it emits added";
    EXPECT_FLOAT_EQ(material.emissive, 1.0F);
}

TEST_F(ModelMaterialsTest, APaintedSurfaceIsReadApartFromWhatItEmitsAndHowItIsDrawn) {
    const auto materials = read(R"(
[[textures]]
texture = "SKIN.BMP"
fullbright = true
emissive = 3.0
blend = "additive"

[textures.painted]
roughness = 0.4
specularColor = [1.0, 0.5, 0.0]
specularWeight = 0.25
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioPainted& painted = paintedOf(*materials);
    EXPECT_FLOAT_EQ(painted.roughness, 0.4F);
    EXPECT_EQ(painted.specularColor, (nx::Vec3{.x = 1.0F, .y = 0.5F, .z = 0.0F}));
    EXPECT_FLOAT_EQ(painted.specularWeight, 0.25F);
    const StudioMaterial& material = materials->front().value();
    EXPECT_EQ(material.isFullbright, true);
    EXPECT_FLOAT_EQ(material.emissive, 3.0F);
    EXPECT_EQ(material.blend, StudioBlend::Additive);
}

TEST_F(ModelMaterialsTest, APaintedSurfaceWithNothingSetTakesItsDefaults) {
    const auto materials = read(R"(
[[textures]]
texture = "skin.bmp"
painted = {}
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioPainted& painted = paintedOf(*materials);
    EXPECT_FLOAT_EQ(painted.roughness, StudioPainted{}.roughness);
    EXPECT_EQ(painted.specularColor, kDielectricReflectance);
    EXPECT_FLOAT_EQ(painted.specularWeight, 1.0F);
    EXPECT_FALSE(painted.specularMap.has_value());
    EXPECT_FALSE(painted.light.has_value());
    const StudioMaterial& material = materials->front().value();
    EXPECT_FLOAT_EQ(material.emissive, 0.0F);
    EXPECT_FALSE(material.isFullbright.has_value());
    EXPECT_FALSE(material.blend.has_value());
}

TEST_F(ModelMaterialsTest, ASpecularMapIsReadAsDataAsItIs) {
    writeImage("models/specular.tga", kDataTexel);
    const auto materials = read(R"(
[[textures]]
texture = "skin.bmp"
painted = { specularColor = [0.9, 0.4, 0.1], specularWeight = 0.5, specularMap = "models/specular.tga" }
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioPainted& painted = paintedOf(*materials);
    ASSERT_TRUE(painted.specularMap.has_value());
    EXPECT_EQ(painted.specularMap->width, 1U);
    EXPECT_EQ(painted.specularMap->height, 1U);
    EXPECT_EQ(painted.specularMap->pixels, pixelsOf(kDataTexel));
    EXPECT_EQ(painted.specularColor, (nx::Vec3{.x = 0.9F, .y = 0.4F, .z = 0.1F}));
    EXPECT_FLOAT_EQ(painted.specularWeight, 0.5F);
    EXPECT_FALSE(materials->front()->baseColor.has_value());
    EXPECT_FALSE(materials->front()->emissiveMask.has_value());
}

TEST_F(ModelMaterialsTest, EachImageGoesToItsOwnPlace) {
    constexpr std::array<std::uint8_t, 4> kBase{200, 100, 50, 255};
    constexpr std::array<std::uint8_t, 4> kEmission{255, 240, 128, 16};
    constexpr std::array<std::uint8_t, 4> kLight{64, 128, 192, 255};
    writeImage("models/specular.tga", kDataTexel);
    writeImage("models/base.tga", kBase);
    writeImage("models/emission.tga", kEmission);
    writeImage("models/light.tga", kLight);
    const auto materials = read(R"(
[[textures]]
texture = "skin.bmp"
baseColorMap = "models/base.tga"
emissiveMask = "models/emission.tga"
emissive = 2.0
painted = { specularMap = "models/specular.tga", light = { map = "models/light.tga" } }
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioMaterial& material = materials->front().value();
    const StudioPainted& painted = paintedOf(*materials);
    ASSERT_TRUE(material.baseColor.has_value());
    ASSERT_TRUE(material.emissiveMask.has_value());
    ASSERT_TRUE(painted.specularMap.has_value());
    ASSERT_TRUE(painted.light.has_value());
    EXPECT_EQ(material.baseColor->pixels, pixelsOf(kBase));
    EXPECT_EQ(material.emissiveMask->pixels, pixelsOf(kEmission));
    EXPECT_EQ(painted.specularMap->pixels, pixelsOf(kDataTexel));
    EXPECT_EQ(painted.light->map.pixels, pixelsOf(kLight));
    EXPECT_FLOAT_EQ(material.emissive, 2.0F);
}

TEST_F(ModelMaterialsTest, APaintedLightIsReadWithItsGain) {
    constexpr std::array<std::uint8_t, 4> kLight{64, 128, 192, 255};
    writeImage("models/light.tga", kLight);
    const auto materials = read(R"(
[[textures]]
texture = "skin.bmp"
[textures.painted]
specularWeight = 0.0
light = { map = "models/light.tga", gain = 0.75 }
)");
    ASSERT_TRUE(materials.has_value()) << nx::describe(materials.error());
    const StudioPainted& painted = paintedOf(*materials);
    ASSERT_TRUE(painted.light.has_value());
    EXPECT_EQ(painted.light->map.pixels, pixelsOf(kLight));
    EXPECT_FLOAT_EQ(painted.light->gain, 0.75F);
    EXPECT_FLOAT_EQ(painted.specularWeight, 0.0F);
    EXPECT_FALSE(painted.specularMap.has_value());
}

TEST_F(ModelMaterialsTest, APaintedLightIsItsMapAndANonNegativeGain) {
    for (const std::string_view light :
         {"light = { gain = 1.0 }", R"(light = { map = "models/light.tga", gain = -1.0 })"}) {
        SCOPED_TRACE(light);
        const auto failure =
            failureOf(std::format("[[textures]]\ntexture = \"skin.bmp\"\n[textures.painted]\n{}\n", light));
        EXPECT_EQ(failure.code, ModelMaterialError::FileInvalid);
        EXPECT_NE(nx::describe(failure).find("light"), std::string::npos);
    }
    const auto missing = failureOf(R"(
[[textures]]
texture = "skin.bmp"
painted = { light = { map = "models/missing.tga" } }
)");
    EXPECT_EQ(missing.code, ModelMaterialError::ImageUnreadable);
    EXPECT_NE(nx::describe(missing).find("models/missing.tga"), std::string::npos);
}

TEST_F(ModelMaterialsTest, ASpecularColorIsThreeSharesFromZeroToOne) {
    for (const std::string_view channels :
         {"[]", "[0.0, 0.5]", "[0.0, 0.5, 1.0, 0.0]", "[-0.01, 0.0, 0.0]", "[0.0, 1.01, 0.0]",
          "[0.0, 0.0, nan]", "[inf, 0.0, 0.0]", "[0.0, \"gold\", 0.0]"}) {
        SCOPED_TRACE(channels);
        const auto failure = failureOf(std::format(
            "[[textures]]\ntexture = \"skin.bmp\"\npainted = {{ specularColor = {} }}\n", channels));
        EXPECT_EQ(failure.code, ModelMaterialError::FileInvalid);
        EXPECT_NE(nx::describe(failure).find("specularColor"), std::string::npos);
    }
}

TEST_F(ModelMaterialsTest, ASpecularWeightIsAShareFromZeroToOne) {
    for (const std::string_view weight : {"-0.01", "1.01", "nan", "inf", "-inf", "\"heavy\""}) {
        SCOPED_TRACE(weight);
        const auto failure = failureOf(std::format(
            "[[textures]]\ntexture = \"skin.bmp\"\npainted = {{ specularWeight = {} }}\n", weight));
        EXPECT_EQ(failure.code, ModelMaterialError::FileInvalid);
        EXPECT_NE(nx::describe(failure).find("specularWeight"), std::string::npos);
    }
}

TEST_F(ModelMaterialsTest, EachSurfaceTakesItsOwnFieldsAlone) {
    for (const std::string_view surface :
         {"painted = { metallic = 0.9 }", "metallicRoughness = { specularWeight = 0.5 }",
          "surface = \"painted\"", "specularColor = [0.04, 0.04, 0.04]"}) {
        SCOPED_TRACE(surface);
        const auto failure = failureOf(std::format("[[textures]]\ntexture = \"skin.bmp\"\n{}\n", surface));
        EXPECT_EQ(failure.code, ModelMaterialError::FileInvalid) << "an unknown key where it is";
    }
}

TEST_F(ModelMaterialsTest, ATextureHasOneSurfaceAtMost) {
    const auto failure = failureOf(R"(
[[textures]]
texture = "skin.bmp"
metallicRoughness = { metallic = 1.0 }
painted = {}
)");
    EXPECT_EQ(failure.code, ModelMaterialError::SurfaceAmbiguous);
    EXPECT_NE(nx::describe(failure).find("textures[0]"), std::string::npos);
}

TEST_F(ModelMaterialsTest, ATextureIsNamedOnce) {
    const auto failure = failureOf(R"(
[[textures]]
texture = "skin.bmp"
metallicRoughness = { metallic = 0.9 }

[[textures]]
texture = "SKIN.BMP"
painted = {}
)");
    EXPECT_EQ(failure.code, ModelMaterialError::TextureRepeated);
    EXPECT_NE(nx::describe(failure).find("textures[1]"), std::string::npos);
}

TEST_F(ModelMaterialsTest, ATextureTheModelLacksIsNamedInTheFailure) {
    const auto failure = failureOf(R"(
[[textures]]
texture = "other.bmp"
painted = {}
)");
    EXPECT_EQ(failure.code, ModelMaterialError::TextureUnknown);
    EXPECT_NE(nx::describe(failure).find("other.bmp"), std::string::npos);
}

TEST_F(ModelMaterialsTest, AnUnreadableSpecularMapIsNamedInTheFailure) {
    const auto failure = failureOf(R"(
[[textures]]
texture = "skin.bmp"
painted = { specularMap = "models/missing.tga" }
)");
    EXPECT_EQ(failure.code, ModelMaterialError::ImageUnreadable);
    EXPECT_NE(nx::describe(failure).find("models/missing.tga"), std::string::npos);
}

} // namespace
