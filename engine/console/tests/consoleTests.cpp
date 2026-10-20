// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

#include "consoleFixtures.hpp"

namespace {

using nx::Result;
using nx::console::BufferState;
using nx::console::Cheats;
using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;
using nx::console::Cvar;
using nx::console::CvarDesc;
using nx::console::CvarFlag;

using Lines = std::vector<std::string>;
using Codes = std::vector<ConsoleError>;

class ConsoleTest : public testing::Test {
protected:
    ConsoleTest() {
        gravity = console.registerCvar(CvarDesc<float>{
            .name = "sv_gravity", .defaultValue = 800, .flags = {CvarFlag::Notify, CvarFlag::Archive}});
        roundTime = console.registerCvar(
            CvarDesc<float>{.name = "mp_roundtime", .defaultValue = 5, .minimum = 1.0F, .maximum = 9.0F});
        hostname = console.registerCvar(CvarDesc<std::string>{
            .name = "hostname", .defaultValue = "nextds", .flags = {CvarFlag::Archive}});
        EXPECT_TRUE(gravity && roundTime && hostname);
    }

    /// Queues `script` and runs it to the end (or to the first wait).
    auto run(std::string_view script) -> BufferState {
        console.addText(script);
        return console.execute();
    }

    nx_test::RecordingOutput output;
    nx_test::MemoryFiles files;
    Console console{output, files};
    Result<Cvar<float>, nx::console::CvarError> gravity = nx::fail(nx::console::CvarError::InvalidName);
    Result<Cvar<float>, nx::console::CvarError> roundTime = nx::fail(nx::console::CvarError::InvalidName);
    Result<Cvar<std::string>, nx::console::CvarError> hostname =
        nx::fail(nx::console::CvarError::InvalidName);
};

TEST_F(ConsoleTest, TypingACvarPrintsItAndAValueSetsIt) {
    EXPECT_EQ(run("sv_gravity"), BufferState::Empty);
    EXPECT_EQ(output.lines, (Lines{R"("sv_gravity" is "800")"}));

    run("SV_GRAVITY 600; hostname \"My Server\"");
    EXPECT_EQ(gravity->value(), 600.0F);
    EXPECT_EQ(gravity->revision(), 1U);
    EXPECT_EQ(hostname->value(), "My Server");
    EXPECT_TRUE(output.failures.empty());
}

TEST_F(ConsoleTest, ARefusedValueIsReportedAndKeepsTheOldOne) {
    run("mp_roundtime abc; mp_roundtime 12; mp_roundtime 2.5");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::InvalidValue, ConsoleError::InvalidValue}));
    EXPECT_EQ(output.failures.front().detail, R"(mp_roundtime: expected a number in [1, 9], got "abc")");
    EXPECT_EQ(output.failures.front().cause.records().front().code, "InvalidValue");
    EXPECT_EQ(output.failures.back().cause.records().front().code, "OutOfRange");
    EXPECT_EQ(roundTime->value(), 2.5F) << "the run goes on after a failure";
}

TEST_F(ConsoleTest, ACvarTakesOneValue) {
    run("hostname My Server");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::InvalidArguments}));
    EXPECT_EQ(hostname->value(), "nextds");
}

TEST_F(ConsoleTest, UnknownCommandsSuggestTheClosestName) {
    run("sv_gravty 100; xyzzy");
    ASSERT_EQ(output.failureCodes(), (Codes{ConsoleError::UnknownCommand, ConsoleError::UnknownCommand}));
    EXPECT_EQ(output.failures[0].detail, R"(unknown command "sv_gravty"; did you mean "sv_gravity"?)");
    EXPECT_EQ(output.failures[1].detail, R"(unknown command "xyzzy")");
}

