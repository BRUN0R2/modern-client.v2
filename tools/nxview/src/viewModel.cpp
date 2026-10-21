// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module tool.nxview;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;

namespace nxview {

namespace {

using nx::assets::MdlView;
using nx::assets::Vfs;

constexpr std::string_view kKnifePath = "models/v_knife.mdl";
constexpr std::string_view kDrawSequence = "draw";
constexpr std::string_view kIdleSequence = "idle";
/// When the knife first idles after it came out, and how long it waits to idle again.
constexpr nx::Seconds kFirstIdle{1.5F};
constexpr nx::Seconds kIdleInterval{20.0F};

/// The index of the sequence of `model` labeled `label`. Errors: NoSequence.
[[nodiscard]] auto sequenceLabeled(const MdlView& model, std::string_view label)
    -> nx::Result<nx::assets::SequenceIndex, ViewModelError> {
    const auto found = std::ranges::find(model.sequences(), label, &nx::assets::StudioSequence::label);
    if (found == model.sequences().end()) {
        return nx::fail(ViewModelError::NoSequence, std::format("{:?} in {:?}", label, model.name()));
    }
    return nx::assets::SequenceIndex{nx::narrow<std::uint32_t>(found - model.sequences().begin())};
}

} // namespace

ViewModel::ViewModel(std::unique_ptr<ModelFiles> files, Animation draw, Animation idle) noexcept
    : m_files{std::move(files)}, m_draw{std::move(draw)}, m_idle{std::move(idle)} {}

auto ViewModel::read(const Vfs& vfs) -> nx::Result<ViewModel, ViewModelError> {
    auto files = readModelFiles(vfs, kKnifePath);
    if (!files) {
        return nx::fail(ViewModelError::FilesFailed, std::move(files.error()));
    }
    const auto animationLabeled = [&vfs,
                                   &files](std::string_view label) -> nx::Result<Animation, ViewModelError> {
        auto sequence = sequenceLabeled(*(*files)->view, label);
        if (!sequence) {
            return std::unexpected{std::move(sequence.error())};
        }
        auto frames = animationOf(vfs, **files, *sequence);
        if (!frames) {
            return nx::fail(ViewModelError::FilesFailed, std::move(frames.error()));
        }
        return Animation{.sequence = *sequence, .frames = std::move(*frames)};
    };
    auto draw = animationLabeled(kDrawSequence);
    auto idle = animationLabeled(kIdleSequence);
    if (!draw || !idle) {
        return std::unexpected{std::move(!draw ? draw.error() : idle.error())};
    }
    return ViewModel{std::move(*files), std::move(*draw), std::move(*idle)};
}

auto ViewModel::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<nx::render::StudioModel, nx::render::StudioError> {
    return renderer.load(commands, {.model = *m_files->view, .textures = m_files->texturesView()});
}

auto ViewModel::pose(const nx::render::Camera& camera, nx::Seconds sinceDeploy) const
    -> std::vector<nx::Mat3x4> {
    const bool isIdling = sinceDeploy >= kFirstIdle;
    const Animation& playing = isIdling ? m_idle : m_draw;
    const nx::Seconds intoIt =
        isIdling ? nx::Seconds{std::fmod((sinceDeploy - kFirstIdle).count(), kIdleInterval.count())}
                 : sinceDeploy;
    const MdlView& model = *m_files->view;
    const nx::assets::StudioSequence& sequence = model.sequences()[playing.sequence.value];
    // The view's own model turns as the view does: the client gives it the view's angles with the pitch
    // negated, which the engine's studio transform negates back.
    return cs::poseBones(cs::skeletonOf(model), sequence, playing.frames,
                         {.frame = cs::sequenceFrameAt(sequence, intoIt, 1.0F),
                          .placement = nx::affine(nx::rotationMatrix(camera.angles), camera.position)});
}

} // namespace nxview
