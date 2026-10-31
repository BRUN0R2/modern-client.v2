// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client's frame extraction, player poses, light selection and grouped animation reads, with
// synthetic content only. File creation and model decoding stay outside the frame measurements.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.render;
import cs.shared;
import cs.client;

#include "playerModel.hpp"
#include "triangleModel.hpp"

namespace {

constexpr std::int64_t kUsualPlayers = 32;
constexpr std::int64_t kManyPlayers = 128;
constexpr std::int64_t kMostPlayers = 1024;
constexpr nx::Seconds kFrameTime{0.01F};
constexpr std::size_t kSequenceRecordBytes = 176;
constexpr std::size_t kSequenceCountOffset = 164;
constexpr std::size_t kEntityLumpOffset = 4;
constexpr std::size_t kGroupFileBytes = 1024UZ * 1024UZ;
constexpr std::int64_t kManySequences = 64;
constexpr std::int64_t kManyLights = 4096;
constexpr float kLightReach = 10000.0F;
constexpr float kAimYaw = 191.25F;
constexpr float kAimPitch = 63.75F;
constexpr std::string_view kCharacterPath = "models/player/benchmark/benchmark.mdl";
constexpr std::string_view kCharacterTexturesPath = "models/player/benchmark/benchmarkT.mdl";
constexpr std::string_view kViewModelPath = "models/triangle.mdl";
constexpr std::string_view kGroupPath = "models/triangle01.mdl";

/// Files used only by this benchmark invocation, removed with it.
class ClientFiles {
public:
    explicit ClientFiles(std::int64_t sequenceCount = kManySequences)
        : folder{std::filesystem::temp_directory_path() /
                 std::format("nxBenchClient-{:08x}{:08x}", std::random_device{}(), std::random_device{}())} {
        const nx::test::Bytes player = nx::test::playerModelOf(nx::test::playerSequences());
        write(kCharacterPath, player);
        nx::test::TriangleModel model;
        write(kCharacterTexturesPath, model.bytes);
        const auto wave =
            std::span{model.bytes}.subspan(model.sequences + kSequenceRecordBytes, kSequenceRecordBytes);
        nx::test::Bytes records;
        for (std::int64_t sequence = 0; sequence < sequenceCount; ++sequence) {
            records.insert(records.end(), wave.begin(), wave.end());
        }
        std::ignore = model.table(kSequenceCountOffset, nx::narrow<std::int32_t>(sequenceCount), records);
        write(kViewModelPath, model.bytes);
        model.groupFile.resize(kGroupFileBytes);
        write(kGroupPath, model.groupFile);
    }

    ClientFiles(const ClientFiles&) = delete;
    auto operator=(const ClientFiles&) -> ClientFiles& = delete;
    ~ClientFiles() {
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    }

    [[nodiscard]] auto mount() const -> nx::assets::Vfs {
        auto mounted = nx::assets::Vfs::create(std::array{folder});
        nx::invariant(mounted.has_value(), "clientBench: the synthetic content mounts");
        return std::move(*mounted);
    }

private:
    void write(std::string_view relative, std::span<const std::byte> bytes) const {
        const std::filesystem::path path = folder / relative;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        nx::invariant(!error, "clientBench: the fixture's directories can be made");
        const auto written = nx::platform::writeFile(path, bytes);
        nx::invariant(written.has_value(), "clientBench: the fixture's files can be written");
    }