TEST_F(ConsoleTest, CheatCvarsChangeOnlyWithCheatsAllowed) {
    const auto noclipSpeed = console.registerCvar(
        CvarDesc<float>{.name = "sv_noclipspeed", .defaultValue = 5, .flags = {CvarFlag::Cheat}});
    ASSERT_TRUE(noclipSpeed.has_value());

    run("sv_noclipspeed 10");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::CheatProtected}));
    EXPECT_EQ(noclipSpeed->value(), 5.0F);

    console.setCheats(Cheats::Allowed);
    output.clear();
    run("sv_noclipspeed 10");
    EXPECT_TRUE(output.failures.empty());
    EXPECT_EQ(noclipSpeed->value(), 10.0F);

    // Code is not restricted by the flag.
    console.setCheats(Cheats::Forbidden);
    EXPECT_TRUE(noclipSpeed->set(7).has_value());
}

TEST_F(ConsoleTest, ServerProtectedCvarsChangeOnlyFromCode) {
    // A client's mirror of a server cvar: prediction reads it, the server's replication writes it.
    const auto mirror = console.registerCvar(
        CvarDesc<float>{.name = "sv_accelerate", .defaultValue = 5, .flags = {CvarFlag::ServerProtected}});
    ASSERT_TRUE(mirror.has_value());
    console.setCheats(Cheats::Allowed);

    run("sv_accelerate 100");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::ServerProtected}));
    EXPECT_EQ(console.setCvar("sv_accelerate", "100").error().code, ConsoleError::ServerProtected);
    EXPECT_EQ(mirror->value(), 5.0F);
    EXPECT_TRUE(mirror->set(10).has_value());

    output.clear();
    run("sv_accelerate");
    EXPECT_EQ(output.lines, (Lines{R"("sv_accelerate" is "10")"}));
}

TEST_F(ConsoleTest, ArchivedCvarsAreTheConfigFile) {
    run("sv_gravity 600; hostname \"My Server; really\"; mp_roundtime 3");
    EXPECT_EQ(console.archivedCvars(), "hostname \"My Server; really\"\nsv_gravity \"600\"\n");

    // config.cfg runs back into the same values.
    nx_test::RecordingOutput otherOutput;
    nx_test::MemoryFiles otherFiles;
    Console other{otherOutput, otherFiles};
    const auto otherGravity =
        other.registerCvar(CvarDesc<float>{.name = "sv_gravity", .flags = {CvarFlag::Archive}});
    const auto otherHostname =
        other.registerCvar(CvarDesc<std::string>{.name = "hostname", .flags = {CvarFlag::Archive}});
    other.addText(console.archivedCvars());
    EXPECT_EQ(other.execute(), BufferState::Empty);
    EXPECT_EQ(otherGravity->value(), 600.0F);
    EXPECT_EQ(otherHostname->value(), "My Server; really");
}

TEST_F(ConsoleTest, EchoPrintsItsWords) {
    run("echo hello   \"big world\"; echo");
    EXPECT_EQ(output.lines, (Lines{"hello big world", ""}));
}

TEST_F(ConsoleTest, RegisteredCommandsGetTheirWords) {
    std::vector<std::string> received;
    ASSERT_TRUE(
        console
            .registerCommand({.name = "kick", .description = "kick <player>"},
                             [&received](Console&, const CommandLine& line) -> Result<void, ConsoleError> {
                                 received.assign(line.words.begin(), line.words.end());
                                 if (line.arguments().empty()) {
                                     return nx::fail(ConsoleError::InvalidArguments, "usage: kick <player>");
                                 }
                                 return {};
                             })
            .has_value());
    EXPECT_TRUE(console.hasCommand("KICK"));

    run("KICK \"Player One\"");
    EXPECT_EQ(received, (std::vector<std::string>{"KICK", "Player One"}));
    run("kick");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::InvalidArguments}));
}

TEST_F(ConsoleTest, CommandNamesAreChecked) {
    const auto handler = [](Console&, const CommandLine&) -> Result<void, ConsoleError> {
        return {};
    };
    EXPECT_EQ(console.registerCommand({.name = "bad name"}, handler).error().code, ConsoleError::InvalidName);
    EXPECT_EQ(console.registerCommand({.name = "SV_GRAVITY"}, handler).error().code, ConsoleError::NameTaken);
    EXPECT_EQ(console.registerCommand({.name = "alias"}, handler).error().code, ConsoleError::NameTaken);
}

