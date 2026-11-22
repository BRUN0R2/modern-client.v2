// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.math;
import nx.assets;
import nx.render;
import cs.shared;
import cs.client;

#include "consoleFixtures.hpp"
#include "temporaryDirectory.hpp"
#include "triangleModel.hpp"

namespace {

constexpr std::size_t kTextureCountOffset = 180;
constexpr std::size_t kSkinFamilyCountOffset = 196;
constexpr std::size_t kBodyPartTableOffset = 204;
constexpr std::size_t kBodyPartModelsOffset = 72;
constexpr std::size_t kBodyPartModelCountOffset = 64;
constexpr std::size_t kBodyPartBytes = 76;
constexpr std::size_t kModelRecordBytes = 112;
constexpr std::size_t kMeshSkinReferenceOffset = 8;
/// Where a sequence keeps its events, and an event its number and its options.
constexpr std::size_t kSequenceEventsOffset = 52;
constexpr std::size_t kEventNumberOffset = 4;
constexpr std::size_t kEventOptionsOffset = 12;
constexpr std::size_t kEventOptionsBytes = 64;

/// A synthetic model with two skins and, when asked, two choices of its body part.
class ViewModelAppearanceTest : public testing::Test {
protected:
    nx::test::TemporaryDirectory files;
    nx::test::TriangleModel model;

    [[nodiscard]] auto weapon() const -> cs::Weapon {
        return cs::Weapon{
            .id = "triangle",
            .models = {.view = "models/triangle.mdl", .viewHand = cs::Hand::Left},
            .view = {.drawSequences = {0},
                     .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}}}};
    }

    void writeBytes(std::string_view path, std::span<const std::byte> bytes) const {
        const std::string text =
            bytes | std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
            std::ranges::to<std::string>();
        files.write(path, text);
    }

    [[nodiscard]] auto read(const cs::Weapon& weapon) const -> nx::Result<cs::ViewModel, cs::ViewModelError> {
        writeBytes(weapon.models.view, model.bytes);
        const std::string_view path = weapon.models.view;
        writeBytes(std::format("{}triangle01.mdl", path.substr(0, path.rfind('/') + 1)), model.groupFile);
        auto vfs = nx::assets::Vfs::create(std::array{files.path()});
        EXPECT_TRUE(vfs.has_value()) << (vfs ? "" : nx::describe(vfs.error()));
        if (!vfs) {
            return nx::fail(cs::ViewModelError::FilesFailed, "cannot mount the test fixture",
                            std::move(vfs.error()));
        }
        return cs::ViewModel::read(*vfs, weapon);
    }

    [[nodiscard]] auto fieldAt(std::size_t offset) const -> std::uint32_t {
        return nx::loadLittleEndian<std::uint32_t>(std::span{model.bytes}.subspan(offset));
    }

    void addBodyChoice() {
        const std::size_t part = fieldAt(kBodyPartTableOffset + sizeof(std::int32_t));
        const std::size_t choice = fieldAt(part + kBodyPartModelsOffset);
        const std::span<const std::byte> record = std::span{model.bytes}.subspan(choice, kModelRecordBytes);
        nx::test::Bytes choices{record.begin(), record.end()};
        choices.insert(choices.end(), record.begin(), record.end());
        const std::size_t choicesAt = model.add(choices);
        model.put(part + kBodyPartModelCountOffset, std::int32_t{2});
        model.put(part + kBodyPartModelsOffset, static_cast<std::int32_t>(choicesAt));
    }

    void addBodyPart() {
        const std::size_t part = fieldAt(kBodyPartTableOffset + sizeof(std::int32_t));
        const std::span<const std::byte> record = std::span{model.bytes}.subspan(part, kBodyPartBytes);
        nx::test::Bytes parts{record.begin(), record.end()};
        parts.insert(parts.end(), record.begin(), record.end());
        model.table(kBodyPartTableOffset, 2, parts);
    }
};

TEST_F(ViewModelAppearanceTest, KeepsOriginalDefaults) {
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->skin(), 0U);
    EXPECT_TRUE(viewModel->bodyChoices().empty());
}

TEST_F(ViewModelAppearanceTest, ReadsSequenceGroupBesideARelocatedModel) {
    cs::Weapon declared = weapon();
    declared.models.view = "models/pack/triangle.mdl";
    declared.view.drawSequences = {1};
    const auto viewModel = read(declared);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->sequence(1).label, "wave");
}

