// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console: cvars, commands, aliases and buttons in one case-insensitive namespace, and the
/// command buffer that runs scripts with `wait` and `exec`, as the original does
/// (docs/12-interface-hud-e-console.md, section 5).
export module nx.console:console;

import std;
import nx.core;
import :text;
import :parser;
import :cvar;
import :button;

export namespace nx::console {

enum class ConsoleError : std::uint8_t {
    UnknownCommand,   ///< Not a command, alias or cvar; the detail suggests the closest name.
    UnknownCvar,      ///< A command that takes a cvar (toggle, incrementvar) got another name.
    InvalidValue,     ///< A cvar refused the text (cause: the CvarError).
    CheatProtected,   ///< A Cheat cvar, while cheats are forbidden.
    ServerProtected,  ///< A ServerProtected cvar: the console never changes it.
    InvalidArguments, ///< Wrong use of a command; the detail shows the usage.
    InvalidName,      ///< Registration of a command or alias with a name the console cannot invoke.
    NameTaken,        ///< Registration with the name of an existing command, alias or cvar.
    ExecFailed,       ///< exec could not read the file (cause: the ConfigFileError).
    ExpansionLimit,   ///< Too many alias expansions and execs in one run (a loop); the buffer is dropped.
    CommandFailed,    ///< A registered command failed in its own module (cause: that module's failure).
};

/// Where the console writes: replies (a cvar's value, echo) and the failures of the commands it
/// runs, which it never drops. The UI console, the server's terminal and tests implement it.
class ConsoleOutput {
public:
    ConsoleOutput() = default;
    ConsoleOutput(const ConsoleOutput&) = delete;
    ConsoleOutput(ConsoleOutput&&) = delete;
    auto operator=(const ConsoleOutput&) -> ConsoleOutput& = delete;
    auto operator=(ConsoleOutput&&) -> ConsoleOutput& = delete;
    virtual ~ConsoleOutput() = default;

    /// One line of reply.
    virtual void print(std::string_view line) = 0;
    virtual void reportFailure(const Failure<ConsoleError>& failure) = 0;
};

enum class ConfigFileError : std::uint8_t {
    NotFound,
    InvalidPath, ///< A path the file source does not serve (outside the game directories).
    Unreadable,
};

/// The files `exec` reads: the VFS in the game (M2), in-memory files in tests. Implementations
/// validate the path.
class ConfigFiles {
public:
    ConfigFiles() = default;
    ConfigFiles(const ConfigFiles&) = delete;
    ConfigFiles(ConfigFiles&&) = delete;
    auto operator=(const ConfigFiles&) -> ConfigFiles& = delete;
    auto operator=(ConfigFiles&&) -> ConfigFiles& = delete;
    virtual ~ConfigFiles() = default;

    [[nodiscard]] virtual auto read(std::string_view path) -> Result<std::string, ConfigFileError> = 0;
};

/// Whether the console may change Cheat cvars. The server follows its sv_cheats; a client follows
/// the server it is connected to.
enum class Cheats : std::uint8_t {
    Forbidden,
    Allowed,
};

/// What is left after Console::execute.
enum class BufferState : std::uint8_t {
    Empty,
    Waiting, ///< A `wait` stopped the run; the rest runs on the next execute().
};

class Console;

/// A command: runs with the words of its line. Failures go to the console output.
using CommandHandler =
    std::move_only_function<Result<void, ConsoleError>(Console& console, const CommandLine& line)>;

struct CommandDesc {
    ConsoleName name;
    std::string_view description{};
};

/// Alias expansions and execs allowed in one execute(). Far above any real script, which expands a
/// few per command; reaching it means an alias or a config that runs itself without a `wait`.
inline constexpr std::size_t kMaxExpansionsPerExecute = 1U << 16U;

/// Edits a mistyped name may be from a known one to be suggested ("did you mean").
inline constexpr std::size_t kMaxSuggestionDistance = 2;

/// The console of one process side (client or server). Single-threaded: it belongs to the thread
/// that runs it (the main thread between frames), and the simulation copies the cvar values it
/// needs at the start of a tick. Cvar, Button and the CvarBase pointers it hands out stay valid
/// for its whole life.
///
/// Built-in commands: alias, wait, exec, echo, toggle and incrementvar. Typing a cvar's name prints
/// its value; the name and one value set it.
class Console {
public:
    Console(ConsoleOutput& output, ConfigFiles& files);

    Console(const Console&) = delete;
    Console(Console&&) = delete;
    auto operator=(const Console&) -> Console& = delete;
    auto operator=(Console&&) -> Console& = delete;
    ~Console() = default;