TEST_F(ConsoleTest, AliasesRunTheirBodyInPlace) {
    run(R"(alias greet "echo hello; echo world"; echo before; greet; echo after)");
    EXPECT_EQ(output.lines, (Lines{"before", "hello", "world", "after"}));
    EXPECT_EQ(console.findAlias("GREET"), std::optional<std::string_view>{"echo hello; echo world"});
}

TEST_F(ConsoleTest, AliasBodiesAreTheRemainingWords) {
    run("alias low sv_gravity 200; low");
    EXPECT_EQ(console.findAlias("low"), std::optional<std::string_view>{"sv_gravity 200"});
    EXPECT_EQ(gravity->value(), 200.0F);
    run("alias nothing; nothing");
    EXPECT_EQ(console.findAlias("nothing"), std::optional<std::string_view>{""});
    EXPECT_TRUE(output.failures.empty());
}

TEST_F(ConsoleTest, AliasesCanRedefineAliases) {
    // The classic toggle script.
    run(R"(alias toggleLight "lightOn"; alias lightOn "echo on; alias toggleLight lightOff"; )"
        R"(alias lightOff "echo off; alias toggleLight lightOn")");
    run("toggleLight; toggleLight; toggleLight");
    EXPECT_EQ(output.lines, (Lines{"on", "off", "on"}));
}

TEST_F(ConsoleTest, AliasesCannotTakeTheNameOfACommandOrCvar) {
    run("alias exec echo no; alias sv_gravity echo no; alias \"two words\" echo no");
    EXPECT_EQ(output.failureCodes(),
              (Codes{ConsoleError::NameTaken, ConsoleError::NameTaken, ConsoleError::InvalidName}));
}

TEST_F(ConsoleTest, AliasListing) {
    run("alias b echo b; alias A echo a; alias");
    EXPECT_EQ(output.lines, (Lines{"A : echo a", "b : echo b"}));
}

TEST_F(ConsoleTest, ARunawayAliasIsStopped) {
    run("alias forever forever; forever; echo never");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::ExpansionLimit}));
    EXPECT_TRUE(output.lines.empty()) << "the buffered commands were dropped";
    // The next run starts with a fresh budget.
    run("echo again");
    EXPECT_EQ(output.lines, (Lines{"again"}));
}

TEST_F(ConsoleTest, WaitResumesOnTheNextExecute) {
    EXPECT_EQ(run("echo one; wait; echo two; wait; echo three"), BufferState::Waiting);
    EXPECT_EQ(output.lines, (Lines{"one"}));
    EXPECT_EQ(console.execute(), BufferState::Waiting);
    EXPECT_EQ(output.lines, (Lines{"one", "two"}));
    EXPECT_EQ(console.execute(), BufferState::Empty);
    EXPECT_EQ(output.lines, (Lines{"one", "two", "three"}));
    EXPECT_EQ(console.execute(), BufferState::Empty);
}

TEST_F(ConsoleTest, AWaitInsideAnAliasDelaysTheRestOfTheBuffer) {
    run(R"(alias jumpThrow "echo jump; wait; echo throw"; jumpThrow; echo next)");
    EXPECT_EQ(output.lines, (Lines{"jump"}));
    EXPECT_EQ(console.execute(), BufferState::Empty);
    EXPECT_EQ(output.lines, (Lines{"jump", "throw", "next"}));
}

TEST_F(ConsoleTest, AWaitAtTheEndLeavesNothingBuffered) {
    EXPECT_EQ(run("echo one; wait"), BufferState::Empty);
}

TEST_F(ConsoleTest, AFrameLoopWithWaitIsNotARunaway) {
    run(R"(alias loop "sv_gravity 1; wait; loop"; loop)");
    for (int frame = 0; frame < 3; ++frame) {
        EXPECT_EQ(console.execute(), BufferState::Waiting);
    }
    EXPECT_TRUE(output.failures.empty());
}

