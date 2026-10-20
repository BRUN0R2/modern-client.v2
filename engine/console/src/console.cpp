// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

namespace {

/// Editors such as Notepad start UTF-8 files with it; it is not part of the first command.
constexpr std::string_view kUtf8ByteOrderMark = "\xEF\xBB\xBF";

/// The words separated by single spaces.
[[nodiscard]] auto joined(std::span<const std::string> words) -> std::string {
    std::string text;
    for (std::size_t index = 0; index < words.size(); ++index) {
        if (index > 0) {
            text += ' ';
        }
        text += words[index];
    }
    return text;
}

/// The key argument of `+name [key]` and `-name [key]`.
[[nodiscard]] auto buttonKey(const CommandLine& line) -> Result<std::optional<KeyId>, ConsoleError> {
    const auto arguments = line.arguments();
    if (arguments.empty()) {
        return std::optional<KeyId>{};
    }
    const auto key = detail::parseInteger(arguments.front());
    if (arguments.size() > 1 || !key || *key < 0) {
        return fail(ConsoleError::InvalidArguments, std::format("usage: {} [key number]", line.name()));
    }
    return std::optional{KeyId{static_cast<std::uint32_t>(*key)}};
}

} // namespace

Console::Console(ConsoleOutput& output, ConfigFiles& files) : m_output{&output}, m_files{&files} {
    registerBuiltins();
}

void Console::registerBuiltins() {
    const auto add = [this](const CommandDesc& desc, CommandHandler handler) {
        const auto registered = registerCommand(desc, std::move(handler));
        invariant(registered.has_value(), "Console: a built-in command failed to register");
    };
    add({.name = "alias", .description = "alias <name> <commands>: a command that runs other commands"},
        [](Console& console, const CommandLine& line) { return console.alias(line); });
    add({.name = "wait", .description = "Runs the rest of the buffered commands on the next frame"},
        [](Console& console, const CommandLine&) -> Result<void, ConsoleError> {
            console.m_waitRequested = true;
            return {};
        });
    add({.name = "exec", .description = "exec <file>: runs the commands of a config file"},
        [](Console& console, const CommandLine& line) { return console.exec(line); });
    add({.name = "echo", .description = "echo <text>: prints the text"},
        [](Console& console, const CommandLine& line) { return console.echo(line); });
    add({.name = "toggle",
         .description = "toggle <cvar> [values...]: switches between 0 and 1, or steps through the values"},
        [](Console& console, const CommandLine& line) { return console.toggle(line); });
    add({.name = "incrementvar",
         .description =
             "incrementvar <cvar> <minimum> <maximum> <step>: adds the step, wrapping around the range"},
        [](Console& console, const CommandLine& line) { return console.incrementVar(line); });
}

auto Console::registerCommand(const CommandDesc& desc, CommandHandler handler) -> Result<void, ConsoleError> {
    expects(static_cast<bool>(handler), "Console::registerCommand: the handler is empty");
    const std::string_view name = desc.name.text();
    if (!isValidName(name)) {
        return fail(ConsoleError::InvalidName, std::format("\"{}\"", name));
    }
    if (isNameTaken(name)) {
        return fail(ConsoleError::NameTaken, std::string{name});
    }
    m_commands.emplace(std::string{name},
                       Command{.description = std::string{desc.description}, .handler = std::move(handler)});
    return {};
}

auto Console::registerButton(const CommandDesc& desc) -> Result<Button, ConsoleError> {
    const std::string_view name = desc.name.text();
    const std::string press = std::format("+{}", name);
    const std::string release = std::format("-{}", name);
    if (!isValidName(name)) {
        return fail(ConsoleError::InvalidName, std::format("\"{}\"", name));
    }
    if (isNameTaken(press) || isNameTaken(release)) {
        return fail(ConsoleError::NameTaken, std::format("{} or {}", press, release));
    }

    ButtonState& state = m_buttons.emplace_back();
    const auto add = [this](std::string_view commandName, std::string_view description,
                            CommandHandler handler) {
        const auto registered =
            registerCommand({.name = commandName, .description = description}, std::move(handler));
        invariant(registered.has_value(), "Console::registerButton: a checked name failed to register");
    };
    add(press, desc.description, [&state](Console&, const CommandLine& line) -> Result<void, ConsoleError> {
        return buttonKey(line).transform([&state](std::optional<KeyId> key) { state.press(key); });
    });
    add(release, desc.description, [&state](Console&, const CommandLine& line) -> Result<void, ConsoleError> {
        return buttonKey(line).transform([&state](std::optional<KeyId> key) { state.release(key); });
    });
    return Button{state};
}

auto Console::findCvar(std::string_view name) const -> const CvarBase* {
    const auto found = m_cvars.find(name);
    return found == m_cvars.end() ? nullptr : found->second.get();
}

auto Console::findAlias(std::string_view name) const -> std::optional<std::string_view> {
    const auto found = m_aliases.find(name);
    return found == m_aliases.end() ? std::nullopt : std::optional<std::string_view>{found->second};
}