TEST_F(ViewModelAppearanceTest, DecodesEverySequenceWhenSeveralShareOneExternalGroup) {
    constexpr std::size_t kSequenceBytes = 176;
    constexpr std::size_t kSequenceCountOffset = 164;
    constexpr std::size_t kOriginalSequenceCount = 2;
    constexpr std::int32_t kExtendedSequenceCount = 3;
    const auto original =
        std::span{model.bytes}.subspan(model.sequences, kOriginalSequenceCount * kSequenceBytes);
    nx::test::Bytes sequences{original.begin(), original.end()};
    const auto wave = original.subspan(kSequenceBytes);
    sequences.insert(sequences.end(), wave.begin(), wave.end());
    std::ignore = model.table(kSequenceCountOffset, kExtendedSequenceCount, sequences);
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    const std::vector<nx::Mat3x4> first = viewModel->pose({}, 1, {}, cs::Hand::Left);
    const std::vector<nx::Mat3x4> second = viewModel->pose({}, 2, {}, cs::Hand::Left);
    EXPECT_EQ(first, second);
    EXPECT_EQ(viewModel->sequence(0).label, "idle");
    EXPECT_EQ(viewModel->sequence(2).label, "wave");
}

TEST_F(ViewModelAppearanceTest, IsMirroredInTheHandItIsNotModeledIn) {
    // Modeled in the left hand, as Counter-Strike's guns are: mirrored for a right-handed player
    // (cl_righthand 1), as the original draws them, and drawn as it is for a left-handed one.
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_TRUE(viewModel->isMirroredIn(cs::Hand::Right));
    EXPECT_FALSE(viewModel->isMirroredIn(cs::Hand::Left));
    const nx::render::Camera camera{};
    const std::vector<nx::Mat3x4> right = viewModel->pose(camera, 0, nx::Seconds{}, cs::Hand::Right);
    const std::vector<nx::Mat3x4> left = viewModel->pose(camera, 0, nx::Seconds{}, cs::Hand::Left);
    ASSERT_EQ(right.size(), left.size());
    ASSERT_FALSE(right.empty());
    // At the view's origin, unturned, the view's right is the world's y: what the bone has along it
    // turned over, the rest as it is.
    const nx::Mat3x4& mirrored = right.front();
    const nx::Mat3x4& asModeled = left.front();
    for (std::size_t row = 0; row < asModeled.rows.size(); ++row) {
        const float side = row == 1 ? -1.0F : 1.0F;
        EXPECT_FLOAT_EQ(mirrored.rows[row].x, side * asModeled.rows[row].x) << row;
        EXPECT_FLOAT_EQ(mirrored.rows[row].y, side * asModeled.rows[row].y) << row;
        EXPECT_FLOAT_EQ(mirrored.rows[row].z, side * asModeled.rows[row].z) << row;
        EXPECT_FLOAT_EQ(mirrored.rows[row].w, side * asModeled.rows[row].w) << row;
    }
}

TEST_F(ViewModelAppearanceTest, OffsetsMoveTheMuzzleInCameraSpaceInEitherHand) {
    const auto view = read(weapon());
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    const nx::render::Camera camera{.position = {.x = 100.0F, .y = 200.0F, .z = 300.0F},
                                    .angles = {.yaw = nx::Degrees{90.0F}}};
    const cs::ViewModelOffset offset{
        .right = nx::HammerUnits{1.5F}, .forward = nx::HammerUnits{2.0F}, .up = nx::HammerUnits{-0.5F}};
    constexpr float kPositionTolerance = 0.0001F;
    for (const cs::Hand hand : {cs::Hand::Left, cs::Hand::Right}) {
        const auto before = view->muzzleAt(view->pose(camera, 0, {}, hand), 0);
        const auto after = view->muzzleAt(view->pose(camera, 0, {}, hand, offset), 0);
        ASSERT_TRUE(before.has_value());
        ASSERT_TRUE(after.has_value());
        const nx::Vec3 delta = *after - *before;
        EXPECT_NEAR(delta.x, offset.right.value(), kPositionTolerance);
        EXPECT_NEAR(delta.y, offset.forward.value(), kPositionTolerance);
        EXPECT_NEAR(delta.z, offset.up.value(), kPositionTolerance);
    }
}

