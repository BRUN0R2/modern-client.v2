// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the textures are made of, read from a materials.txt as the original reads it and looked up as
// it looks them up; the repository's data/cstrike/footsteps.toml; and the sound each step plays: its
// kind by where it is and what the ground is made of, its foot, its volume, the rare tile and the
// silent wading step.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

using cs::Footstep;
using cs::FootstepRules;
using cs::StepKind;
using cs::StepPlace;
using cs::TextureMaterial;
using cs::TextureMaterials;

/// The repository's data/cstrike/footsteps.toml, read.
[[nodiscard]] auto repositoryRules() -> FootstepRules {
    const std::filesystem::path file =
        std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "footsteps.toml";
    auto text = nx::platform::readTextFile(file);
    EXPECT_TRUE(text.has_value());
    auto rules = cs::readFootstepRules(text.value_or(std::string{}), "footsteps.toml");
    EXPECT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    return rules.value_or(FootstepRules{});
}

TEST(TextureMaterialsTest, TheFileIsReadAsTheOriginalReadsIt) {
    const TextureMaterials materials = TextureMaterials::read("// a comment\r\n"
                                                              "\r\n"
                                                              "M metal_floor\r\n"
                                                              "  d  Dirt1  trailing words\r\n"
                                                              "Z odd_letter\r\n"
                                                              "X grass1\r\n"
                                                              "7 not_a_letter\r\n"
                                                              "V\r\n"
                                                              "T averyveryverylongname_tile\r\n"
                                                              "G last_without_end_of_line");
    EXPECT_EQ(materials.size(), 5U);
    EXPECT_EQ(materials.materialOf("metal_floor"), TextureMaterial::Metal);
    EXPECT_EQ(materials.materialOf("METAL_FLOOR"), TextureMaterial::Metal) << "looked up without case";
    EXPECT_EQ(materials.materialOf("dirt1"), TextureMaterial::Dirt) << "a lowercase letter counts";
    EXPECT_EQ(materials.materialOf("odd_letter"), TextureMaterial::Concrete) << "a letter of no material";
    EXPECT_EQ(materials.materialOf("grass1"), TextureMaterial::Grass);
    EXPECT_EQ(materials.materialOf("not_a_letter"), TextureMaterial::Concrete);
    // Names are 16 characters at most, in the file and looked up.
    EXPECT_EQ(materials.materialOf("averyveryverylon"), TextureMaterial::Tile);
    EXPECT_EQ(materials.materialOf("averyveryverylongname"), TextureMaterial::Tile);
    EXPECT_EQ(materials.materialOf("last_without_end_of_line"), TextureMaterial::Concrete)
        << "the original leaves a name its line's end of line does not follow";
    EXPECT_EQ(materials.materialOf("crate"), TextureMaterial::Concrete);
}

TEST(TextureMaterialsTest, TheFirst1024TexturesAreRead) {
    std::string text;
    for (int index = 0; index < 1100; ++index) {
        text += std::format("W wood{}\n", index);
    }
    const TextureMaterials materials = TextureMaterials::read(text);
    EXPECT_EQ(materials.size(), cs::kMostTaggedTextures);
    EXPECT_EQ(materials.materialOf("wood1023"), TextureMaterial::Wood);
    EXPECT_EQ(materials.materialOf("wood1024"), TextureMaterial::Concrete);
}

TEST(TextureMaterialsTest, ALineIsReadIn510BytesAtMost) {
    // The first 510 bytes are a line, the rest of it the next.
    std::string text = "W wood1 ";
    text.resize(cs::kMostLineBytes, ' ');
    text += "M cut_metal \n";
    // A name the 510th byte cuts is not followed by a space, and the line is left out.
    std::string cut = "W ";
    cut.resize(cs::kMostLineBytes, 'y');
    text += cut + "G grate9 \n";
    const TextureMaterials materials = TextureMaterials::read(text);
    EXPECT_EQ(materials.size(), 3U);
    EXPECT_EQ(materials.materialOf("wood1"), TextureMaterial::Wood);
    EXPECT_EQ(materials.materialOf("cut_metal"), TextureMaterial::Metal);
    EXPECT_EQ(materials.materialOf("grate9"), TextureMaterial::Grate);
}

TEST(TextureMaterialsTest, ALineEndsAtANul) {
    using namespace std::string_view_literals;
    const TextureMaterials materials = TextureMaterials::read("M na\0me \n"
                                                              "\0V nul_first \n"
                                                              "T tile\0 \n"
                                                              "S slosh1 \0junk\n"sv);
    EXPECT_EQ(materials.size(), 1U) << "a name a NUL ends is not followed by a space";
    EXPECT_EQ(materials.materialOf("slosh1"), TextureMaterial::Slosh);
    EXPECT_EQ(materials.materialOf("na"), TextureMaterial::Concrete);
    EXPECT_EQ(materials.materialOf("tile"), TextureMaterial::Concrete);
}

TEST(TextureMaterialsTest, ATexturesNameIsLookedUpWithoutItsPrefixes) {
    EXPECT_EQ(cs::lookedUpTextureName("-0crate"), "crate");
    EXPECT_EQ(cs::lookedUpTextureName("+0~light"), "light");
    EXPECT_EQ(cs::lookedUpTextureName("+a{fence"), "fence");
    EXPECT_EQ(cs::lookedUpTextureName("{fence"), "fence");
    EXPECT_EQ(cs::lookedUpTextureName("!water"), "water");
    EXPECT_EQ(cs::lookedUpTextureName("~lamp"), "lamp");
    EXPECT_EQ(cs::lookedUpTextureName("-"), "");
    EXPECT_EQ(cs::lookedUpTextureName("seventeen_letters"), "seventeen_letter");
}