    /// Registers a cvar with its default value. Errors: InvalidName, NameTaken, InvalidDefinition.
    template <CvarType Type>
    auto registerCvar(const CvarDesc<Type>& desc) -> Result<Cvar<Type>, CvarError> {
        const std::string_view name = desc.name.text();
        if (!isValidName(name)) {
            return fail(CvarError::InvalidName, std::format("\"{}\"", name));
        }
        if (isNameTaken(name)) {
            return fail(CvarError::NameTaken, std::string{name});
        }
        auto cvar = std::make_unique<TypedCvar<Type>>(desc);
        if (auto defined = cvar->checkDefinition(); !defined) {
            return std::unexpected(std::move(defined.error()));
        }
        TypedCvar<Type>& registered = *cvar;
        m_cvars.emplace(std::string{name}, std::move(cvar));
        return Cvar<Type>{registered};
    }

    /// Errors: InvalidName, NameTaken.
    auto registerCommand(const CommandDesc& desc, CommandHandler handler) -> Result<void, ConsoleError>;

    /// Registers `+name` and `-name`, which press and release the returned button; both take the
    /// key as an optional argument. Errors: InvalidName, NameTaken.
    auto registerButton(const CommandDesc& desc) -> Result<Button, ConsoleError>;

    /// The cvar called `name` (any case), or nullptr.
    [[nodiscard]] auto findCvar(std::string_view name) const -> const CvarBase*;

    /// The body of the alias called `name` (any case).
    [[nodiscard]] auto findAlias(std::string_view name) const -> std::optional<std::string_view>;

    [[nodiscard]] auto hasCommand(std::string_view name) const -> bool;

    /// Queues `script` after what is already buffered. Its end is the end of a command.
    void addText(std::string_view script);

    /// Queues `script` before what is buffered, to run next. Its end is the end of a command.
    void insertText(std::string_view script);

    /// Runs buffered commands until the buffer is empty or a `wait` stops the run; called once a
    /// frame. A failing command is reported and the run goes on with the next one.
    auto execute() -> BufferState;

    /// Sets a cvar from text, as typing `name text` does: refused for ServerProtected cvars, for
    /// Cheat cvars while cheats are forbidden, and for text the cvar does not accept.
    auto setCvar(std::string_view name, std::string_view text) -> Result<void, ConsoleError>;

    void setCheats(Cheats cheats) noexcept { m_cheats = cheats; }
    [[nodiscard]] auto cheats() const noexcept -> Cheats { return m_cheats; }

    /// Writes one line of reply to the output.
    void print(std::string_view line);

    /// One `name "value"` line per Archive cvar, sorted by name: what config.cfg saves.
    [[nodiscard]] auto archivedCvars() const -> std::string;

    /// Calls `function` with every cvar, sorted by name without regard to case.
    template <std::invocable<const CvarBase&> Function>
    void forEachCvar(Function&& function) const {
        for (const auto& [name, cvar] : m_cvars) {
            std::invoke(function, std::as_const(*cvar));
        }
    }

private:
    struct Command {
        std::string description;
        CommandHandler handler;
    };

    /// Buffered text and how much of it has run.
    struct Segment {
        std::string text;
        std::size_t cursor = 0;
    };

    void registerBuiltins();
    [[nodiscard]] auto isNameTaken(std::string_view name) const -> bool;
    [[nodiscard]] auto suggestion(std::string_view name) const -> std::string;

    void run(const CommandLine& line);
    /// Runs `script` next (an alias body, an exec'd file), counting the expansion.
    void expand(std::string_view script);
    void report(const Result<void, ConsoleError>& result);

    [[nodiscard]] auto requireCvar(std::string_view name) -> Result<CvarBase*, ConsoleError>;
    auto changeCvar(CvarBase& cvar, std::string_view text) -> Result<void, ConsoleError>;
    auto runCvar(CvarBase& cvar, const CommandLine& line) -> Result<void, ConsoleError>;

    auto alias(const CommandLine& line) -> Result<void, ConsoleError>;
    auto exec(const CommandLine& line) -> Result<void, ConsoleError>;
    auto echo(const CommandLine& line) -> Result<void, ConsoleError>;
    auto toggle(const CommandLine& line) -> Result<void, ConsoleError>;
    auto incrementVar(const CommandLine& line) -> Result<void, ConsoleError>;

    ConsoleOutput* m_output;
    ConfigFiles* m_files;
    std::map<std::string, Command, CaseInsensitiveLess> m_commands;
    std::map<std::string, std::string, CaseInsensitiveLess> m_aliases;
    std::map<std::string, std::unique_ptr<CvarBase>, CaseInsensitiveLess> m_cvars;
    std::deque<ButtonState> m_buttons; // a deque, so the Button handles never dangle
    std::deque<Segment> m_buffer;      // front runs first
    std::size_t m_expansions = 0;
    bool m_waitRequested = false;
    Cheats m_cheats = Cheats::Forbidden;
};

} // namespace nx::console