TEST(ViewModelBobTest, GentlyFollowsSpeedAndSettlesWithoutDependingOnFrameRate) {
    constexpr nx::UnitsPerSecond kRunningSpeed{240.0F};
    constexpr float kTolerance = 0.0001F;
    cs::ViewModelBob single;
    EXPECT_EQ(single.offset().up, nx::HammerUnits{});
    single.update(kRunningSpeed, nx::Seconds{0.2F});
    cs::ViewModelBob split;
    for (int frame = 0; frame < 20; ++frame) {
        split.update(kRunningSpeed, nx::Seconds{0.01F});
    }
    const auto offset = single.offset();
    EXPECT_GT(std::abs(offset.right.value()), 0.05F);
    EXPECT_LT(std::abs(offset.up.value()), 0.2F);
    EXPECT_NEAR(offset.right.value(), split.offset().right.value(), kTolerance);
    EXPECT_NEAR(offset.up.value(), split.offset().up.value(), kTolerance);

    single.update({}, nx::Seconds{1.5F});
    EXPECT_NEAR(single.offset().right.value(), 0.0F, kTolerance);
    EXPECT_NEAR(single.offset().up.value(), 0.0F, kTolerance);
}

TEST(ViewModelSettingsTest, ArchivesOffsetsAndRejectsValuesOutsideTheirRanges) {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    const auto settings = cs::ViewModelCvars::registerIn(console);
    ASSERT_TRUE(settings.has_value()) << nx::describe(settings.error());
    EXPECT_TRUE(settings->right.set(2.5F).has_value());
    EXPECT_FALSE(settings->forward.set(2.5F).has_value());
    EXPECT_TRUE(settings->up.set(-1.0F).has_value());
    EXPECT_FALSE(console.setCvar("viewmodel_bob", "1").has_value());
    const cs::ViewModelBob still{};
    EXPECT_EQ(settings->offset(still).right, nx::HammerUnits{2.5F});
    EXPECT_EQ(settings->offset(still).up, nx::HammerUnits{-1.0F});
    const std::string archived = console.archivedCvars();
    for (const std::string_view name : {"viewmodel_offset_x", "viewmodel_offset_y", "viewmodel_offset_z"}) {
        EXPECT_TRUE(archived.contains(name)) << name;
    }
}

TEST_F(ViewModelAppearanceTest, BindsDeclaredSkinAndBodyChoice) {
    addBodyChoice();
    cs::Weapon declared = weapon();
    declared.view.skin = 1;
    declared.view.bodyChoices = std::vector<std::uint32_t>{1};
    const auto viewModel = read(declared);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->skin(), 1U);
    EXPECT_TRUE(std::ranges::equal(viewModel->bodyChoices(), std::array<std::uint32_t, 1>{1}));
}

TEST_F(ViewModelAppearanceTest, LeavesOmittedBodyPartsAtTheirOriginalChoice) {
    addBodyChoice();
    addBodyPart();
    cs::Weapon declared = weapon();
    declared.view.bodyChoices = std::vector<std::uint32_t>{1};
    const auto viewModel = read(declared);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_TRUE(std::ranges::equal(viewModel->bodyChoices(), std::array<std::uint32_t, 1>{1}));
    // The renderer's contract leaves later body parts at choice zero, without a padded copied span.
    EXPECT_EQ(viewModel->bodyChoices().size(), 1U);
}

TEST_F(ViewModelAppearanceTest, OwnsAppearanceAfterWeaponChangesAndModelMoves) {
    addBodyChoice();
    cs::Weapon declared = weapon();
    declared.view.skin = 1;
    declared.view.bodyChoices = std::vector<std::uint32_t>{1};
    auto readModel = read(declared);
    ASSERT_TRUE(readModel.has_value()) << nx::describe(readModel.error());
    cs::ViewModel moved = std::move(*readModel);
    declared.view.skin = 0;
    declared.view.bodyChoices->clear();
    EXPECT_EQ(moved.skin(), 1U);
    EXPECT_TRUE(std::ranges::equal(moved.bodyChoices(), std::array<std::uint32_t, 1>{1}));
}

TEST_F(ViewModelAppearanceTest, RejectsSkinPastTheAvailableFamilies) {
    cs::Weapon declared = weapon();
    declared.view.skin = 2;
    const auto viewModel = read(declared);
    ASSERT_FALSE(viewModel.has_value());
    EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
    EXPECT_NE(nx::describe(viewModel.error()).find("view.skin"), std::string::npos);
}

TEST_F(ViewModelAppearanceTest, RejectsBodyPartIndexesPastTheModel) {
    cs::Weapon declared = weapon();
    declared.view.bodyChoices = std::vector<std::uint32_t>{0, 0};
    const auto viewModel = read(declared);
    ASSERT_FALSE(viewModel.has_value());
    EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
    EXPECT_NE(nx::describe(viewModel.error()).find("view.bodyChoices"), std::string::npos);
}

