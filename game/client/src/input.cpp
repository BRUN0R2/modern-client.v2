// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.console;
import cs.shared;

namespace cs {

namespace {

using nx::console::Button;
using nx::console::Console;
using nx::console::CvarDesc;
using nx::console::CvarFlag;

constexpr nx::console::CvarFlags kArchived{CvarFlag::Archive};

// The values Counter-Strike 1.6's client starts with.
constexpr float kMoveSpeed = 400.0F;
constexpr float kUpSpeed = 320.0F;
constexpr float kMoveSpeedKey = 0.52F;
constexpr float kYawSpeed = 210.0F;
constexpr float kPitchSpeed = 225.0F;
constexpr float kAngleSpeedKey = 0.67F;

[[nodiscard]] auto registerButtons(Console& console) -> nx::Result<InputButtons, nx::console::ConsoleError> {
    const auto add = [&console](std::string_view name, std::string_view description) {
        return console.registerButton({.name = name, .description = description});
    };
    auto forward = add("forward", "Moves forward while held.");
    auto back = add("back", "Moves back while held.");
    auto moveLeft = add("moveleft", "Moves left while held.");
    auto moveRight = add("moveright", "Moves right while held.");
    auto moveUp = add("moveup", "Swims, or flies in noclip, up while held.");
    auto moveDown = add("movedown", "Swims, or flies in noclip, down while held.");
    auto left = add("left", "Turns the view left while held; moves left with +strafe.");
    auto right = add("right", "Turns the view right while held; moves right with +strafe.");
    auto lookUp = add("lookup", "Turns the view up while held.");
    auto lookDown = add("lookdown", "Turns the view down while held.");
    auto strafe = add("strafe", "Makes +left and +right move rather than turn while held.");
    auto speed =
        add("speed", "Walks while held, at cl_movespeedkey of the speeds; turns at cl_anglespeedkey.");
    auto jump = add("jump", "Jumps.");
    auto duck = add("duck", "Ducks while held.");
    auto use = add("use", "Uses what the player looks at.");
    auto attack = add("attack", "Fires the weapon in hand while held.");
    auto attack2 = add("attack2", "Switches the weapon in hand to its silencer or its burst, and back.");
    auto reload = add("reload", "Reloads the weapon in hand.");
    for (auto* const registered :
         {&forward, &back, &moveLeft, &moveRight, &moveUp, &moveDown, &left, &right, &lookUp, &lookDown,
          &strafe, &speed, &jump, &duck, &use, &attack, &attack2, &reload}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return InputButtons{.forward = *forward,
                        .back = *back,
                        .moveLeft = *moveLeft,
                        .moveRight = *moveRight,
                        .moveUp = *moveUp,
                        .moveDown = *moveDown,
                        .left = *left,
                        .right = *right,
                        .lookUp = *lookUp,
                        .lookDown = *lookDown,
                        .strafe = *strafe,
                        .speed = *speed,
                        .jump = *jump,
                        .duck = *duck,
                        .use = *use,
                        .attack = *attack,
                        .attack2 = *attack2,
                        .reload = *reload};
}

[[nodiscard]] auto registerCvars(Console& console) -> nx::Result<InputCvars, nx::console::CvarError> {
    const auto add = [&console](std::string_view name, float value, std::string_view description,
                                nx::console::CvarFlags flags) {
        return console.registerCvar(
            CvarDesc<float>{.name = name, .defaultValue = value, .description = description, .flags = flags});
    };
    auto forwardSpeed =
        add("cl_forwardspeed", kMoveSpeed, "The speed +forward asks, in units a second.", kArchived);
    auto backSpeed = add("cl_backspeed", kMoveSpeed, "The speed +back asks, in units a second.", kArchived);
    auto sideSpeed = add("cl_sidespeed", kMoveSpeed,
                         "The speed +moveleft and +moveright ask, in units a second.", kArchived);
    auto upSpeed = add("cl_upspeed", kUpSpeed, "The speed +moveup and +movedown ask, in units a second.", {});
    auto moveSpeedKey = add("cl_movespeedkey", kMoveSpeedKey, "The share of each speed +speed asks.", {});
    auto yawSpeed = add("cl_yawspeed", kYawSpeed, "Degrees a second +left and +right turn the view.", {});
    auto pitchSpeed =
        add("cl_pitchspeed", kPitchSpeed, "Degrees a second +lookup and +lookdown turn the view.", {});
    auto angleSpeedKey =
        add("cl_anglespeedkey", kAngleSpeedKey, "The share of each turn while +speed is held.", {});
    for (auto* const registered : {&forwardSpeed, &backSpeed, &sideSpeed, &upSpeed, &moveSpeedKey, &yawSpeed,
                                   &pitchSpeed, &angleSpeedKey}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return InputCvars{.forwardSpeed = *forwardSpeed,
                      .backSpeed = *backSpeed,
                      .sideSpeed = *sideSpeed,
                      .upSpeed = *upSpeed,
                      .moveSpeedKey = *moveSpeedKey,
                      .yawSpeed = *yawSpeed,
                      .pitchSpeed = *pitchSpeed,
                      .angleSpeedKey = *angleSpeedKey};
}

/// The share of the time since its edges were last taken that `button` was held; takes them.
[[nodiscard]] auto takeShare(const Button& button) -> float {
    const nx::console::ButtonEdges edges = button.takeEdges();
    return heldShare(button.isDown(), edges);
}

/// Whether `button` is down, or went down since its edges were last taken; takes them.
[[nodiscard]] auto takeIsPressed(const Button& button) -> bool {
    return button.takeEdges().pressed || button.isDown();
}

} // namespace

auto PlayerInput::registerIn(Console& console) -> nx::Result<PlayerInput, InputError> {
    auto inputButtons = registerButtons(console);
    if (!inputButtons) {
        return nx::fail(InputError::RegistrationFailed, std::move(inputButtons.error()));
    }
    auto inputCvars = registerCvars(console);
    if (!inputCvars) {
        return nx::fail(InputError::RegistrationFailed, std::move(inputCvars.error()));
    }
    return PlayerInput{*inputButtons, *inputCvars};
}

void PlayerInput::turn(nx::EulerAngles& view, nx::Seconds elapsed) {
    const float speed = elapsed.count() * (m_buttons.speed.isDown() ? m_cvars.angleSpeedKey.value() : 1.0F);
    if (!m_buttons.strafe.isDown()) {
        const float yawSpeed = speed * m_cvars.yawSpeed.value();
        float yaw = view.yaw.value();
        yaw -= yawSpeed * takeShare(m_buttons.right);
        yaw += yawSpeed * takeShare(m_buttons.left);
        view.yaw = nx::normalizeAngle(nx::Degrees{yaw});
    }
    const float pitchSpeed = speed * m_cvars.pitchSpeed.value();
    float pitch = view.pitch.value();
    pitch -= pitchSpeed * takeShare(m_buttons.lookUp);
    pitch += pitchSpeed * takeShare(m_buttons.lookDown);
    view.pitch = nx::Degrees{std::clamp(pitch, -kMaxPitch.value(), kMaxPitch.value())};
}

auto PlayerInput::nextCommand(nx::EulerAngles view, nx::Milliseconds duration, nx::UnitsPerSecond maxSpeed)
    -> UserCommand {
    const float sideSpeed = m_cvars.sideSpeed.value();
    const float upSpeed = m_cvars.upSpeed.value();
    float side = 0.0F;
    float up = 0.0F;
    float forward = 0.0F;
    if (m_buttons.strafe.isDown()) {
        side += sideSpeed * takeShare(m_buttons.right);
        side -= sideSpeed * takeShare(m_buttons.left);
    }
    side += sideSpeed * takeShare(m_buttons.moveRight);
    side -= sideSpeed * takeShare(m_buttons.moveLeft);
    up += upSpeed * takeShare(m_buttons.moveUp);
    up -= upSpeed * takeShare(m_buttons.moveDown);
    forward += m_cvars.forwardSpeed.value() * takeShare(m_buttons.forward);
    forward -= m_cvars.backSpeed.value() * takeShare(m_buttons.back);
    if (m_buttons.speed.isDown()) {
        const float share = m_cvars.moveSpeedKey.value();
        forward *= share;
        side *= share;
        up *= share;
    }
    // The speeds together no more than the player's limit, each cut by the same share.
    if (maxSpeed.value() != 0.0F) {
        const auto requested =
            static_cast<float>(std::sqrt(double{(forward * forward) + (side * side) + (up * up)}));
        if (requested > maxSpeed.value()) {
            const float share = maxSpeed.value() / requested;
            forward *= share;
            side *= share;
            up *= share;
        }
    }

    // The movement buttons as they are now: their edges went into the speeds above.
    nx::Flags<cs::Button> held;
    for (const auto& [button, isSet] : {std::pair{cs::Button::Forward, m_buttons.forward.isDown()},
                                        std::pair{cs::Button::Back, m_buttons.back.isDown()},
                                        std::pair{cs::Button::MoveLeft, m_buttons.moveLeft.isDown()},
                                        std::pair{cs::Button::MoveRight, m_buttons.moveRight.isDown()},
                                        std::pair{cs::Button::Jump, takeIsPressed(m_buttons.jump)},
                                        std::pair{cs::Button::Duck, takeIsPressed(m_buttons.duck)},
                                        std::pair{cs::Button::Use, takeIsPressed(m_buttons.use)},
                                        std::pair{cs::Button::Attack, takeIsPressed(m_buttons.attack)},
                                        std::pair{cs::Button::Attack2, takeIsPressed(m_buttons.attack2)},
                                        std::pair{cs::Button::Reload, takeIsPressed(m_buttons.reload)}}) {
        if (isSet) {
            held.set(button);
        }
    }
    return UserCommand{.duration = duration,
                       .viewAngles = view,
                       .move = {.forward = nx::UnitsPerSecond{forward},
                                .side = nx::UnitsPerSecond{side},
                                .up = nx::UnitsPerSecond{up}},
                       .buttons = held,
                       .weaponSelect = std::exchange(m_weaponSelect, std::nullopt)};
}

} // namespace cs
