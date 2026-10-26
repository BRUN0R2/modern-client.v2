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

ClientGame::ClientGame(const MapModels& models, PlayerSnapshot first)
    : m_models{&models}, m_last{std::move(first)}, m_previousEye{eyeOf(m_last.player)},
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
    if (snapshot.lightStyles != m_last.lightStyles) {
        lightStyles.setPatterns(snapshot.lightStyles.patterns);
    }
    m_last = std::move(snapshot);
}

auto ClientGame::lightStylesAt(nx::Seconds time) const noexcept -> nx::render::LightStyleValues {
    return lightStyles.valuesAt(time);
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

auto ClientGame::placements() const -> std::vector<nx::render::ModelPlacement> {
    return m_last.brushEntities | std::views::transform([](const BrushEntityState& entity) {
               return nx::render::ModelPlacement{.model = entity.model,
                                                 .origin = entity.origin,
                                                 .angles = entity.angles,
                                                 .waveHeight = entity.waveHeight,
                                                 .showsAlternateTextures = entity.showsAlternateTextures};
           }) |
           std::ranges::to<std::vector>();
}

auto ClientGame::hostages() const -> std::vector<ShownHostage> {
    const nx::GameTime shown = shownTime();
    std::vector<ShownHostage> hostages;
    for (const HostageState& state : m_last.hostages) {
        const nx::assets::MdlView* const model = m_models->hostageModel(state.spawn);
        if (model == nullptr || state.sequence.value >= model->sequences().size()) {
            continue; // one whose model the client could not read, which it listed
        }
        hostages.push_back(
            shownHostageOf(state, model->sequences()[state.sequence.value], shown, m_last.rate));
    }
    return hostages;
}

} // namespace cs