auto Console::hasCommand(std::string_view name) const -> bool {
    return m_commands.contains(name);
}

auto Console::isNameTaken(std::string_view name) const -> bool {
    return m_commands.contains(name) || m_aliases.contains(name) || m_cvars.contains(name);
}

void Console::addText(std::string_view script) {
    m_buffer.push_back(Segment{.text = std::string{script}});
}

void Console::insertText(std::string_view script) {
    m_buffer.push_front(Segment{.text = std::string{script}});
}

auto Console::execute() -> BufferState {
    m_expansions = 0;
    while (!m_buffer.empty()) {
        Segment& segment = m_buffer.front();
        const std::string_view pending = std::string_view{segment.text}.substr(segment.cursor);
        if (pending.empty()) {
            m_buffer.pop_front();
            continue;
        }
        const auto [command, remainder] = splitFirstCommand(pending);
        const CommandLine line = tokenize(command);
        segment.cursor = segment.text.size() - remainder.size();
        if (segment.cursor == segment.text.size()) {
            m_buffer.pop_front(); // fully read: gone before its last command inserts anything
        }

        // Running may insert segments in front (aliases, exec) or drop the whole buffer.
        if (!line.words.empty()) {
            run(line);
        }
        if (m_waitRequested) {
            m_waitRequested = false;
            break;
        }
    }
    return m_buffer.empty() ? BufferState::Empty : BufferState::Waiting;
}

void Console::run(const CommandLine& line) {
    const std::string_view name = line.name();
    if (const auto command = m_commands.find(name); command != m_commands.end()) {
        report(command->second.handler(*this, line));
        return;
    }
    if (const auto body = m_aliases.find(name); body != m_aliases.end()) {
        // Arguments after an alias name are ignored, as in the original.
        expand(body->second);
        return;
    }
    if (const auto cvar = m_cvars.find(name); cvar != m_cvars.end()) {
        report(runCvar(*cvar->second, line));
        return;
    }
    report(fail(ConsoleError::UnknownCommand, suggestion(name)));
}

void Console::expand(std::string_view script) {
    if (++m_expansions > kMaxExpansionsPerExecute) {
        m_buffer.clear();
        report(fail(ConsoleError::ExpansionLimit,
                    std::format("more than {} alias expansions and execs in one run: an alias or config runs "
                                "itself without a wait; the buffered commands were dropped",
                                kMaxExpansionsPerExecute)));
        return;
    }
    insertText(script);
}

void Console::report(const Result<void, ConsoleError>& result) {
    if (!result) {
        m_output->reportFailure(result.error());
    }
}

void Console::print(std::string_view line) {
    m_output->print(line);
}

auto Console::archivedCvars() const -> std::string {
    std::string config;
    for (const auto& [name, cvar] : m_cvars) {
        if (cvar->flags().test(CvarFlag::Archive)) {
            config += std::format("{} \"{}\"\n", cvar->name(), cvar->text());
        }
    }
    return config;
}

auto Console::suggestion(std::string_view name) const -> std::string {
    std::string_view closest;
    std::size_t closestDistance = kMaxSuggestionDistance + 1;
    const auto consider = [&](std::string_view candidate) {
        if (const std::size_t distance = editDistance(name, candidate); distance < closestDistance) {
            closest = candidate;
            closestDistance = distance;
        }
    };
    for (const auto& [candidate, command] : m_commands) {
        consider(candidate);
    }
    for (const auto& [candidate, body] : m_aliases) {
        consider(candidate);
    }
    for (const auto& [candidate, cvar] : m_cvars) {
        consider(candidate);
    }
    if (closest.empty()) {
        return std::format("unknown command \"{}\"", name);
    }
    return std::format("unknown command \"{}\"; did you mean \"{}\"?", name, closest);
}

auto Console::requireCvar(std::string_view name) -> Result<CvarBase*, ConsoleError> {
    const auto found = m_cvars.find(name);
    if (found == m_cvars.end()) {
        return fail(ConsoleError::UnknownCvar, std::format("unknown cvar \"{}\"", name));
    }
    return found->second.get();
}

auto Console::setCvar(std::string_view name, std::string_view text) -> Result<void, ConsoleError> {
    return requireCvar(name).and_then([this, text](CvarBase* cvar) { return changeCvar(*cvar, text); });
}

auto Console::changeCvar(CvarBase& cvar, std::string_view text) -> Result<void, ConsoleError> {
    if (cvar.flags().test(CvarFlag::ServerProtected)) {
        return fail(
            ConsoleError::ServerProtected,
            std::format("{} is controlled by the server; the console does not change it", cvar.name()));
    }
    if (cvar.flags().test(CvarFlag::Cheat) && m_cheats == Cheats::Forbidden) {
        return fail(ConsoleError::CheatProtected,
                    std::format("{} changes only while cheats are allowed (sv_cheats 1)", cvar.name()));
    }
    if (auto assigned = cvar.assign(text); !assigned) {
        std::string detail = assigned.error().detail;
        return fail(ConsoleError::InvalidValue, std::move(detail), std::move(assigned.error()));
    }
    return {};
}

