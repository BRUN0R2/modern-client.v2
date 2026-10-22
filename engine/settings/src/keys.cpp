// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.settings;

import std;
import nx.core;
import nx.platform;
import nx.console;

namespace nx::settings {

namespace {

/// A key of the original that has a name, and its number.
struct KeyName {
    std::string_view name;
    std::uint8_t number = 0;
};

/// The original's names of keys, as its options list them. A key that types a printable character
/// is named by it, but `;`, which ends a command: SEMICOLON.
constexpr auto kKeyNames = std::to_array<KeyName>({
    {"TAB", 9},
    {"ENTER", 13},
    {"ESCAPE", 27},
    {"SPACE", 32},
    {"SEMICOLON", 59},
    {"BACKSPACE", 127},
    {"UPARROW", 128},
    {"DOWNARROW", 129},
    {"LEFTARROW", 130},
    {"RIGHTARROW", 131},
    {"ALT", 132},
    {"CTRL", 133},
    {"SHIFT", 134},
    {"F1", 135},
    {"F2", 136},
    {"F3", 137},
    {"F4", 138},
    {"F5", 139},
    {"F6", 140},
    {"F7", 141},
    {"F8", 142},
    {"F9", 143},
    {"F10", 144},
    {"F11", 145},
    {"F12", 146},
    {"INS", 147},
    {"DEL", 148},
    {"PGDN", 149},
    {"PGUP", 150},
    {"HOME", 151},
    {"END", 152},
    {"KP_HOME", 160},
    {"KP_UPARROW", 161},
    {"KP_PGUP", 162},
    {"KP_LEFTARROW", 163},
    {"KP_5", 164},
    {"KP_RIGHTARROW", 165},
    {"KP_END", 166},
    {"KP_DOWNARROW", 167},
    {"KP_PGDN", 168},
    {"KP_ENTER", 169},
    {"KP_INS", 170},
    {"KP_DEL", 171},
    {"KP_SLASH", 172},
    {"KP_MINUS", 173},
    {"KP_PLUS", 174},
    {"CAPSLOCK", 175},
    {"JOY1", 203},
    {"JOY2", 204},
    {"JOY3", 205},
    {"JOY4", 206},
    {"AUX1", 207},
    {"AUX2", 208},
    {"AUX3", 209},
    {"AUX4", 210},
    {"AUX5", 211},
    {"AUX6", 212},
    {"AUX7", 213},
    {"AUX8", 214},
    {"AUX9", 215},
    {"AUX10", 216},
    {"AUX11", 217},
    {"AUX12", 218},
    {"AUX13", 219},
    {"AUX14", 220},
    {"AUX15", 221},
    {"AUX16", 222},
    {"AUX17", 223},
    {"AUX18", 224},
    {"AUX19", 225},
    {"AUX20", 226},
    {"AUX21", 227},
    {"AUX22", 228},
    {"AUX23", 229},
    {"AUX24", 230},
    {"AUX25", 231},
    {"AUX26", 232},
    {"AUX27", 233},
    {"AUX28", 234},
    {"AUX29", 235},
    {"AUX30", 236},
    {"AUX31", 237},
    {"AUX32", 238},
    {"MWHEELDOWN", 239},
    {"MWHEELUP", 240},
    {"MOUSE1", 241},
    {"MOUSE2", 242},
    {"MOUSE3", 243},
    {"MOUSE4", 244},
    {"MOUSE5", 245},
    {"PAUSE", 255},
});

/// The ASCII character after the printable ones, which no name views: the original's BACKSPACE.
constexpr char kDeleteCharacter = '\x7F';

/// Every ASCII character at its own number: what a one-character name views.
constexpr auto kAscii = [] {
    std::array<char, 128> characters{};
    for (std::size_t number = 0; number < characters.size(); ++number) {
        characters[number] = static_cast<char>(number);
    }
    return characters;
}();

/// The number of the key called `name`, in any case.
[[nodiscard]] constexpr auto numberNamed(std::string_view name) noexcept -> std::optional<std::uint32_t> {
    for (const KeyName& key : kKeyNames) {
        if (equalsIgnoringCase(key.name, name)) {
            return key.number;
        }
    }
    return std::nullopt;
}

/// The key called `name`, which must be one of kKeyNames.
[[nodiscard]] consteval auto named(std::string_view name) -> console::KeyId {
    return console::KeyId{numberNamed(name).value()};
}

/// The key that types `character`.
[[nodiscard]] constexpr auto typing(char character) noexcept -> console::KeyId {
    return console::KeyId{static_cast<unsigned char>(character)};
}

/// The key `offset` keys after `first`.
[[nodiscard]] constexpr auto after(console::KeyId first, std::uint32_t offset) noexcept -> console::KeyId {
    return console::KeyId{std::to_underlying(first) + offset};
}

} // namespace

auto keyOf(platform::Key key) noexcept -> std::optional<console::KeyId> {
    using platform::Key;
    const auto offset = [key](Key first) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(std::to_underlying(key) - std::to_underlying(first));
    };
    if (key >= Key::A && key <= Key::Z) {
        return after(typing('a'), offset(Key::A));
    }
    if (key >= Key::Digit1 && key <= Key::Digit9) {
        return after(typing('1'), offset(Key::Digit1));
    }
    if (key >= Key::F1 && key <= Key::F12) {
        return after(named("F1"), offset(Key::F1));
    }
    switch (key) {
    case Key::Digit0:
        return typing('0');
    case Key::Enter:
        return named("ENTER");
    case Key::Escape:
        return named("ESCAPE");
    case Key::Backspace:
        return named("BACKSPACE");
    case Key::Tab:
        return named("TAB");
    case Key::Space:
        return named("SPACE");
    case Key::Minus:
        return typing('-');
    case Key::Equals:
        return typing('=');
    case Key::LeftBracket:
        return typing('[');
    case Key::RightBracket:
        return typing(']');
    case Key::Backslash:
        return typing('\\');
    case Key::Semicolon:
        return typing(';');
    case Key::Apostrophe:
        return typing('\'');
    case Key::Grave:
        return typing('`');
    case Key::Comma:
        return typing(',');
    case Key::Period:
        return typing('.');
    case Key::Slash:
        return typing('/');
    case Key::CapsLock:
        return named("CAPSLOCK");
    case Key::Pause:
        return named("PAUSE");
    case Key::Insert:
        return named("INS");
    case Key::Home:
        return named("HOME");
    case Key::PageUp:
        return named("PGUP");
    case Key::Delete:
        return named("DEL");
    case Key::End:
        return named("END");
    case Key::PageDown:
        return named("PGDN");
    case Key::Right:
        return named("RIGHTARROW");
    case Key::Left:
        return named("LEFTARROW");
    case Key::Down:
        return named("DOWNARROW");
    case Key::Up:
        return named("UPARROW");
    case Key::KeypadDivide:
        return named("KP_SLASH");
    case Key::KeypadMultiply:
        return typing('*');
    case Key::KeypadMinus:
        return named("KP_MINUS");
    case Key::KeypadPlus:
        return named("KP_PLUS");
    case Key::KeypadEnter:
        return named("KP_ENTER");
    case Key::Keypad1:
        return named("KP_END");
    case Key::Keypad2:
        return named("KP_DOWNARROW");
    case Key::Keypad3:
        return named("KP_PGDN");
    case Key::Keypad4:
        return named("KP_LEFTARROW");
    case Key::Keypad5:
        return named("KP_5");
    case Key::Keypad6:
        return named("KP_RIGHTARROW");
    case Key::Keypad7:
        return named("KP_HOME");
    case Key::Keypad8:
        return named("KP_UPARROW");
    case Key::Keypad9:
        return named("KP_PGUP");
    case Key::Keypad0:
        return named("KP_INS");
    case Key::KeypadPeriod:
        return named("KP_DEL");
    case Key::LeftControl:
    case Key::RightControl:
        return named("CTRL");
    case Key::LeftShift:
    case Key::RightShift:
        return named("SHIFT");
    case Key::LeftAlt:
    case Key::RightAlt:
        return named("ALT");
    default:
        return std::nullopt;
    }
}

