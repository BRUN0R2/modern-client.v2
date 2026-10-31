// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// A user command each this long, the classic rate of 100 a second (docs/07, section 2.2), a tick of
/// the game's clock with each; a frame catches up on this much time at most, so that a stalled one
/// does not run away.
constexpr nx::Milliseconds kCommandInterval = nx::tickLengthOf(nx::kDefaultTickRate);
constexpr nx::Seconds kMaxCatchUp{0.25F};

[[nodiscard]] auto eyeOf(const MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

} // namespace

ClientGame::ClientGame(const MapModels& models, const PlayerModels& players, PlayerSnapshot first)
    : m_models{&models}, m_players{&players}, m_last{std::move(first)}, m_previousEye{eyeOf(m_last.player)},
      m_turnedView{m_last.turnedView} {
    lightStyles.setPatterns(m_last.lightStyles.patterns);
}

auto ClientGame::ticksDue(nx::Seconds elapsed, CatchUp catchUp) -> std::uint32_t {
    m_pending += elapsed;
    if (catchUp == CatchUp::Limited) {
        m_pending = std::min(m_pending, kMaxCatchUp);
    }
    std::uint32_t due = 0;
    while (m_pending >= kCommandInterval) {
        m_pending -= kCommandInterval;
        ++due;
    }
    return due;
}

auto ClientGame::numbered(UserCommand command) -> UserCommand {
    m_lastCommand = CommandNumber{m_lastCommand.value + 1};
    command.number = m_lastCommand;
    return command;
}

auto ClientGame::nextCommand(PlayerInput& input, nx::EulerAngles view) -> UserCommand {
    return numbered(input.nextCommand(view, kCommandInterval, m_last.player.maxSpeed));
}

auto ClientGame::standingCommand(nx::EulerAngles view) -> UserCommand {
    return numbered(UserCommand{.duration = kCommandInterval, .viewAngles = view});
}

void ClientGame::receive(PlayerSnapshot snapshot) {
    m_previousEye = snapshot.wasPlaced ? eyeOf(snapshot.player) : eyeOf(m_last.player);
    if (snapshot.turnedView) {
        m_turnedView = snapshot.turnedView;
    }
    std::ranges::move(snapshot.sounds, std::back_inserter(m_sounds));
    std::ranges::move(snapshot.impacts, std::back_inserter(m_impacts));
    std::ranges::move(snapshot.bloodMarks, std::back_inserter(bloodMarks));
    std::ranges::move(snapshot.bloodSprays, std::back_inserter(bloodSprays));
    std::ranges::move(snapshot.bloodStreams, std::back_inserter(bloodStreams));
    std::ranges::move(snapshot.streakSplashes, std::back_inserter(streakSplashes));
    if (snapshot.lightStyles != m_last.lightStyles) {
        lightStyles.setPatterns(snapshot.lightStyles.patterns);
    }
    previousPlayerOrigins.clear();
    previousEntityOrigins.clear();
    for (const EntityState& entity : m_last.entities) {
        previousEntityOrigins.emplace(entity.entity, entity.origin);
    }
    for (const PlayerEntityState& player : m_last.players) {
        if (player.player.value >= previousPlayerOrigins.size()) {
            previousPlayerOrigins.resize(std::size_t{player.player.value} + 1);
        }
        previousPlayerOrigins[player.player.value] = player.origin;
    }
    m_last = std::move(snapshot);
}

auto ClientGame::eye() const noexcept -> nx::Vec3 {
    return nx::lerp(m_previousEye, eyeOf(m_last.player), m_pending / kCommandInterval);
}

auto ClientGame::shownTime() const -> nx::GameTime {
    return m_last.time.after(m_pending - kCommandInterval, m_last.rate);
}

auto ClientGame::viewModel() const -> std::optional<ViewModelPose> {
    const std::optional<ViewAnimation>& animation = m_last.inventory.animation();
    if (!animation) {
        return std::nullopt;
    }
    return ViewModelPose{.weapon = animation->weapon,
                         .sequence = animation->sequence,
                         .start = animation->start,
                         .intoIt = std::max(shownTime().since(animation->start, m_last.rate), nx::Seconds{})};
}

auto ClientGame::hearing() const -> Hearing {
    return Hearing{.from = shownTime(), .rate = m_last.rate};
}

auto ClientGame::roundTimeShown() const -> std::uint32_t {
    return m_last.roundTimer.shownAt(shownTime(), m_last.rate);
}

void ClientGame::writePlacements(std::vector<nx::render::ModelPlacement>& placements) const {
    placements.resize(m_last.brushEntities.size());
    std::ranges::transform(m_last.brushEntities, placements.begin(), [](const BrushEntityState& entity) {
        return nx::render::ModelPlacement{.model = entity.model,
                                          .origin = entity.origin,
                                          .angles = entity.angles,
                                          .waveHeight = entity.waveHeight,
                                          .showsAlternateTextures = entity.showsAlternateTextures};
    });
}