auto Console::runCvar(CvarBase& cvar, const CommandLine& line) -> Result<void, ConsoleError> {
    const auto arguments = line.arguments();
    if (arguments.empty()) {
        print(std::format("\"{}\" is \"{}\"", cvar.name(), cvar.text()));
        return {};
    }
    if (arguments.size() > 1) {
        return fail(ConsoleError::InvalidArguments,
                    std::format("{} takes one value; quote a value with spaces: {} \"{}\"", cvar.name(),
                                cvar.name(), line.argumentText));
    }
    return changeCvar(cvar, arguments.front());
}

auto Console::alias(const CommandLine& line) -> Result<void, ConsoleError> {
    const auto arguments = line.arguments();
    if (arguments.empty()) {
        for (const auto& [name, body] : m_aliases) {
            print(std::format("{} : {}", name, body));
        }
        return {};
    }
    const std::string& name = arguments.front();
    if (!isValidName(name)) {
        return fail(ConsoleError::InvalidName, std::format("alias \"{}\"", name));
    }
    if (m_commands.contains(name) || m_cvars.contains(name)) {
        return fail(ConsoleError::NameTaken, std::format("alias {}: a command or cvar has that name", name));
    }
    // The body is the rest of the words joined by spaces; `alias name` alone defines an empty alias.
    m_aliases.insert_or_assign(name, joined(arguments.subspan(1)));
    return {};
}

auto Console::exec(const CommandLine& line) -> Result<void, ConsoleError> {
    const auto arguments = line.arguments();
    if (arguments.size() != 1) {
        return fail(ConsoleError::InvalidArguments, "usage: exec <file>");
    }
    const std::string& path = arguments.front();
    auto content = m_files->read(path);
    if (!content) {
        return fail(ConsoleError::ExecFailed, std::format("exec {}", path), std::move(content.error()));
    }
    print(std::format("execing {}", path));
    std::string_view script = *content;
    if (script.starts_with(kUtf8ByteOrderMark)) {
        script.remove_prefix(kUtf8ByteOrderMark.size());
    }
    expand(script);
    return {};
}

auto Console::echo(const CommandLine& line) -> Result<void, ConsoleError> {
    print(joined(line.arguments()));
    return {};
}

auto Console::toggle(const CommandLine& line) -> Result<void, ConsoleError> {
    const auto arguments = line.arguments();
    if (arguments.empty()) {
        return fail(ConsoleError::InvalidArguments, "usage: toggle <cvar> [values...]");
    }
    return requireCvar(arguments.front()).and_then([&](CvarBase* cvar) {
        const std::string current = cvar->text();
        const auto values = arguments.subspan(1);
        if (values.empty()) {
            return changeCvar(*cvar, current == "0" ? "1" : "0");
        }
        // The value after the current one, or the first when the current one is not listed. Values
        // compare as the cvar writes them (0.5, not .50).
        const auto found = std::ranges::find(values, current);
        const auto next =
            found == values.end() || std::next(found) == values.end() ? values.begin() : std::next(found);
        return changeCvar(*cvar, *next);
    });
}

auto Console::incrementVar(const CommandLine& line) -> Result<void, ConsoleError> {
    constexpr std::size_t kNumberCount = 3; // minimum, maximum, step
    const auto arguments = line.arguments();
    const auto usage = [] {
        return fail(ConsoleError::InvalidArguments, "usage: incrementvar <cvar> <minimum> <maximum> <step>");
    };
    if (arguments.size() != kNumberCount + 1) {
        return usage();
    }
    std::array<std::optional<float>, kNumberCount> numbers{};
    std::ranges::transform(arguments.subspan(1), numbers.begin(),
                           [](const std::string& text) { return detail::parseFloat(text); });
    const auto [minimum, maximum, step] = numbers;
    if (!minimum || !maximum || !step) {
        return usage();
    }
    auto found = requireCvar(arguments.front());
    if (!found) {
        return std::unexpected(std::move(found.error()));
    }
    CvarBase& cvar = **found;
    if (cvar.kind() != CvarKind::Integer && cvar.kind() != CvarKind::Float) {
        return fail(ConsoleError::InvalidArguments,
                    std::format("incrementvar: {} is not a number", cvar.name()));
    }
    const auto current = detail::parseFloat(cvar.text());
    invariant(current.has_value(), "incrementvar: a numeric cvar wrote a value it cannot read");
    float next = current.value_or(*minimum) + *step;
    if (next > *maximum) {
        next = *minimum;
    } else if (next < *minimum) {
        next = *maximum;
    }
    return changeCvar(cvar, std::format("{}", next));
}

} // namespace nx::console