auto keyOf(platform::MouseButton button) noexcept -> std::optional<console::KeyId> {
    using platform::MouseButton;
    switch (button) {
    case MouseButton::Left:
        return named("MOUSE1");
    case MouseButton::Right:
        return named("MOUSE2");
    case MouseButton::Middle:
        return named("MOUSE3");
    case MouseButton::X1:
        return named("MOUSE4");
    case MouseButton::X2:
        return named("MOUSE5");
    default:
        return std::nullopt;
    }
}

auto keyNamed(std::string_view name) noexcept -> std::optional<console::KeyId> {
    if (name.size() == 1) {
        // A character names the key that types it, when that key has a name to be written back with.
        const char character = name.front();
        if (std::size_t{static_cast<unsigned char>(character)} >= kAscii.size()) {
            return std::nullopt;
        }
        const console::KeyId key = typing(toLowerAscii(character));
        return nameOf(key) ? std::optional{key} : std::nullopt;
    }
    return numberNamed(name).transform([](std::uint32_t number) { return console::KeyId{number}; });
}

auto nameOf(console::KeyId key) noexcept -> std::optional<std::string_view> {
    const std::uint32_t number = std::to_underlying(key);
    const bool isPrintable = number > std::uint32_t{' '} && number < std::uint32_t{kDeleteCharacter};
    if (isPrintable && number != std::uint32_t{'"'} && number != std::uint32_t{';'}) {
        return std::string_view{&kAscii[number], 1};
    }
    for (const KeyName& entry : kKeyNames) {
        if (entry.number == number) {
            return entry.name;
        }
    }
    return std::nullopt;
}

} // namespace nx::settings