    std::filesystem::path folder;
};

[[nodiscard]] auto emptyWeapons() -> cs::WeaponCatalog {
    auto weapons = cs::WeaponCatalog::make({}, {});
    nx::invariant(weapons.has_value(), "clientBench: an empty weapon catalog is valid");
    return std::move(*weapons);
}

[[nodiscard]] auto boxMapBytes(std::string_view entities = {}) -> std::vector<std::byte> {
    auto read =
        nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp");
    nx::invariant(read.has_value(), "clientBench: the synthetic map can be read");
    std::vector<std::byte> bytes = std::move(*read);
    if (!entities.empty()) {
        const auto header = std::bit_cast<std::array<std::byte, sizeof(std::int32_t) * 2>>(std::array{
            nx::narrow<std::int32_t>(bytes.size()), nx::narrow<std::int32_t>(entities.size() + 1)});
        std::ranges::copy(header, bytes.begin() + kEntityLumpOffset);
        std::ranges::copy(std::as_bytes(std::span{entities}), std::back_inserter(bytes));
        bytes.push_back(std::byte{});
    }
    return bytes;
}

[[nodiscard]] auto snapshotOf(const cs::WeaponCatalog& weapons, std::size_t count) -> cs::PlayerSnapshot {
    cs::PlayerSnapshot snapshot{.inventory = cs::Inventory{weapons}};
    const nx::assets::SequenceIndex aim{nx::test::playerSequenceIndex("ref_aim_knife")};
    const nx::assets::SequenceIndex gait{nx::test::playerSequenceIndex("idle1")};
    for (std::size_t index = 0; index < count; ++index) {
        snapshot.players.push_back(
            cs::PlayerEntityState{.player = cs::PlayerIndex{nx::narrow<std::uint32_t>(index)},
                                  .origin = {.x = static_cast<float>(index)},
                                  .sequence = aim,
                                  .gaitSequence = gait});
        snapshot.brushEntities.push_back(
            cs::BrushEntityState{.model = nx::assets::ModelIndex{nx::narrow<std::uint32_t>(index + 1)},
                                 .origin = {.x = static_cast<float>(index)}});
    }
    return snapshot;
}

void extractPlayers(benchmark::State& state) {
    ClientFiles files;
    const nx::assets::Vfs vfs = files.mount();
    const cs::WeaponCatalog weapons = emptyWeapons();
    const cs::Characters characters{.characters = {{.name = "benchmark"}}};
    const cs::PlayerModels players = cs::PlayerModels::read(vfs, characters, weapons);
    const std::vector<std::byte> bytes = boxMapBytes();
    auto map = nx::assets::BspView::parse(bytes);
    nx::invariant(map.has_value(), "clientBench: the synthetic map is valid");
    const cs::MapModels models = cs::MapModels::read(vfs, *map);
    const std::size_t count = nx::narrow<std::size_t>(state.range(0));
    cs::PlayerSnapshot first = snapshotOf(weapons, count);
    cs::ClientGame client{models, players, first};
    client.receive(std::move(first));
    std::vector<cs::ShownPlayer> shown;
    for ([[maybe_unused]] auto iteration : state) {
        client.updatePlayers(kFrameTime, cs::kLongestCorpseStay, shown);
        benchmark::DoNotOptimize(shown);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(extractPlayers)->Arg(kUsualPlayers)->Arg(kManyPlayers)->Arg(kMostPlayers);

void extractPlacements(benchmark::State& state) {
    ClientFiles files;
    const nx::assets::Vfs vfs = files.mount();
    const cs::WeaponCatalog weapons = emptyWeapons();
    const cs::PlayerModels players = cs::PlayerModels::read(vfs, {}, weapons);
    const std::vector<std::byte> bytes = boxMapBytes();
    auto map = nx::assets::BspView::parse(bytes);
    nx::invariant(map.has_value(), "clientBench: the synthetic map is valid");
    const cs::MapModels models = cs::MapModels::read(vfs, *map);
    const cs::ClientGame client{models, players,
                                snapshotOf(weapons, nx::narrow<std::size_t>(state.range(0)))};
    std::vector<nx::render::ModelPlacement> placements;
    for ([[maybe_unused]] auto iteration : state) {
        client.writePlacements(placements);
        benchmark::DoNotOptimize(placements);
    }
}
BENCHMARK(extractPlacements)->Arg(kUsualPlayers)->Arg(kMostPlayers);

void lookupHostageModels(benchmark::State& state) {
    ClientFiles files;
    const nx::assets::Vfs vfs = files.mount();
    std::string entities = R"({ "classname" "worldspawn" })";
    for (std::int64_t hostage = 0; hostage < state.range(0); ++hostage) {
        entities += std::format(R"({{ "classname" "hostage_entity" "model" "{}" }})", kViewModelPath);
    }
    const std::vector<std::byte> bytes = boxMapBytes(entities);
    auto map = nx::assets::BspView::parse(bytes);
    nx::invariant(map.has_value(), "clientBench: the synthetic hostage map is valid");
    const cs::MapModels models = cs::MapModels::read(vfs, *map);
    const std::uint32_t count = nx::narrow<std::uint32_t>(state.range(0));
    for ([[maybe_unused]] auto iteration : state) {
        for (std::uint32_t spawn = 0; spawn < count; ++spawn) {
            const nx::assets::MdlView* model = models.hostageModel(spawn);
            benchmark::DoNotOptimize(model);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(lookupHostageModels)->Arg(kUsualPlayers)->Arg(kMostPlayers);

void posePlayerAim(benchmark::State& state) {
    const nx::test::Bytes bytes = nx::test::playerModelOf(nx::test::playerSequences());
    auto model = nx::assets::MdlView::parse(bytes);
    nx::invariant(model.has_value(), "clientBench: the synthetic player's model is valid");
    std::vector<nx::assets::AnimationFrames> animations;
    for (std::uint32_t sequence = 0; sequence < model->sequences().size(); ++sequence) {
        auto decoded = model->decodeAnimation(nx::assets::SequenceIndex{sequence}, bytes);
        nx::invariant(decoded.has_value(), "clientBench: the synthetic player's animation is valid");
        animations.push_back(std::move(*decoded));
    }
    const nx::assets::SequenceIndex aim{nx::test::playerSequenceIndex("ref_aim_knife")};
    const cs::PlayerPoseQuery query{.yawBlend = kAimYaw, .pitchBlend = kAimPitch};
    for ([[maybe_unused]] auto iteration : state) {
        std::vector<nx::Mat3x4> bones = cs::posePlayerBones(*model, animations, aim, query);
        benchmark::DoNotOptimize(bones);
    }
}
BENCHMARK(posePlayerAim);

void selectNearestLights(benchmark::State& state) {
    std::vector<nx::render::StudioLight> lights;
    for (std::int64_t index = state.range(0); index > 0; --index) {
        lights.push_back(
            nx::render::StudioLight{.position = {.x = static_cast<float>(index)}, .reach = kLightReach});
    }
    for ([[maybe_unused]] auto iteration : state) {
        std::vector<nx::render::StudioLight> nearest =
            cs::nearestLights(lights, {}, nx::narrow<std::size_t>(state.range(1)));
        benchmark::DoNotOptimize(nearest);
    }
}
BENCHMARK(selectNearestLights)->Args({kManyLights, 0})->Args({kManyLights, nx::render::kMostStudioLights});

void readGroupedViewModel(benchmark::State& state) {
    ClientFiles files{state.range(0)};
    const nx::assets::Vfs vfs = files.mount();
    const cs::Weapon weapon{.models = {.view = std::string{kViewModelPath}}, .view = {.drawSequences = {0}}};
    for ([[maybe_unused]] auto iteration : state) {
        auto model = cs::ViewModel::read(vfs, weapon);
        nx::invariant(model.has_value(), "clientBench: the synthetic view model is valid");
        benchmark::DoNotOptimize(model);
    }
}
BENCHMARK(readGroupedViewModel)->Arg(1)->Arg(kManySequences)->Unit(benchmark::kMicrosecond);

} // namespace
