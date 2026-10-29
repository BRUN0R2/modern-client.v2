// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The player's input: the buttons and cvars it registers, the commands the buttons make and the view
// the keys turn, as the original's client makes them.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.console;
import cs.shared;
import cs.client;

namespace {

using cs::Button;
using cs::heldShare;
using nx::console::ButtonEdges;

constexpr float kTolerance = 1.0e-4F;
/// A command of the classic rate, 100 a second.
constexpr nx::Milliseconds kTick{10.0F};
constexpr nx::UnitsPerSecond kNoLimit{0.0F};

/// A console's output that keeps the failures.
class FailureOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view /*line*/) override {}
    void reportFailure(const nx::Failure<nx::console::ConsoleError>& failure) override {
        failures.push_back(failure.code);
    }

    std::vector<nx::console::ConsoleError> failures;
};

/// No files.
class NoFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
    }
};

/// A console with the player's input.
class Player {
public:
    Player() : console{output, files}, input{cs::PlayerInput::registerIn(console).value()} {}

    /// Runs `script` now, as the binds' commands run before the frame's commands are made.
    void run(std::string_view script) {
        console.addText(script);
        std::ignore = console.execute();
    }

    [[nodiscard]] auto next(nx::UnitsPerSecond maxSpeed = kNoLimit) -> cs::UserCommand {
        return input.nextCommand({}, kTick, maxSpeed);
    }

    FailureOutput output;
    NoFiles files;
    nx::console::Console console;
    cs::PlayerInput input;
};

TEST(PlayerInputTest, AButtonCountsForTheShareOfTheCommandItWasHeld) {
    using enum cs::ButtonPosition;
    EXPECT_EQ(heldShare(Down, {}), 1.0F) << "held throughout";
    EXPECT_EQ(heldShare(Up, {}), 0.0F);
    EXPECT_EQ(heldShare(Down, ButtonEdges{.pressed = true}), 0.5F) << "pressed during it";
    EXPECT_EQ(heldShare(Up, ButtonEdges{.released = true}), 0.0F) << "released during it";
    EXPECT_EQ(heldShare(Up, ButtonEdges{.pressed = true, .released = true}), 0.25F) << "tapped";
    EXPECT_EQ(heldShare(Down, ButtonEdges{.pressed = true, .released = true}), 0.75F)
        << "let go and pressed again";
}

TEST(PlayerInputTest, RegistersTheOriginalsButtonsAndCvars) {
    Player player;
    for (const std::string_view name :
         {"forward", "back", "moveleft", "moveright", "moveup", "movedown", "left", "right", "lookup",
          "lookdown", "strafe", "speed", "jump", "duck", "use"}) {
        EXPECT_TRUE(player.console.hasCommand(std::format("+{}", name))) << name;
        EXPECT_TRUE(player.console.hasCommand(std::format("-{}", name))) << name;
    }
    const cs::InputCvars& cvars = player.input.cvars();
    EXPECT_FLOAT_EQ(cvars.forwardSpeed.value(), 400.0F);
    EXPECT_FLOAT_EQ(cvars.backSpeed.value(), 400.0F);
    EXPECT_FLOAT_EQ(cvars.sideSpeed.value(), 400.0F);
    EXPECT_FLOAT_EQ(cvars.upSpeed.value(), 320.0F);
    EXPECT_FLOAT_EQ(cvars.moveSpeedKey.value(), 0.52F);
    EXPECT_FLOAT_EQ(cvars.yawSpeed.value(), 210.0F);
    EXPECT_FLOAT_EQ(cvars.pitchSpeed.value(), 225.0F);
    EXPECT_FLOAT_EQ(cvars.angleSpeedKey.value(), 0.67F);

    // What config.cfg keeps: the speeds Counter-Strike archives, and no other.
    const std::string archived = player.console.archivedCvars();
    for (const std::string_view name : {"cl_forwardspeed", "cl_backspeed", "cl_sidespeed"}) {
        EXPECT_NE(archived.find(name), std::string::npos) << name;
    }
    for (const std::string_view name :
         {"cl_upspeed", "cl_movespeedkey", "cl_yawspeed", "cl_pitchspeed", "cl_anglespeedkey"}) {
        EXPECT_EQ(archived.find(name), std::string::npos) << name;
    }
}

