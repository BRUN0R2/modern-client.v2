// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;

#include "triangleModel.hpp"

namespace {

using nx::assets::BoneAxis;
using nx::assets::BoneIndex;
using nx::assets::MdlError;
using nx::assets::MdlView;
using nx::assets::SequenceIndex;
using nx::assets::StudioTextureFlag;
using nx::assets::Winding;

using Bytes = std::vector<std::byte>;
using nx::test::TriangleModel;

[[nodiscard]] auto parseError(const TriangleModel& model) -> MdlError {
    const auto view = MdlView::parse(model.bytes);
    EXPECT_FALSE(view.has_value());
    return view ? MdlError{} : view.error().code;
}

TEST(MdlTest, StudioIndicesPreserveTagIdentityAndIntegerRepresentation) {
    constexpr std::uint32_t kLargest = std::numeric_limits<std::uint32_t>::max();
    static_assert(SequenceIndex{}.value == 0);
    static_assert(SequenceIndex{kLargest}.value == kLargest);
    static_assert(sizeof(SequenceIndex) == sizeof(std::uint32_t));
    static_assert(alignof(SequenceIndex) == alignof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<SequenceIndex>);
    static_assert(std::is_nothrow_default_constructible_v<SequenceIndex>);
    static_assert(std::is_nothrow_constructible_v<SequenceIndex, std::uint32_t>);
    static_assert(!std::convertible_to<std::uint32_t, SequenceIndex>);
    static_assert(!std::constructible_from<SequenceIndex, BoneIndex>);
    SUCCEED();
}

TEST(MdlTest, ReadsTheSkeletonAndWhatHangsOnIt) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_EQ(view->name(), "models/triangle.mdl");
    EXPECT_EQ(view->eyePosition(), (nx::Vec3{.x = 0.0F, .y = 0.0F, .z = 28.0F}));
    EXPECT_EQ(view->bounds().max.z, 72.0F);

    ASSERT_EQ(view->bones().size(), 2U);
    EXPECT_FALSE(view->bones()[0].parent.has_value());
    EXPECT_EQ(view->bones()[1].parent, std::optional{BoneIndex{0}});
    EXPECT_EQ(view->bones()[1].name, "head");
    EXPECT_EQ(view->bones()[1].values[2], 10.0F);
    EXPECT_TRUE(view->bones()[1].controllers[4].has_value());
    EXPECT_FALSE(view->bones()[1].controllers[3].has_value());

    ASSERT_EQ(view->boneControllers().size(), 1U);
    const auto& controller = view->boneControllers()[0];
    EXPECT_EQ(controller.bone.value, 1U);
    EXPECT_EQ(controller.axis, BoneAxis::RotationY);
    EXPECT_TRUE(controller.isCyclic);
    EXPECT_EQ(controller.end, 30.0F);

    ASSERT_EQ(view->hitboxes().size(), 1U);
    EXPECT_EQ(view->hitboxes()[0].group, 1U);
    EXPECT_EQ(view->hitboxes()[0].bounds.max, (nx::Vec3{.x = 4.0F, .y = 4.0F, .z = 8.0F}));
    ASSERT_EQ(view->attachments().size(), 1U);
    EXPECT_EQ(view->attachments()[0].name, "eyes");
    EXPECT_EQ(view->attachments()[0].origin.z, 4.0F);
}

TEST(MdlTest, ReadsTexturesSkinsAndGeometry) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());

    ASSERT_EQ(view->textures().size(), 1U);
    const auto& texture = view->textures()[0];
    EXPECT_EQ(texture.name, "skin.bmp");
    EXPECT_TRUE(texture.flags.test(StudioTextureFlag::Chrome));
    EXPECT_TRUE(texture.flags.test(StudioTextureFlag::Masked));
    EXPECT_FALSE(texture.flags.test(StudioTextureFlag::Additive));
    EXPECT_EQ(texture.pixels.size(), 256U);
    EXPECT_EQ(texture.palette.size(), nx::assets::kPaletteBytes);
    EXPECT_EQ(texture.palette[0], std::byte{9});
    ASSERT_EQ(view->skins().size(), 2U);
    EXPECT_EQ(view->skins()[1].size(), 1U);

    ASSERT_EQ(view->bodyParts().size(), 1U);
    const auto& part = view->bodyParts()[0];
    EXPECT_EQ(part.name, "body");
    ASSERT_EQ(part.models.size(), 1U);
    const auto& triangle = part.models[0];
    EXPECT_EQ(triangle.vertices.size(), 3U);
    EXPECT_EQ(triangle.vertices[1].bone.value, 1U);
    EXPECT_EQ(triangle.normals.size(), 1U);
    ASSERT_EQ(triangle.meshes.size(), 1U);
    ASSERT_EQ(triangle.meshes[0].runs.size(), 1U);
    const auto& run = triangle.meshes[0].runs[0];
    EXPECT_EQ(run.winding, Winding::Strip);
    ASSERT_EQ(run.corners.size(), 3U);
    EXPECT_EQ(run.corners[2].vertex.value, 2U);
    EXPECT_EQ(run.corners[2].s, 8);
}