auto ClientGame::lightStylesAt(nx::Seconds time) const noexcept -> nx::render::LightStyleValues {
    return lightStyles.valuesAt(time);
}

void ClientGame::updatePlayers(nx::Seconds frameTime, nx::Seconds corpseStay,
                               std::vector<ShownPlayer>& players) {
    const nx::GameTime shown = shownTime();
    const float between = m_pending / kCommandInterval;
    players.clear();
    players.reserve(m_last.players.size());
    for (const PlayerEntityState& state : m_last.players) {
        const nx::assets::MdlView* const model = m_players->characterModel(state.character);
        if (model == nullptr) {
            continue; // one whose model the client could not read, which it listed
        }
        const std::uint8_t amount = corpseAmountOf(
            playerCorpses.deadFor(state.player.value, state.isDead, shown, m_last.rate), corpseStay);
        if (amount == 0) {
            continue; // a corpse gone
        }
        const std::optional<nx::Vec3> before = state.player.value < previousPlayerOrigins.size()
                                                   ? previousPlayerOrigins[state.player.value]
                                                   : std::nullopt;
        const nx::Vec3 origin = before ? nx::lerp(*before, state.origin, between) : state.origin;
        if (state.player.value >= m_gaits.size()) {
            m_gaits.resize(state.player.value + 1);
        }
        std::optional<Gait>& gait = m_gaits[state.player.value];
        if (!gait) {
            gait = Gait{.yaw = state.angles.yaw.value(), .previousOrigin = origin};
        }
        processGait(*gait, *model, state.gaitSequence, origin, state.angles, frameTime, state.framerate);
        const std::span<const nx::assets::StudioSequence> sequences = model->sequences();
        const nx::assets::SequenceIndex sequence =
            state.sequence.value < sequences.size() ? state.sequence : nx::assets::SequenceIndex{};
        players.push_back(ShownPlayer{
            .character = state.character,
            .weapon = state.weapon,
            .origin = origin,
            .angles = nx::EulerAngles{.yaw = nx::Degrees{gait->yaw}, .roll = state.angles.roll},
            .sequence = sequence,
            .frame = estimatedFrameOf(sequences[sequence.value], state.frame,
                                      shown.since(state.advancedAt, m_last.rate), state.framerate),
            .gaitSequence = state.gaitSequence,
            .gaitFrame = SequenceFrame{gait->frame},
            .yawBlend = gait->yawBlend,
            .pitchBlend = static_cast<float>(gait->pitchBlend),
            .amount = amount});
    }
    playerCorpses.forgetUnasked();
}

void ClientGame::writeHostages(std::vector<ShownHostage>& hostages) const {
    const nx::GameTime shown = shownTime();
    hostages.clear();
    hostages.reserve(m_last.hostages.size());
    for (const HostageState& state : m_last.hostages) {
        const nx::assets::MdlView* const model = m_models->hostageModel(state.spawn);
        if (model == nullptr || state.sequence.value >= model->sequences().size()) {
            continue; // one whose model the client could not read, which it listed
        }
        hostages.push_back(
            shownHostageOf(state, model->sequences()[state.sequence.value], shown, m_last.rate));
    }
}

void ClientGame::writeEntities(const EntityCatalog& catalog, nx::Seconds corpseStay,
                               std::vector<ShownEntity>& entities) {
    const nx::GameTime shown = shownTime();
    entities.clear();
    entities.reserve(m_last.entities.size());
    for (const EntityState& state : m_last.entities) {
        const bool isDead = state.health <= nx::HitPoints{};
        const std::uint8_t amount =
            corpseAmountOf(entityCorpses.deadFor(state.entity, isDead, shown, m_last.rate), corpseStay);
        if (amount == 0) {
            continue; // a corpse gone
        }
        const auto before = previousEntityOrigins.find(state.entity);
        const nx::Vec3 origin = before == previousEntityOrigins.end()
                                    ? state.origin
                                    : nx::lerp(before->second, state.origin, m_pending / kCommandInterval);
        const EntityType& type = catalog.type(state.type);
        const auto& sequence =
            catalog.model(type.model).files->view->sequences()[state.animation.sequence.value];
        entities.push_back({.type = state.type,
                            .origin = origin,
                            .angles = state.angles,
                            .sequence = state.animation.sequence,
                            .frame = estimatedFrameOf(sequence, state.animation.frame,
                                                      shown.since(state.animation.advancedAt, m_last.rate),
                                                      state.animation.framerate),
                            .amount = amount});
    }
    entityCorpses.forgetUnasked();
}

} // namespace cs
