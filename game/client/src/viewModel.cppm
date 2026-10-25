// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A weapon in its holder's view (docs/05-rendering.md, section 9): the view model its data names
/// (cs::Weapon), each of its sequences decoded, posed where the player looks from, as the view's own
/// model is (cs::poseBones), in the sequence the player's weapons play (cs::Inventory); mirrored when
/// it was modeled in the other hand than the player's, the right one (cl_righthand's default), as
/// the original draws Counter-Strike's guns (Xash3D's R_AllowFlipViewModel, GPL, consulted: docs/20
/// section 3; the original's, observed: docs/05 section 9).
export module cs.client:viewModel;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :modelFiles;
import :modelMaterials;
import :muzzleFlash;

export namespace cs {

/// How far from the middle the knife's crosshair is when the player stands, before the crosshair's
/// scale (measured: docs/12-ui-hud-and-console.md, section 3.2).
inline constexpr std::uint32_t kKnifeCrosshairDistance = 7;

/// Why a weapon's view model cannot be held.
enum class ViewModelError : std::uint8_t {
    FilesFailed,  ///< A file of it cannot be read or used (cause: the model files' error).
    NoSequence,   ///< It lacks a sequence its weapon's data plays.
    NoAppearance, ///< Its static skin/body choices cannot be bound, or its alternate declares them or
                  ///< its muzzle flashes' look.
};

/// What a frame shows of the view model: the weapon's, its sequence, when it started, and how far
/// into it.
struct ViewModelPose {
    WeaponIndex weapon{};
    std::uint32_t sequence{};
    nx::GameTime start{}; ///< A sequence started again (a shot after a shot) plays its events again.
    nx::Seconds intoIt{};

    /// Whether `other` is the same play of the same sequence, further into it or not.
    [[nodiscard]] inline auto isSamePlay(const ViewModelPose& other) const noexcept -> bool {
        return weapon == other.weapon && sequence == other.sequence && start == other.start;
    }
};

/// The first number of the client's events; the game's own come before.
inline constexpr std::int32_t kFirstClientEvent = 5000;
/// The client event that plays a sound at full volume, its options the sound's name, a path under
/// `sound/` (the reloads' and the draws' sounds of the original's view models).
inline constexpr std::int32_t kSoundEvent = 5004;

/// A client event a view model's play passed, and how long into the play it comes: when it is heard
/// and seen, though a frame passes many at once.
struct PassedEvent {
    const nx::assets::StudioEvent* event{};
    nx::Seconds intoPlay{};
};

/// The client events of `sequence` its play passes from frame `from`, left out, to frame `to`,
/// included, in their order.
[[nodiscard]] auto clientEventsBetween(const nx::assets::StudioSequence& sequence, float from, float to)
    -> std::vector<const nx::assets::StudioEvent*>;

/// How long into a play of `sequence` its frame `frame` comes, the play `intoIt` into it at frame
/// `now` (cs::sequenceFrameAt): from its start, or, in a loop, from the start of the turn `now` is
/// in. A sequence of no rate stays at its first frame.
[[nodiscard]] auto intoPlayOf(const nx::assets::StudioSequence& sequence, float frame, float now,
                              nx::Seconds intoIt) -> nx::Seconds;

/// A weapon's view model, its textures, validated static appearance and animations of its sequences.
class ViewModel {
public:
    /// Reads the view model of `weapon` from `vfs`, its `*T.mdl` when its textures are not in it, and
    /// the animations of each of its sequences, from their sequence group files. Errors: FilesFailed,
    /// NoSequence (one the weapon's view plays), NoAppearance (skin, textures or body choices, or an
    /// alternate view that declares them or a muzzle flash look).
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, const Weapon& weapon)
        -> nx::Result<ViewModel, ViewModelError>;

    /// Its model on the GPU of `renderer`, its uploads recorded in `commands`. Errors: those of
    /// StudioRenderer::load.
    [[nodiscard]] auto load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<nx::render::StudioModel, nx::render::StudioError>;

