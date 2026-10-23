// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import cs.shared;
import cs.client;

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
        writeBytes("models/triangle01.mdl", model.groupFile);
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

TEST_F(ViewModelAppearanceTest, RejectsStaticAppearanceInAlternateViews) {
    for (const bool declaresSkin : {false, true}) {
        SCOPED_TRACE(declaresSkin);
        cs::Weapon declared = weapon();
        declared.alternate.emplace();
        declared.alternate->view = declared.view;
        if (declaresSkin) {
            declared.alternate->view->skin = 0;
        } else {
            declared.alternate->view->bodyChoices = std::vector<std::uint32_t>{};
        }
        const auto viewModel = read(declared);
        ASSERT_FALSE(viewModel.has_value());
        EXPECT_EQ(viewModel.error().code, cs::ViewModelError::NoAppearance);
        EXPECT_NE(nx::describe(viewModel.error()).find("alternate.view"), std::string::npos);
    }
}

} // namespace