TEST_F(ViewModelAppearanceTest, RejectsUnavailableBodyChoices) {
    addBodyChoice();
    cs::Weapon declared = weapon();
    declared.view.bodyChoices = std::vector<std::uint32_t>{2};
    const auto viewModel = read(declared);
    ASSERT_FALSE(viewModel.has_value());
    EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
    EXPECT_NE(nx::describe(viewModel.error()).find("view.bodyChoices[0]"), std::string::npos);
}

TEST_F(ViewModelAppearanceTest, BindsSkinsFromTheSeparateTexturesFile) {
    writeBytes("models/triangleT.mdl", model.bytes);
    model.put(kTextureCountOffset, std::int32_t{0});
    model.put(kSkinFamilyCountOffset, std::int32_t{1});
    cs::Weapon declared = weapon();
    declared.view.skin = 1;
    const auto viewModel = read(declared);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->skin(), 1U)
        << "the main model's skin count does not describe the external texture file";
}

TEST_F(ViewModelAppearanceTest, RejectsSkinsOutsideTheSeparateTexturesFile) {
    writeBytes("models/triangleT.mdl", model.bytes);
    model.put(kTextureCountOffset, std::int32_t{0});
    model.put(kSkinFamilyCountOffset, std::int32_t{3});
    cs::Weapon declared = weapon();
    declared.view.skin = 2;
    const auto viewModel = read(declared);
    ASSERT_FALSE(viewModel.has_value());
    EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
}

TEST_F(ViewModelAppearanceTest, RejectsTextureFilesThatCannotBindTheModelsSkinReferences) {
    writeBytes("models/triangleT.mdl", model.bytes);
    model.put(kTextureCountOffset, std::int32_t{0});
    model.put(model.mesh + kMeshSkinReferenceOffset, std::int32_t{1});
    const auto viewModel = read(weapon());
    ASSERT_FALSE(viewModel.has_value());
    EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
    EXPECT_NE(nx::describe(viewModel.error()).find("skin reference"), std::string::npos);
}

TEST_F(ViewModelAppearanceTest, KeepsStaticAppearanceWhileAlternateChangesSequences) {
    addBodyChoice();
    cs::Weapon declared = weapon();
    declared.view.skin = 1;
    declared.view.bodyChoices = std::vector<std::uint32_t>{1};
    declared.alternate.emplace();
    declared.alternate->view = weapon().view;
    const auto viewModel = read(declared);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->skin(), 1U);
    EXPECT_TRUE(std::ranges::equal(viewModel->bodyChoices(), std::array<std::uint32_t, 1>{1}));
}

/// The same model, whose "idle" plays a sound event: player/pl_step1.wav.
class ViewModelSoundsTest : public ViewModelAppearanceTest {};

TEST_F(ViewModelSoundsTest, ItsSoundEventsNameTheSoundsTheyPlay) {
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->soundEvents(), (std::vector<std::string_view>{"player/pl_step1.wav"}));
}

TEST_F(ViewModelSoundsTest, PlaysSoundEventsAcrossMoreThanOneLoop) {
    const std::size_t event = fieldAt(model.sequences + kSequenceEventsOffset);
    model.put(event, std::int32_t{0});
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    cs::ViewModelEvents events;
    cs::ViewModelPose pose{.intoIt = nx::Seconds{0.025F}};
    ASSERT_EQ(events.passed(&*viewModel, pose).size(), 1U);
    pose.intoIt = nx::Seconds{0.075F};
    const auto passed = events.passed(&*viewModel, pose);
    ASSERT_EQ(passed.size(), 2U);
    EXPECT_EQ(passed[0].event->event, cs::kSoundEvent);
    EXPECT_FLOAT_EQ(passed[0].intoPlay.count(), 1.0F / 30.0F);
    EXPECT_FLOAT_EQ(passed[1].intoPlay.count(), 2.0F / 30.0F);
    EXPECT_TRUE(events.passed(&*viewModel, pose).empty());
}

TEST_F(ViewModelSoundsTest, AWeaponInHandSoundsAsItsDataAndItsViewModelsEventsSay) {
    cs::Weapon held = weapon();
    held.view.drawSound = cs::GameSound{.files = {"weapons/draw1.wav", "weapons/draw2.wav"}};
    const auto viewModel = read(held);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(
        cs::soundFilesOf(held, &*viewModel),
        (std::vector<std::string_view>{"weapons/draw1.wav", "weapons/draw2.wav", "player/pl_step1.wav"}));
    EXPECT_EQ(cs::soundFilesOf(held, nullptr),
              (std::vector<std::string_view>{"weapons/draw1.wav", "weapons/draw2.wav"}))
        << "a weapon not seen in hand";
}

