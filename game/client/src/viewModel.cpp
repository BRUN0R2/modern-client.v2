// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.console;
import nx.rhi;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// A sequence played anew plays the events of its first frame too (R_StudioClientEvents).
constexpr float kBeforeTheFirstFrame = -0.01F;
/// Limit catch-up work for very short loops or a stalled render frame.
constexpr std::size_t kMaxLoopsOfEventsPerFrame = 8;
constexpr nx::log::Category kViewModelLog{"client"};

/// At a typical running speed, roughly one step per shift of the weapon from side to side.
constexpr nx::UnitsPerSecond kFullBobSpeed{240.0F};
constexpr nx::Seconds kBobResponseTime{0.12F};
constexpr float kBobCyclesPerSecond = 1.35F;
constexpr float kBobCycle = 2.0F * std::numbers::pi_v<float>;
constexpr nx::HammerUnits kSideBob{0.45F};
constexpr nx::HammerUnits kDepthBob{0.15F};
constexpr nx::HammerUnits kVerticalBob{0.55F};
constexpr float kBobStrength = 0.35F;
constexpr nx::console::CvarFlags kArchived{nx::console::CvarFlag::Archive};

using nx::assets::MdlView;
using nx::assets::Vfs;

/// NoAppearance when declared static choices cannot bind to the model or its external textures.
[[nodiscard]] auto checkAppearance(const MdlView& model, const MdlView& textures, const Weapon& weapon)
    -> nx::Result<void, ViewModelError> {
    if (weapon.alternate && weapon.alternate->view &&
        (weapon.alternate->view->skin || weapon.alternate->view->bodyChoices ||
         weapon.alternate->view->muzzleFlash)) {
        return nx::fail(
            ViewModelError::NoAppearance,
            std::format("{}: alternate.view cannot declare static skin, bodyChoices or muzzleFlash; use view",
                        weapon.models.view));
    }
    if (auto checked = model.checkTextures(textures); !checked) {
        return nx::fail(ViewModelError::NoAppearance, weapon.models.view, std::move(checked.error()));
    }
    const std::uint32_t skin = weapon.view.skin.value_or(0);
    if (skin >= textures.skins().size()) {
        return nx::fail(ViewModelError::NoAppearance,
                        std::format("{}: view.skin {} is outside {} skin families", weapon.models.view, skin,
                                    textures.skins().size()));
    }
    if (weapon.view.bodyChoices) {
        const std::vector<std::uint32_t>& choices = *weapon.view.bodyChoices;
        const std::span<const nx::assets::BodyPart> parts = model.bodyParts();
        if (choices.size() > parts.size()) {
            return nx::fail(ViewModelError::NoAppearance,
                            std::format("{}: view.bodyChoices has {} entries for {} body parts",
                                        weapon.models.view, choices.size(), parts.size()));
        }
        for (std::size_t part = 0; part < choices.size(); ++part) {
            if (choices[part] >= parts[part].models.size()) {
                return nx::fail(ViewModelError::NoAppearance,
                                std::format("{}: view.bodyChoices[{}] {} is outside {} choices of {:?}",
                                            weapon.models.view, part, choices[part],
                                            parts[part].models.size(), parts[part].name));
            }
        }
    }
    return {};
}

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

void ViewModelBob::update(nx::UnitsPerSecond groundSpeed, nx::Seconds elapsed) noexcept {
    const float target = std::clamp(groundSpeed / kFullBobSpeed, 0.0F, 1.0F);
    const float blend = -std::expm1(-elapsed / kBobResponseTime);
    // Integrate the smoothed speed over the frame so that changing FPS does not change the rhythm.
    const float travelled =
        (target * elapsed.count()) + ((speedShare - target) * kBobResponseTime.count() * blend);
    speedShare = std::lerp(speedShare, target, blend);
    phase = std::fmod(phase + (travelled * kBobCyclesPerSecond * kBobCycle), kBobCycle);
}