TEST(MdlTest, AModelWhoseTexturesAreInItsTFileJoinsThem) {
    TriangleModel model;
    model.put(180, std::int32_t{0}); // no textures
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_TRUE(view->textures().empty());
    EXPECT_TRUE(view->skins().empty());

    const TriangleModel textures; // one texture and one skin reference, as a *T.mdl has
    const auto texturesFile = MdlView::parse(textures.bytes);
    ASSERT_TRUE(texturesFile.has_value()) << nx::describe(texturesFile.error());
    const auto joined = view->checkTextures(*texturesFile);
    EXPECT_TRUE(joined.has_value()) << nx::describe(joined.error());

    EXPECT_EQ(view->checkTextures(*view).error().code, MdlError::BadValue)
        << "a textures file without textures";

    TriangleModel farReference = model;
    farReference.put(farReference.mesh + 8, std::int32_t{1});
    const auto far = MdlView::parse(farReference.bytes);
    ASSERT_TRUE(far.has_value()) << nx::describe(far.error());
    EXPECT_EQ(far->checkTextures(*texturesFile).error().code, MdlError::BadIndex) << "skin reference 1 of 1";
}

TEST(MdlTest, TheTexturesFileIsNamedAfterTheModel) {
    const auto pathOf = [](std::string_view model) -> std::string {
        const auto path = nx::assets::VfsPath::parse(model).and_then(nx::assets::texturesFilePath);
        return path ? std::string{path->text()} : nx::describe(path.error());
    };
    EXPECT_EQ(pathOf("models/player/gign/gign.mdl"), "models/player/gign/gignT.mdl");
    EXPECT_EQ(pathOf("models/V_AK47.MDL"), "models/V_AK47T.MDL");

    const auto map = nx::assets::VfsPath::parse("maps/de_dust2.bsp");
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(nx::assets::texturesFilePath(*map).error().code, nx::assets::VfsError::InvalidPath);
}

TEST(MdlTest, ReadsTheSequences) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    ASSERT_EQ(view->sequenceGroups().size(), 2U);
    EXPECT_EQ(view->sequenceGroups()[1].fileName, "models/triangle01.mdl");
    ASSERT_EQ(view->sequences().size(), 2U);
    const auto& idle = view->sequences()[0];
    EXPECT_EQ(idle.label, "idle");
    EXPECT_EQ(idle.fps, 30.0F);
    EXPECT_TRUE(idle.isLooping);
    EXPECT_EQ(idle.frameCount, 2U);
    EXPECT_EQ(idle.blendCount, 1U);
    EXPECT_EQ(idle.group.value, 0U);
    ASSERT_EQ(idle.events.size(), 1U);
    EXPECT_EQ(idle.events[0].frame, 1);
    EXPECT_EQ(idle.events[0].event, 5004);
    EXPECT_EQ(idle.events[0].options, "player/pl_step1.wav");
    EXPECT_EQ(view->sequences()[1].group.value, 1U);
}

TEST(MdlTest, DecodesAnimationsHereAndInSequenceGroups) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());

    const auto idle = view->decodeAnimation(SequenceIndex{0}, model.bytes);
    ASSERT_TRUE(idle.has_value()) << nx::describe(idle.error());
    EXPECT_EQ(idle->values.size(), 2U * 2U * 6U);
    const BoneIndex root{0};
    const BoneIndex head{1};
    EXPECT_EQ(idle->value(0, 0, root, BoneAxis::Z), 5);
    EXPECT_EQ(idle->value(0, 1, root, BoneAxis::Z), 7);
    EXPECT_EQ(idle->value(0, 0, head, BoneAxis::RotationY), 100);
    EXPECT_EQ(idle->value(0, 1, head, BoneAxis::RotationY), 100) << "past its values, a run repeats the last";
    EXPECT_EQ(idle->value(0, 1, head, BoneAxis::X), 0) << "an axis without a stream stays at 0";

    const auto wave = view->decodeAnimation(SequenceIndex{1}, model.groupFile);
    ASSERT_TRUE(wave.has_value()) << nx::describe(wave.error());
    EXPECT_EQ(wave->value(0, 0, head, BoneAxis::RotationY), -3);
}

TEST(MdlTest, RejectsABrokenAnimation) {
    const auto animationError = [](auto change, std::uint32_t sequence = 0) {
        TriangleModel model;
        change(model);
        const auto view = MdlView::parse(model.bytes);
        EXPECT_TRUE(view.has_value()) << nx::describe(view.error());
        if (!view) {
            return MdlError{};
        }
        const auto frames =
            view->decodeAnimation(SequenceIndex{sequence}, sequence == 0 ? model.bytes : model.groupFile);
        EXPECT_FALSE(frames.has_value());
        return frames ? MdlError{} : frames.error().code;
    };
    EXPECT_EQ(animationError([](TriangleModel&) {}, 2), MdlError::BadIndex) << "sequence 2 of 2";
    EXPECT_EQ(animationError([](TriangleModel& model) { model.bytes[model.animation + 24] = std::byte{0}; }),
              MdlError::BadValue)
        << "a run of no value";
    EXPECT_EQ(
        animationError([](TriangleModel& model) { model.put(model.animation + 4, std::uint16_t{60000}); }),
        MdlError::OutOfBounds);
    EXPECT_EQ(animationError([](TriangleModel& model) { model.groupFile[3] = std::byte{'T'}; }, 1),
              MdlError::NotSequenceGroup);
    EXPECT_EQ(
        animationError([](TriangleModel& model) { model.put(model.sequences + 56, std::int32_t{1 << 30}); }),
        MdlError::BadValue)
        << "more than kMaxAnimationValues";
}

