// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;

namespace cs {

namespace {

using nx::assets::MdlView;
using nx::assets::Vfs;

/// The sequences of `view`, `firing` and `reload` of a weapon, added to `played`.
void addPlayed(std::vector<std::uint32_t>& played, const WeaponView& view,
               const std::optional<WeaponFiring>& firing, const std::optional<WeaponReload>& reload) {
    std::ranges::copy(view.drawSequences, std::back_inserter(played));
    std::ranges::copy(view.idles | std::views::transform(&WeaponIdle::sequence), std::back_inserter(played));
    if (firing) {
        std::ranges::copy(firing->shootSequences, std::back_inserter(played));
        if (firing->lastShotSequence) {
            played.push_back(*firing->lastShotSequence);
        }
    }
    if (reload) {
        std::ranges::copy(reload->sequences, std::back_inserter(played));
    }
}

/// NoSequence when `weapon` plays a sequence `model` lacks: drawn, idling, firing, reloaded,
/// switched, with its alternate on or off, or slashing and stabbing.
[[nodiscard]] auto checkSequences(const MdlView& model, const Weapon& weapon)
    -> nx::Result<void, ViewModelError> {
    std::vector<std::uint32_t> played;
    addPlayed(played, weapon.view, weapon.firing, weapon.reload);
    if (const std::optional<WeaponAlternate>& alternate = weapon.alternate) {
        addPlayed(played, alternate->view.value_or(weapon.view),
                  alternate->firing ? alternate->firing : weapon.firing,
                  alternate->reload ? alternate->reload : weapon.reload);
        for (const std::optional<std::uint32_t>& switched : {alternate->onSequence, alternate->offSequence}) {
            if (switched) {
                played.push_back(*switched);
            }
        }
    }
    if (const std::optional<WeaponMelee>& melee = weapon.melee) {
        for (const MeleeAttack* const attack : {&melee->slash, &melee->stab}) {
            std::ranges::copy(attack->missSequences, std::back_inserter(played));
            std::ranges::copy(attack->hitSequences, std::back_inserter(played));
        }
    }
    for (const std::uint32_t sequence : played) {
        if (sequence >= model.sequences().size()) {
            return nx::fail(ViewModelError::NoSequence, std::format("{} of the {} of {:?}", sequence,
                                                                    model.sequences().size(), model.name()));
        }
    }
    return {};
}

} // namespace

ViewModel::ViewModel(std::unique_ptr<ModelFiles> files, std::vector<nx::assets::AnimationFrames> animations,
                     bool isMirrored) noexcept
    : m_files{std::move(files)}, m_animations{std::move(animations)}, m_isMirrored{isMirrored} {}

auto ViewModel::read(const Vfs& vfs, const Weapon& weapon) -> nx::Result<ViewModel, ViewModelError> {
    auto files = readModelFiles(vfs, weapon.models.view);
    if (!files) {
        return nx::fail(ViewModelError::FilesFailed, std::move(files.error()));
    }
    const MdlView& model = *(*files)->view;
    if (auto checked = checkSequences(model, weapon); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    std::vector<nx::assets::AnimationFrames> animations;
    for (std::uint32_t sequence = 0; sequence < model.sequences().size(); ++sequence) {
        auto frames = animationOf(vfs, **files, nx::assets::SequenceIndex{sequence});
        if (!frames) {
            return nx::fail(ViewModelError::FilesFailed, std::move(frames.error()));
        }
        animations.push_back(std::move(*frames));
    }
    return ViewModel{std::move(*files), std::move(animations), weapon.models.viewHand != kPlayerHand};
}

auto ViewModel::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<nx::render::StudioModel, nx::render::StudioError> {
    return renderer.load(commands, {.model = *m_files->view, .textures = m_files->texturesView()});
}

auto ViewModel::pose(const nx::render::Camera& camera, std::uint32_t sequence, nx::Seconds intoIt) const
    -> std::vector<nx::Mat3x4> {
    const MdlView& model = *m_files->view;
    nx::expects(sequence < m_animations.size(), "ViewModel::pose: a sequence of the model");
    const nx::assets::StudioSequence& played = model.sequences()[sequence];
    // The view's own model turns as the view does: the client gives it the view's angles with the pitch
    // negated, which the engine's studio transform negates back.
    nx::Mat3x4 placement = nx::affine(nx::rotationMatrix(camera.angles), camera.position);
    if (m_isMirrored) {
        // Its left, the placement's second column, turned to its right.
        for (nx::Vec4& row : placement.rows) {
            row.y = -row.y;
        }
    }
    return cs::poseBones(cs::skeletonOf(model), played, m_animations[sequence],
                         {.frame = cs::sequenceFrameAt(played, intoIt, 1.0F), .placement = placement});
}

} // namespace cs