    /// Its bones `intoIt` into its sequence `sequence`, which must be one of its, played from its
    /// first frame (cs::sequenceFrameAt), in `hand` of a player whose eye and view are `camera`'s:
    /// from each bone's space to the world, mirrored across the view's right when isMirroredIn it.
    [[nodiscard]] auto pose(const nx::render::Camera& camera, std::uint32_t sequence, nx::Seconds intoIt,
                            Hand hand) const -> std::vector<nx::Mat3x4>;

    /// Whether it is drawn mirrored in the player's `hand`: modeled in the other one, its weapon's
    /// `viewHand`.
    [[nodiscard]] inline auto isMirroredIn(Hand hand) const noexcept -> bool { return m_hand != hand; }

    /// Its validated static skin family, from the weapon's main view; zero when omitted.
    [[nodiscard]] inline auto skin() const noexcept -> std::uint32_t { return m_appearance.skin; }

    /// Its validated static body choices. The model owns them: the span lasts as long as it does.
    /// Omitted parts select their first choice, also while the weapon's alternate is on.
    [[nodiscard]] inline auto bodyChoices() const noexcept -> std::span<const std::uint32_t> {
        return m_appearance.bodyChoices;
    }

    /// Its sequence `sequence`, which must be one of its.
    [[nodiscard]] auto sequence(std::uint32_t sequence) const -> const nx::assets::StudioSequence&;

    /// Where its attachment `attachment` is, its bones posed as `bones` says (what pose gave); none
    /// when it has no such attachment.
    [[nodiscard]] auto attachmentAt(std::span<const nx::Mat3x4> bones, std::uint32_t attachment) const
        -> std::optional<nx::Vec3>;

    /// The names of the sounds its sound events (kSoundEvent) play, those of its sequences in their
    /// order. The model owns them: the views last as long as it does.
    [[nodiscard]] auto soundEvents() const -> std::vector<std::string_view>;

    /// The style of its muzzle flash event `event` (muzzleFlashStyleOf): its options, under its
    /// weapon's look.
    [[nodiscard]] auto muzzleFlashStyleOf(const nx::assets::StudioEvent& event) const -> MuzzleFlashStyle;
    /// The sprites its muzzle flash events show, each once, those of its sequences in their order.
    [[nodiscard]] auto muzzleFlashSprites() const -> std::vector<std::string>;

private:
    /// What the weapon's data declares of its look, which its main view alone gives.
    struct Appearance {
        std::uint32_t skin = 0;
        std::vector<std::uint32_t> bodyChoices{};
        std::optional<MuzzleFlashLook> muzzleFlash{};
    };

    ViewModel(std::unique_ptr<ModelFiles> files, std::vector<nx::assets::AnimationFrames> animations,
              std::vector<std::optional<nx::render::StudioMaterial>> materials, Hand hand,
              Appearance appearance) noexcept;

    std::unique_ptr<ModelFiles> m_files;
    std::vector<nx::assets::AnimationFrames> m_animations; ///< Of each of its sequences, in their order.
    /// Of each of its textures in the enhanced look, from its material file (cs.client:modelMaterials).
    std::vector<std::optional<nx::render::StudioMaterial>> m_materials;
    Hand m_hand; ///< The one it is modeled in.
    Appearance m_appearance;
};

/// The names of the sounds the player hears of `weapon` in hand: the files of each sound it makes
/// (soundsOf), then those the sound events of `viewModel`, its view model, play; a name as often as
/// they give it, and none of a view model when it has none. The views last as long as both do.
[[nodiscard]] auto soundFilesOf(const Weapon& weapon, const ViewModel* viewModel)
    -> std::vector<std::string_view>;

/// The client events the view model passes, frame after frame, as the original's client plays them
/// (Xash3D's R_StudioClientEvents, GPL, consulted: docs/20 section 3): those of the sequence it plays
/// from the frame it was at in the last frame, or from before its first frame when a play starts.
class ViewModelEvents {
public:
    /// The client events `viewModel`, that of the weapon of `pose`, passed since the last call's pose,
    /// each with how long into the play it comes; none without either.
    [[nodiscard]] auto passed(const ViewModel* viewModel, const std::optional<ViewModelPose>& pose)
        -> std::vector<PassedEvent>;

private:
    std::optional<ViewModelPose> m_lastPose; ///< The last call's.
};

} // namespace cs