TEST(MdlTest, RejectsABrokenSequence) {
    const auto withChange = [](auto change) {
        TriangleModel model;
        change(model);
        return parseError(model);
    };
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 56, std::int32_t{0}); }),
              MdlError::BadValue)
        << "no frame";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 120, std::int32_t{0}); }),
              MdlError::BadValue)
        << "no blend";
    EXPECT_EQ(withChange([](TriangleModel& model) {
                  model.put(model.sequences + 32, std::numeric_limits<float>::quiet_NaN());
              }),
              MdlError::BadValue);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 156, std::int32_t{2}); }),
              MdlError::BadIndex)
        << "group 2 of 2";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 72, std::int32_t{2}); }),
              MdlError::BadIndex)
        << "motion bone 2 of 2";
    EXPECT_EQ(
        withChange([](TriangleModel& model) { model.put(model.sequences + 52, std::int32_t{1 << 24}); }),
        MdlError::OutOfBounds)
        << "events past the file";
}

TEST(MdlTest, RejectsABrokenHeader) {
    EXPECT_EQ(MdlView::parse(Bytes(243)).error().code, MdlError::TooSmall);
    TriangleModel idsq;
    idsq.bytes[3] = std::byte{'Q'};
    EXPECT_EQ(parseError(idsq), MdlError::NotStudioModel);
    TriangleModel version;
    version.put(4, std::int32_t{6});
    EXPECT_EQ(parseError(version), MdlError::UnsupportedVersion);
    TriangleModel tooManyBones;
    tooManyBones.put(140, std::int32_t{1000});
    EXPECT_EQ(parseError(tooManyBones), MdlError::OutOfBounds);
    TriangleModel eyes;
    eyes.put(76, std::numeric_limits<float>::infinity());
    EXPECT_EQ(parseError(eyes), MdlError::BadValue);
}

TEST(MdlTest, RejectsABrokenRecord) {
    const auto withChange = [](auto change) {
        TriangleModel model;
        change(model);
        return parseError(model);
    };
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.bones + 112 + 32, std::int32_t{1}); }),
              MdlError::BadIndex)
        << "a bone that is its own parent";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.bones + 112 + 40, std::int32_t{1}); }),
              MdlError::BadIndex)
        << "controller 1 of 1";
    EXPECT_EQ(withChange([](TriangleModel& model) {
                  model.put(model.bones + 64, std::numeric_limits<float>::quiet_NaN());
              }),
              MdlError::BadValue);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.controller + 4, std::uint32_t{0x3}); }),
              MdlError::BadValue)
        << "two axes";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.controller + 4, std::uint32_t{0x40}); }),
              MdlError::BadValue)
        << "no axis of the six";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.controller, std::int32_t{2}); }),
              MdlError::BadIndex)
        << "bone 2 of 2";
    EXPECT_EQ(withChange([](TriangleModel& model) {
                  model.put(model.hitbox + 8, std::numeric_limits<float>::infinity());
              }),
              MdlError::BadValue);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.texture + 68, std::int32_t{0}); }),
              MdlError::BadValue)
        << "a texture of no width";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.texture + 76, std::int32_t{1 << 24}); }),
              MdlError::OutOfBounds);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.skins + 2, std::int16_t{1}); }),
              MdlError::BadIndex)
        << "texture 1 of 1";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(192, std::int32_t{0}); }), MdlError::BadValue)
        << "no skin reference";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.mesh + 8, std::int32_t{1}); }),
              MdlError::BadIndex)
        << "skin reference 1 of 1";
}

TEST(MdlTest, RejectsBrokenGeometry) {
    const auto withChange = [](auto change) {
        TriangleModel model;
        change(model);
        return parseError(model);
    };
    EXPECT_EQ(withChange([](TriangleModel& model) { model.bytes[model.vertexBones + 2] = std::byte{2}; }),
              MdlError::BadIndex)
        << "bone 2 of 2";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.commands, std::int16_t{2}); }),
              MdlError::BadValue)
        << "a run of two corners";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.commands + 2 + 8, std::int16_t{3}); }),
              MdlError::BadIndex)
        << "vertex 3 of 3";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.commands + 2 + 2, std::int16_t{1}); }),
              MdlError::BadIndex)
        << "normal 1 of 1";
    EXPECT_EQ(
        withChange([](TriangleModel& model) { model.put(model.commands + 2 + 24, std::int16_t{32767}); }),
        MdlError::OutOfBounds)
        << "a run past the end of the file";
}

} // namespace
