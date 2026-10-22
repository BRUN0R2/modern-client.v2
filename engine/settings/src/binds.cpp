// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.settings;

import std;
import nx.core;
import nx.platform;
import nx.console;

namespace nx::settings {

namespace {

using console::Console;
using console::ConsoleError;
using console::KeyId;

/// A key and what Counter-Strike 1.6 binds to it.
struct DefaultBind {
    std::string_view key;
    std::string_view command;
};

/// Counter-Strike 1.6's binds, in the order its options list them.
constexpr auto kDefaultBinds = std::to_array<DefaultBind>({
    {"h", "+commandmenu"},
    {"k", "+voicerecord"},
    {"b", "buy"},
    {",", "buyammo1"},
    {".", "buyammo2"},
    {"o", "buyequip"},
    {"F1", "autobuy"},
    {"F2", "rebuy"},
    {"m", "chooseteam"},
    {"z", "radio1"},
    {"x", "radio2"},
    {"c", "radio3"},
    {"g", "drop"},
    {"n", "nightvision"},
    {"w", "+forward"},
    {"UPARROW", "+forward"},
    {"s", "+back"},
    {"DOWNARROW", "+back"},
    {"LEFTARROW", "+left"},
    {"RIGHTARROW", "+right"},
    {"a", "+moveleft"},
    {"d", "+moveright"},
    {"SPACE", "+jump"},
    {"CTRL", "+duck"},
    {"TAB", "+showscores"},
    {"i", "showbriefing"},
    {"e", "+use"},
    {"'", "+moveup"},
    {"/", "+movedown"},
    {"PGUP", "+lookup"},
    {"PGDN", "+lookdown"},
    {"END", "force_centerview"},
    {"ALT", "+strafe"},
    {"INS", "+klook"},
    {"SEMICOLON", "+mlook"},
    {"r", "+reload"},
    {"SHIFT", "+speed"},
    {"MOUSE1", "+attack"},
    {"ENTER", "+attack"},
    {"MOUSE2", "+attack2"},
    {"\\", "+attack2"},
    {"f", "impulse 100"},
    {"t", "impulse 201"},
    {"1", "slot1"},
    {"2", "slot2"},
    {"3", "slot3"},
    {"4", "slot4"},
    {"5", "slot5"},
    {"6", "slot6"},
    {"7", "slot7"},
    {"8", "slot8"},
    {"9", "slot9"},
    {"0", "slot10"},
    {"MWHEELUP", "invprev"},
    {"[", "invprev"},
    {"MWHEELDOWN", "invnext"},
    {"]", "invnext"},
    {"q", "lastinv"},
    {"F5", "snapshot"},
    {"F10", "quit"},
    {"ESCAPE", "escape"},
    {"~", "toggleconsole"},
    {"`", "toggleconsole"},
    {"y", "messagemode"},
    {"u", "messagemode2"},
});

[[nodiscard]] auto indexOf(KeyId key) noexcept -> std::size_t {
    return std::size_t{std::to_underlying(key)};
}

/// The key the first argument of `line` names. Errors: InvalidArguments, with `usage` when there is
/// no argument.
[[nodiscard]] auto keyArgument(const console::CommandLine& line, std::string_view usage)
    -> Result<KeyId, ConsoleError> {
    const auto arguments = line.arguments();
    if (arguments.empty()) {
        return fail(ConsoleError::InvalidArguments, std::format("usage: {}", usage));
    }
    const std::optional<KeyId> key = keyNamed(arguments.front());
    if (!key) {
        return fail(ConsoleError::InvalidArguments,
                    std::format("\"{}\" isn't a valid key", arguments.front()));
    }
    return *key;
}

[[nodiscard]] auto bindCommand(Console& console, KeyBindings& bindings, const console::CommandLine& line)
    -> Result<void, ConsoleError> {
    return keyArgument(line, "bind <key> [command]").and_then([&](KeyId key) -> Result<void, ConsoleError> {
        const auto arguments = line.arguments();
        if (arguments.size() == 1) {
            const std::string_view command = bindings.commandOf(key);
            console.print(command.empty() ? std::format("\"{}\" is not bound", arguments.front())
                                          : std::format("\"{}\" = \"{}\"", arguments.front(), command));
            return {};
        }
        std::string command =
            arguments.subspan(1) | std::views::join_with(' ') | std::ranges::to<std::string>();
        if (command.contains('"')) {
            return fail(ConsoleError::InvalidArguments,
                        std::format("bind {}: a command holds no \"", arguments.front()));
        }
        bindings.bind(key, std::move(command));
        return {};
    });
}

[[nodiscard]] auto unbindCommand(KeyBindings& bindings, const console::CommandLine& line)
    -> Result<void, ConsoleError> {
    if (line.arguments().size() > 1) {
        return fail(ConsoleError::InvalidArguments, "usage: unbind <key>");
    }
    return keyArgument(line, "unbind <key>").transform([&bindings](KeyId key) { bindings.bind(key, {}); });
}

void listBinds(Console& console, const KeyBindings& bindings) {
    std::size_t count = 0;
    for (std::uint32_t number = 0; number < kKeyCount; ++number) {
        const KeyId key{number};
        if (const std::string_view command = bindings.commandOf(key); !command.empty()) {
            console.print(std::format("{} \"{}\"", nameOf(key).value_or(""), command));
            ++count;
        }
    }
    console.print(std::format("{} {} bound", count, count == 1 ? "key" : "keys"));
}

} // namespace

auto KeyBindings::commandOf(KeyId key) const noexcept -> std::string_view {
    const std::size_t index = indexOf(key);
    return index < m_commands.size() ? std::string_view{m_commands[index]} : std::string_view{};
}

void KeyBindings::bind(KeyId key, std::string command) {
    expects(nameOf(key).has_value(), "KeyBindings::bind: a key with a name");
    expects(!command.contains('"') && !command.contains('\n'),
            "KeyBindings::bind: a command config.cfg reads back");
    m_commands[indexOf(key)] = std::move(command);
}

void KeyBindings::unbindAll() noexcept {
    for (std::string& command : m_commands) {
        command.clear();
    }
}

auto KeyBindings::configText() const -> std::string {
    std::string text = "unbindall\n";
    for (std::uint32_t number = 0; number < kKeyCount; ++number) {
        const KeyId key{number};
        if (const std::string_view command = commandOf(key); !command.empty()) {
            text += std::format("bind \"{}\" \"{}\"\n", nameOf(key).value_or(""), command);
        }
    }
    return text;
}

auto defaultBindings() -> KeyBindings {
    KeyBindings bindings;
    for (const DefaultBind& bind : kDefaultBinds) {
        const std::optional<KeyId> key = keyNamed(bind.key);
        invariant(key.has_value(), "defaultBindings: a key of the original");
        bindings.bind(*key, std::string{bind.command});
    }
    return bindings;
}

auto registerBindCommands(Console& console, KeyBindings& bindings) -> Result<void, ConsoleError> {
    if (auto registered = console.registerCommand(
            {.name = "bind", .description = "binds a command to a key, or prints what the key runs"},
            [&bindings](Console& self, const console::CommandLine& line) {
                return bindCommand(self, bindings, line);
            });
        !registered) {
        return registered;
    }
    if (auto registered = console.registerCommand({.name = "unbind", .description = "unbinds a key"},
                                                  [&bindings](Console&, const console::CommandLine& line) {
                                                      return unbindCommand(bindings, line);
                                                  });
        !registered) {
        return registered;
    }
    if (auto registered = console.registerCommand({.name = "unbindall", .description = "unbinds every key"},
                                                  [&bindings](Console&, const console::CommandLine&) {
                                                      bindings.unbindAll();
                                                      return Result<void, ConsoleError>{};
                                                  });
        !registered) {
        return registered;
    }
    return console.registerCommand(
        {.name = "bindlist", .description = "lists the bound keys and their commands"},
        [&bindings](Console& self, const console::CommandLine&) {
            listBinds(self, bindings);
            return Result<void, ConsoleError>{};
        });
}

void BoundInput::handle(const platform::WindowEvent& event) {
    const auto change = [this](std::optional<KeyId> key, bool isDown) {
        if (!key) {
            return; // a key the original has no number for
        }
        if (isDown) {
            press(*key);
        } else {
            release(*key);
        }
    };
    if (const auto* key = std::get_if<platform::KeyChanged>(&event)) {
        change(keyOf(key->key), key->isDown);
    } else if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event)) {
        change(keyOf(button->button), button->isDown);
    } else if (const auto* wheel = std::get_if<platform::MouseWheelMoved>(&event)) {
        // Each whole notch presses its key and releases it, as the original's wheel does.
        m_wheel += wheel->dy;
        for (; m_wheel >= 1.0F; m_wheel -= 1.0F) {
            press(kWheelUp);
            release(kWheelUp);
        }
        for (; m_wheel <= -1.0F; m_wheel += 1.0F) {
            press(kWheelDown);
            release(kWheelDown);
        }
    } else if (const auto* focus = std::get_if<platform::FocusChanged>(&event);
               focus != nullptr && !focus->isFocused) {
        releaseAll();
        m_wheel = 0.0F;
    }
}

void BoundInput::press(KeyId key) {
    const std::size_t index = indexOf(key);
    expects(index < kKeyCount, "BoundInput::press: a key of the original");
    if (m_down.test(index)) {
        return;
    }
    m_down.set(index);
    const std::string_view command = m_bindings->commandOf(key);
    if (command.starts_with('+')) {
        m_console->addText(std::format("{} {}", command, index));
    } else if (!command.empty()) {
        m_console->addText(command);
    }
}

void BoundInput::release(KeyId key) {
    const std::size_t index = indexOf(key);
    expects(index < kKeyCount, "BoundInput::release: a key of the original");
    if (!m_down.test(index)) {
        return; // it went down before the input was read here, or while another view had the keys
    }
    m_down.reset(index);
    if (const std::string_view command = m_bindings->commandOf(key); command.starts_with('+')) {
        m_console->addText(std::format("-{} {}", command.substr(1), index));
    }
}

void BoundInput::releaseAll() {
    for (std::uint32_t number = 0; number < kKeyCount; ++number) {
        release(KeyId{number});
    }
}

} // namespace nx::settings