TEST(PlayerInputTest, AButtonPressedDuringACommandAsksHalfItsSpeed) {
    Player player;
    player.run("+forward 17");
    const cs::UserCommand pressed = player.next();
    EXPECT_FLOAT_EQ(pressed.move.forward.value(), 200.0F);
    EXPECT_TRUE(pressed.buttons.test(Button::Forward));

    const cs::UserCommand held = player.next();
    EXPECT_FLOAT_EQ(held.move.forward.value(), 400.0F);
    EXPECT_EQ(held.duration, kTick);

    player.run("-forward 17; +back 18; +moveleft 19; +moveup 20");
    const cs::UserCommand turned = player.next();
    EXPECT_FLOAT_EQ(turned.move.forward.value(), -200.0F) << "forward let go, back pressed";
    EXPECT_FLOAT_EQ(turned.move.side.value(), -200.0F);
    EXPECT_FLOAT_EQ(turned.move.up.value(), 160.0F);
    EXPECT_FALSE(turned.buttons.test(Button::Forward));
    EXPECT_TRUE(turned.buttons.test(Button::Back));
    EXPECT_TRUE(turned.buttons.test(Button::MoveLeft));
    EXPECT_TRUE(player.output.failures.empty());
}

TEST(PlayerInputTest, SpeedWalksAndTheLimitCapsTheSpeedsTogether) {
    Player player;
    player.run("+forward 17; +moveright 18");
    std::ignore = player.next();

    const cs::UserCommand diagonal = player.next(nx::UnitsPerSecond{250.0F});
    const float each = 400.0F * (250.0F / std::sqrt(2.0F * 400.0F * 400.0F));
    EXPECT_NEAR(diagonal.move.forward.value(), each, kTolerance);
    EXPECT_NEAR(diagonal.move.side.value(), each, kTolerance);

    player.run("-moveright 18; +speed");
    const cs::UserCommand walking = player.next(nx::UnitsPerSecond{250.0F});
    EXPECT_FLOAT_EQ(walking.move.forward.value(), 250.0F * 0.52F) << "a share of its own speed: 130";
    EXPECT_LT(walking.move.forward.value(), 150.0F) << "below the speed its steps sound at";
    EXPECT_EQ(walking.move.side.value(), 0.0F);

    player.run("cl_movespeedkey 0.9");
    EXPECT_FLOAT_EQ(player.next(nx::UnitsPerSecond{250.0F}).move.forward.value(), 250.0F * 0.52F)
        << "the key 0.52 at most";
    player.run("cl_movespeedkey 0.3");
    EXPECT_FLOAT_EQ(player.next(nx::UnitsPerSecond{250.0F}).move.forward.value(), 250.0F * 0.3F);
    player.run("cl_forwardspeed 100");
    EXPECT_FLOAT_EQ(player.next(nx::UnitsPerSecond{250.0F}).move.forward.value(), 100.0F * 0.3F)
        << "of what it asks under the limit";
}

TEST(PlayerInputTest, AJumpPressedAndReleasedWithinACommandStillJumps) {
    Player player;
    player.run("+jump 239; -jump 239"); // a notch of the wheel
    const cs::UserCommand notch = player.next();
    EXPECT_TRUE(notch.buttons.test(Button::Jump));
    EXPECT_FALSE(player.next().buttons.test(Button::Jump)) << "once";

    player.run("+use 101");
    EXPECT_TRUE(player.next().buttons.test(Button::Use));
    player.run("-use 101; +duck 102");
    const cs::UserCommand ducked = player.next();
    EXPECT_FALSE(ducked.buttons.test(Button::Use));
    EXPECT_TRUE(ducked.buttons.test(Button::Duck));
}

TEST(PlayerInputTest, TheArrowsTurnTheViewOrStrafe) {
    Player player;
    nx::EulerAngles view{};
    player.run("+left 130");
    player.input.turn(view, nx::Seconds{0.1F});
    EXPECT_NEAR(view.yaw.value(), 0.1F * 210.0F * 0.5F, kTolerance) << "pressed during the frame";
    player.input.turn(view, nx::Seconds{0.1F});
    EXPECT_NEAR(view.yaw.value(), 0.1F * 210.0F * 1.5F, kTolerance);

    player.run("+speed; +lookdown 149");
    player.input.turn(view, nx::Seconds{0.1F});
    EXPECT_NEAR(view.pitch.value(), 0.1F * 0.67F * 225.0F * 0.5F, kTolerance);
    player.input.turn(view, nx::Seconds{10.0F});
    EXPECT_FLOAT_EQ(view.pitch.value(), cs::kMaxPitch.value());

    player.run("-speed; +strafe");
    const float yaw = view.yaw.value();
    player.input.turn(view, nx::Seconds{0.1F});
    EXPECT_FLOAT_EQ(view.yaw.value(), yaw) << "+left moves rather than turns";
    EXPECT_FLOAT_EQ(player.next().move.side.value(), -400.0F);
}

} // namespace