auto ViewModelBob::offset() const noexcept -> ViewModelOffset {
    const float amount = speedShare * kBobStrength;
    const float step = 2.0F * phase;
    return {.right = kSideBob * (amount * std::sin(phase)),
            .forward = kDepthBob * (amount * std::sin(step)),
            .up = kVerticalBob * (amount * std::cos(step))};
}

auto ViewModelCvars::registerIn(nx::console::Console& console)
    -> nx::Result<ViewModelCvars, nx::console::CvarError> {
    const auto offset = [&console](std::string_view name, std::string_view description, float limit) {
        return console.registerCvar(nx::console::CvarDesc<float>{.name = name,
                                                                 .description = description,
                                                                 .flags = kArchived,
                                                                 .minimum = -limit,
                                                                 .maximum = limit});
    };
    auto right =
        offset("viewmodel_offset_x", "Shift the weapon right in camera space.", kMaxViewModelSideOffset);
    auto forward =
        offset("viewmodel_offset_y", "Shift the weapon forward in camera space.", kMaxViewModelDepthOffset);
    auto up = offset("viewmodel_offset_z", "Shift the weapon up in camera space.", kMaxViewModelDepthOffset);
    for (auto* const registered : {&right, &forward, &up}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return ViewModelCvars{.right = *right, .forward = *forward, .up = *up};
}

auto ViewModelCvars::offset(const ViewModelBob& motion) const noexcept -> ViewModelOffset {
    ViewModelOffset offset = motion.offset();
    offset.right += nx::HammerUnits{right.value()};
    offset.forward += nx::HammerUnits{forward.value()};
    offset.up += nx::HammerUnits{up.value()};
    return offset;
}

ViewModel::ViewModel(std::unique_ptr<ModelFiles> files, std::vector<nx::assets::AnimationFrames> animations,
                     std::vector<std::optional<nx::render::StudioMaterial>> materials, Hand hand,
                     Appearance appearance) noexcept
    : m_files{std::move(files)}, m_animations{std::move(animations)}, m_materials{std::move(materials)},
      m_hand{hand}, m_appearance{std::move(appearance)} {}

auto ViewModel::read(const Vfs& vfs, const Weapon& weapon) -> nx::Result<ViewModel, ViewModelError> {
    auto files = readModelFiles(vfs, weapon.models.view, ModelTextures::Read);
    if (!files) {
        return nx::fail(ViewModelError::FilesFailed, std::move(files.error()));
    }
    const MdlView& model = *(*files)->view;
    if (auto checked = checkAppearance(model, (*files)->texturesView(), weapon); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (auto checked = checkSequences(model, weapon); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    auto animations = animationsOf(vfs, **files);
    if (!animations) {
        return nx::fail(ViewModelError::FilesFailed, std::move(animations.error()));
    }
    // Its materials for the enhanced look: without them, as the original shades it.
    auto materials = readModelMaterials(vfs, weapon.models.view, (*files)->texturesView());
    if (!materials) {
        nx::log::warning(kViewModelLog, "{}: shaded as the original shades it: {}", weapon.models.view,
                         nx::describe(materials.error()));
    }
    return ViewModel{std::move(*files), std::move(*animations),
                     std::move(materials).value_or(std::vector<std::optional<nx::render::StudioMaterial>>{}),
                     weapon.models.viewHand,
                     Appearance{.skin = weapon.view.skin.value_or(0),
                                .bodyChoices = weapon.view.bodyChoices.value_or(std::vector<std::uint32_t>{}),
                                .muzzleFlash = weapon.view.muzzleFlash}};
}

auto ViewModel::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<nx::render::StudioModel, nx::render::StudioError> {
    return renderer.load(
        commands, {.model = *m_files->view, .textures = m_files->texturesView(), .materials = m_materials});
}

auto ViewModel::sequence(std::uint32_t sequence) const -> const nx::assets::StudioSequence& {
    nx::expects(sequence < m_animations.size(), "ViewModel::sequence: a sequence of the model");
    return m_files->view->sequences()[sequence];
}

auto ViewModel::attachmentAt(std::span<const nx::Mat3x4> bones, std::uint32_t attachment) const
    -> std::optional<nx::Vec3> {
    const std::span<const nx::assets::Attachment> attachments = m_files->view->attachments();
    if (attachment >= attachments.size() || attachments[attachment].bone.value >= bones.size()) {
        return std::nullopt;
    }
    return nx::transformPoint(bones[attachments[attachment].bone.value], attachments[attachment].origin);
}

auto ViewModel::muzzleAt(std::span<const nx::Mat3x4> bones, std::uint32_t sequenceIndex) const
    -> std::optional<nx::Vec3> {
    for (const nx::assets::StudioEvent& event : sequence(sequenceIndex).events) {
        if (const auto attachment = muzzleFlashAttachmentOf(event.event)) {
            return attachmentAt(bones, *attachment);
        }
    }
    return attachmentAt(bones, 0);
}

auto ViewModel::soundEvents() const -> std::vector<std::string_view> {
    std::vector<std::string_view> sounds;
    for (const nx::assets::StudioSequence& sequence : m_files->view->sequences()) {
        for (const nx::assets::StudioEvent& event : sequence.events) {
            if (event.event == kSoundEvent) {
                sounds.emplace_back(event.options);
            }
        }
    }
    return sounds;
}

auto ViewModel::muzzleFlashStyleOf(const nx::assets::StudioEvent& event) const -> MuzzleFlashStyle {
    return cs::muzzleFlashStyleOf(event.options, m_appearance.muzzleFlash);
}

auto ViewModel::muzzleFlashSprites() const -> std::vector<std::string> {
    std::vector<std::string> sprites;
    for (const nx::assets::StudioSequence& sequence : m_files->view->sequences()) {
        for (const nx::assets::StudioEvent& event : sequence.events) {
            if (!muzzleFlashAttachmentOf(event.event)) {
                continue;
            }
            std::string sprite = muzzleFlashStyleOf(event).sprite;
            if (!std::ranges::contains(sprites, sprite)) {
                sprites.push_back(std::move(sprite));
            }
        }
    }
    return sprites;
}

auto soundFilesOf(const Weapon& weapon, const ViewModel* viewModel) -> std::vector<std::string_view> {
    std::vector<std::string_view> files;
    for (const GameSound* const sound : soundsOf(weapon)) {
        std::ranges::copy(sound->files, std::back_inserter(files));
    }
    if (viewModel != nullptr) {
        std::ranges::copy(viewModel->soundEvents(), std::back_inserter(files));
    }
    return files;
}

auto ViewModel::pose(const nx::render::Camera& camera, std::uint32_t sequence, nx::Seconds intoIt, Hand hand,
                     ViewModelOffset offset) const -> std::vector<nx::Mat3x4> {
    const MdlView& model = *m_files->view;
    nx::expects(sequence < m_animations.size(), "ViewModel::pose: a sequence of the model");
    const nx::assets::StudioSequence& played = model.sequences()[sequence];
    // The view's own model turns as the view does: the client gives it the view's angles with the pitch
    // negated, which the engine's studio transform negates back.
    const nx::Mat3 rotation = nx::rotationMatrix(camera.angles);
    // Studio space has +Y to the left. Apply the screen-space shift before mirroring the bones.
    const nx::Vec3 shift{.x = offset.forward.value(), .y = -offset.right.value(), .z = offset.up.value()};
    nx::Mat3x4 placement = nx::affine(rotation, camera.position + (rotation * shift));
    if (isMirroredIn(hand)) {
        // Its left, the placement's second column, turned to its right.
        for (nx::Vec4& row : placement.rows) {
            row.y = -row.y;
        }
    }
    return cs::poseBones(
        cs::skeletonOf(model), played, m_animations[sequence],
        {.frame = cs::sequenceFrameAt(played, intoIt, cs::kOwnSpeed), .placement = placement});
}

auto clientEventsBetween(const nx::assets::StudioSequence& sequence, float from, float to)
    -> std::vector<const nx::assets::StudioEvent*> {
    std::vector<const nx::assets::StudioEvent*> passed;
    for (const nx::assets::StudioEvent& event : sequence.events) {
        const auto frame = static_cast<float>(event.frame);
        if (event.event >= kFirstClientEvent && frame > from && frame <= to) {
            passed.push_back(&event);
        }
    }
    return passed;
}

auto intoPlayOf(const nx::assets::StudioSequence& sequence, float frame, float now, nx::Seconds intoIt)
    -> nx::Seconds {
    if (sequence.fps <= 0.0F) {
        return nx::Seconds{};
    }
    if (!sequence.isLooping) {
        return nx::Seconds{frame / sequence.fps};
    }
    return std::max(intoIt - nx::Seconds{(now - frame) / sequence.fps}, nx::Seconds{});
}

auto ViewModelEvents::passed(const ViewModel* viewModel, const std::optional<ViewModelPose>& pose)
    -> std::vector<PassedEvent> {
    if (!pose || viewModel == nullptr) {
        m_lastPose.reset();
        return {};
    }
    const nx::assets::StudioSequence& sequence = viewModel->sequence(pose->sequence);
    const bool continued = m_lastPose && m_lastPose->isSamePlay(*pose);
    const nx::Seconds previous = continued ? m_lastPose->intoIt : nx::Seconds{};
    m_lastPose = pose;
    if (sequence.isLooping && sequence.frameCount > 1 && sequence.fps > 0.0F) {
        const auto rate = static_cast<double>(sequence.fps);
        const auto framesPerLoop = static_cast<double>(sequence.frameCount - 1);
        const double firstFrame = continued ? static_cast<double>(previous.count()) * rate
                                            : static_cast<double>(kBeforeTheFirstFrame);
        const double lastFrame = static_cast<double>(pose->intoIt.count()) * rate;
        std::vector<PassedEvent> events;
        if (std::ranges::none_of(sequence.events, [](const nx::assets::StudioEvent& event) {
                return event.event >= kFirstClientEvent;
            })) {
            return events;
        }
        const double lastLoop = std::floor(lastFrame / framesPerLoop);
        const double firstLoop = std::max(0.0, std::floor(firstFrame / framesPerLoop));
        const double loopsAfterFirst =
            std::min(lastLoop - firstLoop, static_cast<double>(kMaxLoopsOfEventsPerFrame - 1));
        const std::size_t loopCount =
            loopsAfterFirst >= 0.0 ? static_cast<std::size_t>(loopsAfterFirst) + 1 : 0;
        for (std::size_t loop = 0; loop < loopCount; ++loop) {
            const auto loopsBack = static_cast<double>(loopCount - loop - 1);
            const double start = (lastLoop - loopsBack) * framesPerLoop;
            for (const auto* event : clientEventsBetween(sequence, static_cast<float>(firstFrame - start),
                                                         static_cast<float>(lastFrame - start))) {
                events.push_back(
                    {.event = event,
                     .intoPlay = nx::Seconds{static_cast<float>((start + event->frame) / rate)}});
            }
        }
        return events;
    }
    const float now = sequenceFrameAt(sequence, pose->intoIt, kOwnSpeed).value();
    // A play already passed goes on from the frame it was at; a new one plays its events from its first.
    const float before =
        continued ? sequenceFrameAt(sequence, previous, kOwnSpeed).value() : kBeforeTheFirstFrame;
    return clientEventsBetween(sequence, before, now) |
           std::views::transform([&sequence, now, &pose](const nx::assets::StudioEvent* event) {
               return PassedEvent{
                   .event = event,
                   .intoPlay = intoPlayOf(sequence, static_cast<float>(event->frame), now, pose->intoIt)};
           }) |
           std::ranges::to<std::vector>();
}

} // namespace cs