TEST_F(ViewModelAppearanceTest, RejectsStaticAppearanceInAlternateViews) {
    for (const std::string_view key : {"skin", "bodyChoices", "muzzleFlash"}) {
        SCOPED_TRACE(key);
        cs::Weapon declared = weapon();
        declared.alternate.emplace();
        declared.alternate->view = declared.view;
        if (key == "skin") {
            declared.alternate->view->skin = 0;
        } else if (key == "bodyChoices") {
            declared.alternate->view->bodyChoices = std::vector<std::uint32_t>{};
        } else {
            declared.alternate->view->muzzleFlash = cs::MuzzleFlashLook{.brightness = nx::Scale{2.0F}};
        }
        const auto viewModel = read(declared);
        ASSERT_FALSE(viewModel.has_value());
        EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
        EXPECT_NE(nx::describe(viewModel.error()).find("alternate.view"), std::string::npos);
    }
}

/// The same model, whose "idle" shows a muzzle flash in place of its sound.
class ViewModelMuzzleFlashTest : public ViewModelAppearanceTest {
protected:
    /// Its event a muzzle flash's at the attachment named by `eventNumber`.
    void flashes(std::string_view options, std::int32_t eventNumber = 5001) {
        const std::size_t event = fieldAt(model.sequences + kSequenceEventsOffset);
        model.put(event + kEventNumberOffset, eventNumber);
        nx::test::Bytes written;
        nx::test::appendName(written, options, kEventOptionsBytes);
        std::ranges::copy(written,
                          model.bytes.begin() + static_cast<std::ptrdiff_t>(event + kEventOptionsOffset));
    }
};

TEST_F(ViewModelMuzzleFlashTest, ItsEventsNameTheSpritesTheirFlashesShowEachOnce) {
    flashes("#I341 S0.16 F0 P30 T999");
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->muzzleFlashSprites(), (std::vector<std::string>{"sprites/muzzleflash341.spr"}));
    EXPECT_TRUE(viewModel->soundEvents().empty()) << "its event is a flash's";
}

TEST_F(ViewModelMuzzleFlashTest, TheFiringSequenceSelectsTheMuzzleInEitherHand) {
    constexpr std::size_t kAttachmentTableOffset = 212;
    const auto attachment = [](nx::Vec3 origin) {
        nx::test::Bytes record;
        nx::test::appendName(record, "muzzle", 32);
        nx::test::append(record, std::int32_t{0});
        nx::test::append(record, std::int32_t{1});
        nx::test::appendVec3(record, origin);
        record.resize(record.size() + 36);
        return record;
    };
    auto muzzles = attachment({.x = 4.0F, .y = 2.0F});
    const auto second = attachment({.x = 8.0F, .y = 4.0F});
    muzzles.insert(muzzles.end(), second.begin(), second.end());
    std::ignore = model.table(kAttachmentTableOffset, 2, muzzles);
    flashes("22", 5011);
    const auto viewModel = read(weapon());
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    const auto leftBones = viewModel->pose({}, 0, {}, cs::Hand::Left);
    const auto rightBones = viewModel->pose({}, 0, {}, cs::Hand::Right);
    const auto left = viewModel->muzzleAt(leftBones, 0);
    const auto right = viewModel->muzzleAt(rightBones, 0);
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(left, viewModel->attachmentAt(leftBones, 1));
    EXPECT_NE(left, viewModel->attachmentAt(leftBones, 0));
    EXPECT_FLOAT_EQ(left->y, -right->y);
    EXPECT_FLOAT_EQ(left->x, right->x);
    EXPECT_FLOAT_EQ(left->z, right->z);
}

TEST_F(ViewModelMuzzleFlashTest, ItsWeaponsLookStylesItsFlashes) {
    flashes("22");
    cs::Weapon styled = weapon();
    styled.view.muzzleFlash =
        cs::MuzzleFlashLook{.sprite = "sprites/goldFlash.spr", .brightness = nx::Scale{4.0F}};
    const auto viewModel = read(styled);
    ASSERT_TRUE(viewModel.has_value()) << nx::describe(viewModel.error());
    EXPECT_EQ(viewModel->muzzleFlashSprites(), (std::vector<std::string>{"sprites/goldFlash.spr"}));
    const cs::MuzzleFlashStyle style = viewModel->muzzleFlashStyleOf(viewModel->sequence(0).events.front());
    EXPECT_EQ(style.sprite, "sprites/goldFlash.spr");
    EXPECT_FLOAT_EQ(style.scale.value(), 0.2F) << "the event's";
    EXPECT_FLOAT_EQ(style.brightness.value(), 4.0F);
}

} // namespace