TEST(FootstepRulesTest, TheRepositorysStepsAreTheOriginals) {
    const FootstepRules rules = repositoryRules();
    for (const StepKind kind : nx::EnumArray<StepKind, int>::keys()) {
        EXPECT_NE(rules.soundsOf(kind), nullptr) << nx::enumName(kind);
    }
    const cs::StepSounds& concrete = *rules.soundsOf(StepKind::Concrete);
    EXPECT_EQ(concrete.rightFoot.files,
              (std::vector<std::string>{"player/pl_step1.wav", "player/pl_step3.wav"}));
    EXPECT_EQ(concrete.leftFoot.files,
              (std::vector<std::string>{"player/pl_step2.wav", "player/pl_step4.wav"}));
    EXPECT_EQ(concrete.rightFoot.volume, 0.5F);
    EXPECT_EQ(rules.soundsOf(StepKind::Dirt)->rightFoot.volume, 0.55F);
    EXPECT_EQ(rules.soundsOf(StepKind::Vent)->rightFoot.volume, 0.7F);
    EXPECT_EQ(rules.soundsOf(StepKind::Wade)->rightFoot.volume, 0.65F);
    EXPECT_EQ(rules.soundsOf(StepKind::Ladder)->rightFoot.volume, 0.35F);
    EXPECT_EQ(rules.soundsOf(StepKind::Tile)->rare->oneIn, 5U);
    EXPECT_EQ(rules.soundsOf(StepKind::Wade)->silentEvery, 4U);
    EXPECT_EQ(rules.splash.files.size(), 4U);
    const std::vector<std::string_view> files = rules.soundFiles();
    EXPECT_EQ(files.size(), 41U) << "each file once: the splash's are the wading steps'";
    EXPECT_EQ(files.front(), "player/pl_wade1.wav");
}

TEST(FootstepRulesTest, AStepIsOfTheKindOfWhereItIsAndWhatTheGroundIsMadeOf) {
    EXPECT_EQ(cs::stepKindOf(StepPlace::Ground, TextureMaterial::Metal), StepKind::Metal);
    EXPECT_EQ(cs::stepKindOf(StepPlace::Ground, TextureMaterial::Snow), StepKind::Snow);
    EXPECT_EQ(cs::stepKindOf(StepPlace::Ground, TextureMaterial::Slosh), StepKind::Slosh);
    for (const TextureMaterial material :
         {TextureMaterial::Wood, TextureMaterial::Computer, TextureMaterial::Grass, TextureMaterial::Glass,
          TextureMaterial::Flesh}) {
        EXPECT_EQ(cs::stepKindOf(StepPlace::Ground, material), StepKind::Concrete) << nx::enumName(material);
    }
    EXPECT_EQ(cs::stepKindOf(StepPlace::Ladder, TextureMaterial::Metal), StepKind::Ladder);
    EXPECT_EQ(cs::stepKindOf(StepPlace::Wade, TextureMaterial::Metal), StepKind::Wade);
    EXPECT_EQ(cs::stepKindOf(StepPlace::Slosh, TextureMaterial::Metal), StepKind::Slosh);
}

TEST(StepVoiceTest, EachFootPlaysItsSoundAtTheStepsVolume) {
    const FootstepRules rules = repositoryRules();
    cs::StepVoice voice{rules};
    nx::Random random{0, "test"};
    const nx::GameTime at = nx::GameTime::atTick(7);
    const auto left = voice.soundOf(Footstep{.isLeftFoot = true}, TextureMaterial::Vent, at, random);
    ASSERT_TRUE(left.has_value());
    EXPECT_EQ(left->sound, &rules.soundsOf(StepKind::Vent)->leftFoot);
    EXPECT_EQ(left->at, at);
    EXPECT_FLOAT_EQ(left->volume, 0.7F);
    const auto quiet = voice.soundOf(Footstep{.share = 0.35F}, TextureMaterial::Concrete, at, random);
    ASSERT_TRUE(quiet.has_value());
    EXPECT_EQ(quiet->sound, &rules.soundsOf(StepKind::Concrete)->rightFoot);
    EXPECT_FLOAT_EQ(quiet->volume, 0.5F * 0.35F);
    const auto landing = voice.soundOf(Footstep{.volume = 0.85F}, TextureMaterial::Dirt, at, random);
    ASSERT_TRUE(landing.has_value());
    EXPECT_FLOAT_EQ(landing->volume, 0.85F);
}

TEST(StepVoiceTest, OneWadingStepInFourIsSilentAndOneTileStepInFiveRare) {
    const FootstepRules rules = repositoryRules();
    cs::StepVoice voice{rules};
    nx::Random random{0, "test"};
    std::vector<bool> heard;
    for (int step = 0; step < 9; ++step) {
        heard.push_back(
            voice.soundOf(Footstep{.place = StepPlace::Wade}, TextureMaterial::Concrete, {}, random)
                .has_value());
    }
    EXPECT_EQ(heard, (std::vector<bool>{false, true, true, true, false, true, true, true, false}));

    const cs::GameSound* const rare = &rules.soundsOf(StepKind::Tile)->rare->sound;
    int rareSteps = 0;
    constexpr int kSteps = 1000;
    for (int step = 0; step < kSteps; ++step) {
        const auto made = voice.soundOf(Footstep{}, TextureMaterial::Tile, {}, random);
        ASSERT_TRUE(made.has_value());
        rareSteps += made->sound == rare ? 1 : 0;
    }
    EXPECT_GT(rareSteps, kSteps / 5 - 50);
    EXPECT_LT(rareSteps, kSteps / 5 + 50);
}

} // namespace