TEST_F(ConsoleTest, ExecRunsTheFileBeforeTheRestOfTheBuffer) {
    files.files["server.cfg"] =
        "// server settings\r\nsv_gravity 700\r\necho in server.cfg\r\nexec extra.cfg\r\n";
    files.files["extra.cfg"] = "\xEF\xBB\xBF"
                               "echo in extra.cfg";
    run("exec server.cfg; echo after");
    EXPECT_EQ(output.lines,
              (Lines{"execing server.cfg", "in server.cfg", "execing extra.cfg", "in extra.cfg", "after"}));
    EXPECT_EQ(gravity->value(), 700.0F);
    EXPECT_TRUE(output.failures.empty()) << "the byte order mark is not part of the first command";
}

TEST_F(ConsoleTest, ExecOfAMissingFileFails) {
    run("exec missing.cfg; echo after");
    ASSERT_EQ(output.failureCodes(), (Codes{ConsoleError::ExecFailed}));
    EXPECT_EQ(output.failures.front().cause.records().front().code, "NotFound");
    EXPECT_EQ(output.lines, (Lines{"after"}));
    run("exec");
    EXPECT_EQ(output.failures.back().code, ConsoleError::InvalidArguments);
}

TEST_F(ConsoleTest, AConfigThatExecsItselfIsStopped) {
    files.files["loop.cfg"] = "exec loop.cfg";
    run("exec loop.cfg");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::ExpansionLimit}));
}

TEST_F(ConsoleTest, InsertedTextRunsFirst) {
    console.addText("echo second");
    console.insertText("echo first");
    console.execute();
    EXPECT_EQ(output.lines, (Lines{"first", "second"}));
}

TEST_F(ConsoleTest, ToggleSwitchesOrCycles) {
    const auto flashlight = console.registerCvar(CvarDesc<bool>{.name = "mp_flashlight"});
    ASSERT_TRUE(flashlight.has_value());
    run("toggle mp_flashlight");
    EXPECT_TRUE(flashlight->value());
    run("toggle mp_flashlight");
    EXPECT_FALSE(flashlight->value());

    run("toggle mp_roundtime 2 3 4.5");
    EXPECT_EQ(roundTime->value(), 2.0F) << "the current value is not listed: the first one";
    run("toggle mp_roundtime 2 3 4.5; toggle mp_roundtime 2 3 4.5");
    EXPECT_EQ(roundTime->value(), 4.5F);
    run("toggle mp_roundtime 2 3 4.5");
    EXPECT_EQ(roundTime->value(), 2.0F) << "wraps around";

    run("toggle; toggle nothing");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::InvalidArguments, ConsoleError::UnknownCvar}));
}

TEST_F(ConsoleTest, IncrementVarWrapsAroundTheRange) {
    run("mp_roundtime 3; incrementvar mp_roundtime 1 4 1");
    EXPECT_EQ(roundTime->value(), 4.0F);
    run("incrementvar mp_roundtime 1 4 1");
    EXPECT_EQ(roundTime->value(), 1.0F);
    run("incrementvar mp_roundtime 1 4 -1");
    EXPECT_EQ(roundTime->value(), 4.0F);

    run("incrementvar hostname 0 1 1; incrementvar mp_roundtime 1 x 1; incrementvar nothing 0 1 1");
    EXPECT_EQ(output.failureCodes(), (Codes{ConsoleError::InvalidArguments, ConsoleError::InvalidArguments,
                                            ConsoleError::UnknownCvar}));
}

TEST_F(ConsoleTest, ForEachCvarIsSortedByName) {
    std::vector<std::string_view> names;
    console.forEachCvar([&names](const nx::console::CvarBase& cvar) { names.push_back(cvar.name()); });
    EXPECT_EQ(names, (std::vector<std::string_view>{"hostname", "mp_roundtime", "sv_gravity"}));
}

} // namespace
