// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The player's knife in view (docs/05-rendering.md, section 9): models/v_knife.mdl, held as
/// Counter-Strike deploys it (ReGameDLL's CKnife::Deploy, DefaultDeploy and CKnife::WeaponIdle, GPL,
/// consulted: docs/20 section 3): its `draw` sequence as it comes out, then `idle` 1.5 seconds later
/// and again every 20 seconds, each from its first frame; posed where the player looks from, as the
/// view's own model is (cs::poseBones). Until the game's weapons come, the player holds it.
export module cs.client:viewModel;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import :modelFiles;

export namespace cs {

/// Why the knife cannot be held.
enum class ViewModelError : std::uint8_t {
    FilesFailed, ///< A file of it cannot be read or used (cause: the model files' error).
    NoSequence,  ///< It lacks a sequence the knife plays.
};

/// The knife's model, its textures and the animations of the sequences it plays.
class ViewModel {
public:
    /// Reads the knife's model from `vfs`, its `*T.mdl` when its textures are not in it, and the
    /// animations of `draw` and `idle`, from their sequence group files. Errors: FilesFailed,
    /// NoSequence.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs) -> nx::Result<ViewModel, ViewModelError>;

    /// Its model on the GPU of `renderer`, its uploads recorded in `commands`. Errors: those of
    /// StudioRenderer::load.
    [[nodiscard]] auto load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<nx::render::StudioModel, nx::render::StudioError>;

    /// Its bones `sinceDeploy` after it came out, in the hand of a player whose eye and view are
    /// `camera`'s, from each bone's space to the world.
    [[nodiscard]] auto pose(const nx::render::Camera& camera, nx::Seconds sinceDeploy) const
        -> std::vector<nx::Mat3x4>;

private:
    /// A sequence the knife plays, and its decoded frames.
    struct Animation {
        nx::assets::SequenceIndex sequence{};
        nx::assets::AnimationFrames frames{};
    };

    ViewModel(std::unique_ptr<ModelFiles> files, Animation draw, Animation idle) noexcept;

    std::unique_ptr<ModelFiles> m_files;
    Animation m_draw;
    Animation m_idle;
};

} // namespace cs
