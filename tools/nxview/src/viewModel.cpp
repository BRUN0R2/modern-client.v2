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
using nx::assets::VfsPath;

constexpr std::string_view kKnifePath = "models/v_knife.mdl";
constexpr std::string_view kDrawSequence = "draw";
constexpr std::string_view kIdleSequence = "idle";
/// When the knife first idles after it came out, and how long it waits to idle again.
constexpr nx::Seconds kFirstIdle{1.5F};
constexpr nx::Seconds kIdleInterval{20.0F};

/// The bytes of the file at `path`, which `what` names. Errors: ReadFailed.
[[nodiscard]] auto readFile(const Vfs& vfs, nx::Result<VfsPath, nx::assets::VfsError> path,
                            std::string_view what) -> nx::Result<std::vector<std::byte>, ViewModelError> {
    auto bytes = path.and_then([&vfs](const VfsPath& found) { return vfs.readAll(found); });
    if (!bytes) {
        return nx::fail(ViewModelError::ReadFailed, std::string{what}, std::move(bytes.error()));
    }
    return std::move(*bytes);
}

/// The sequence of `model` labeled `label`, and its decoded frames, from `modelBytes` or its sequence
/// group's file. Errors: NoSequence, ReadFailed, Invalid.
[[nodiscard]] auto animationOf(const Vfs& vfs, const MdlView& model, std::span<const std::byte> modelBytes,
                               std::string_view label)
    -> nx::Result<std::pair<nx::assets::SequenceIndex, nx::assets::AnimationFrames>, ViewModelError> {
    const auto found = std::ranges::find(model.sequences(), label, &nx::assets::StudioSequence::label);
    if (found == model.sequences().end()) {
        return nx::fail(ViewModelError::NoSequence, std::format("{:?} in {:?}", label, model.name()));
    }
    const nx::assets::SequenceIndex index{nx::narrow<std::uint32_t>(found - model.sequences().begin())};
    std::vector<std::byte> groupBytes;
    if (const std::uint32_t group = found->group.value; group != 0) {
        auto read = readFile(vfs, VfsPath::parseGoldsrc(model.sequenceGroups()[group].fileName),
                             std::format("the sequence group file of {:?}", label));
        if (!read) {
            return std::unexpected{std::move(read.error())};
        }
        groupBytes = std::move(*read);
    }
    auto frames = model.decodeAnimation(index, groupBytes.empty() ? modelBytes : std::span{groupBytes});
    if (!frames) {
        return nx::fail(ViewModelError::Invalid, std::format("the animation of {:?}", label),
                        std::move(frames.error()));
    }
    return std::pair{index, std::move(*frames)};
}

} // namespace

ViewModel::ViewModel(std::unique_ptr<Files> files, MdlView model, std::optional<MdlView> textures,
                     Animation draw, Animation idle) noexcept
    : m_files{std::move(files)}, m_model{std::move(model)}, m_textures{std::move(textures)},
      m_draw{std::move(draw)}, m_idle{std::move(idle)} {}

auto ViewModel::read(const Vfs& vfs) -> nx::Result<ViewModel, ViewModelError> {
    auto files = std::make_unique<Files>();
    const auto path = VfsPath::parse(kKnifePath);
    auto modelBytes = readFile(vfs, path, kKnifePath);
    if (!modelBytes) {
        return std::unexpected{std::move(modelBytes.error())};
    }
    files->model = std::move(*modelBytes);
    auto model = MdlView::parse(files->model);
    if (!model) {
        return nx::fail(ViewModelError::Invalid, std::string{kKnifePath}, std::move(model.error()));
    }
    std::optional<MdlView> textures;
    if (model->textures().empty()) {
        auto texturesBytes = readFile(
            vfs, path.and_then([](const VfsPath& found) { return nx::assets::texturesFilePath(found); }),
            "its textures file");
        if (!texturesBytes) {
            return std::unexpected{std::move(texturesBytes.error())};
        }
        files->textures = std::move(*texturesBytes);
        auto parsed = MdlView::parse(files->textures);
        if (!parsed) {
            return nx::fail(ViewModelError::Invalid, "its textures file", std::move(parsed.error()));
        }
        textures = std::move(*parsed);
    }
    auto draw = animationOf(vfs, *model, files->model, kDrawSequence);
    auto idle = animationOf(vfs, *model, files->model, kIdleSequence);
    if (!draw || !idle) {
        return std::unexpected{std::move(!draw ? draw.error() : idle.error())};
    }
    return ViewModel{std::move(files), std::move(*model), std::move(textures),
                     Animation{.sequence = draw->first, .frames = std::move(draw->second)},
                     Animation{.sequence = idle->first, .frames = std::move(idle->second)}};
}

auto ViewModel::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<nx::render::StudioModel, nx::render::StudioError> {
    return renderer.load(commands, {.model = m_model, .textures = m_textures ? *m_textures : m_model});
}

auto ViewModel::pose(const nx::render::Camera& camera, nx::Seconds sinceDeploy) const
    -> std::vector<nx::Mat3x4> {
    const bool isIdling = sinceDeploy >= kFirstIdle;
    const Animation& playing = isIdling ? m_idle : m_draw;
    const nx::Seconds intoIt =
        isIdling ? nx::Seconds{std::fmod((sinceDeploy - kFirstIdle).count(), kIdleInterval.count())}
                 : sinceDeploy;
    const nx::assets::StudioSequence& sequence = m_model.sequences()[playing.sequence.value];
    // The view's own model turns as the view does: the client gives it the view's angles with the pitch
    // negated, which the engine's studio transform negates back.
    return cs::poseBones(cs::skeletonOf(m_model), sequence, playing.frames,
                         {.frame = cs::sequenceFrameAt(sequence, intoIt),
                          .placement = nx::affine(nx::rotationMatrix(camera.angles), camera.position)});
}

} // namespace nxview
