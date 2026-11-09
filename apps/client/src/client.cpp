// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// stderr: a macro, which `import std;` does not export.
#include <cstdio>

module app.client;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.ecs;
import nx.agents;
import nx.platform;
import nx.console;
import nx.assets;
import nx.settings;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import nx.ui;
import cs.shared;
import cs.server;
import cs.client;
import cs.bots;
import mod.zombies;

namespace app {

namespace {

using nx::console::ConsoleError;

constexpr nx::log::Category kLog{"client"};

/// The folder of the data that holds the project's own fonts.
constexpr std::string_view kFontsFolder = "fonts";
/// How long a minimized window waits before it looks for events again.
constexpr std::chrono::milliseconds kMinimizedWait{50};
/// The most of a frame the game's ticks take: a game that cannot keep up with its ticks lets go of the rest
/// of the time it is behind, and plays slower, rather than its frames each take as long as all the ticks they
/// fell behind by, which fall further behind and end at a few frames a second.
constexpr std::chrono::milliseconds kMostTickingInAFrame{33};
/// A sound that could not be played is reported once in this long, with how many others were not.
constexpr std::chrono::seconds kSoundFailureInterval{5};
/// The stream of random numbers that chooses which decal a bullet leaves when its mark has several, as
/// the original's game chooses it among them.
constexpr std::string_view kBulletHoleStream = "bulletHoles";
/// The console's output: replies on the standard output, failures on the standard error, and both in
/// the console's window once there is one.
class PrintedOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override {
        std::println("{}", line);
        if (window != nullptr) {
            window->print(line);
        }
    }
    void reportFailure(const nx::Failure<ConsoleError>& failure) override {
        std::println(stderr, "next: {}", nx::describe(failure));
        if (window != nullptr) {
            window->reportFailure(failure);
        }
    }

    nx::ui::ConsoleDialog* window = nullptr; ///< The client's, when it has one.
};

/// The original's picture as the video cvars set it.
[[nodiscard]] auto gammaOf(const nx::settings::VideoCvars& video) -> nx::render::GammaSettings {
    return {.gamma = video.gamma.value(),
            .brightness = video.brightness.value(),
            .textureGamma = video.textureGamma.value(),
            .lightGamma = video.lightGamma.value()};
}

/// The most decals on faces the video cvars keep: `r_decals`, never negative.
[[nodiscard]] auto decalLimitOf(const nx::settings::VideoCvars& video) -> std::size_t {
    return nx::narrow<std::size_t>(video.decals.value());
}

/// `name` as the original's `play` reads it: a WAV file's when it has no extension.
[[nodiscard]] auto soundFileOf(std::string_view name) -> std::string {
    const std::size_t dot = name.rfind('.');
    const std::size_t separator = name.find_last_of("/\\");
    const bool hasExtension =
        dot != std::string_view::npos && (separator == std::string_view::npos || dot > separator);
    return hasExtension ? std::string{name} : std::format("{}.wav", name);
}

/// Turns `view` by the mouse's `moved` as the original's mouse does: sensitivity times m_yaw degrees
/// a count across, sensitivity times m_pitch down, up to the pitch the view stops at.
void look(nx::EulerAngles& view, const nx::platform::MouseMoved& moved,
          const nx::settings::MouseCvars& mouse) {
    const float sensitivity = mouse.sensitivity.value();
    view.yaw = nx::normalizeAngle(view.yaw - nx::Degrees{moved.dx * sensitivity * mouse.yaw.value()});
    view.pitch = nx::Degrees{std::clamp(view.pitch.value() + (moved.dy * sensitivity * mouse.pitch.value()),
                                        -cs::kMaxPitch.value(), cs::kMaxPitch.value())};
}

/// The client: the player's settings and console, the map it plays, and the window and GPU it plays
/// it in. It stays where it is made: the console's commands and the binds' input point into it.
class Client {
public:
    Client(const ClientOptions& options, const std::filesystem::path& userFolder)
        : m_options{&options}, m_loggerScope{m_logger}, m_files{userFolder}, m_console{m_output, m_files},
          m_bindings{nx::settings::defaultBindings()}, m_keys{m_console, m_bindings} {
        // What is logged goes where it always went, and to the console's window.
        auto logSink = std::make_unique<nx::ui::ConsoleLogSink>();
        m_logSink = logSink.get();
        m_logger.addSink(std::make_unique<nx::log::ConsoleSink>());
        m_logger.addSink(std::move(logSink));
    }

    Client(const Client&) = delete;
    auto operator=(const Client&) -> Client& = delete;
    Client(Client&&) = delete;
    auto operator=(Client&&) -> Client& = delete;
    ~Client() {
        m_output.window = nullptr; // the window goes before the console's output
        if (m_video) {
            m_video->device->waitIdle(); // what the GPU still draws goes with the scene, however it ends
        }
    }

    /// Registers the cvars and commands, opens the game's content, runs the player's files and the
    /// command line's commands, then opens the window and the GPU.
    [[nodiscard]] auto start() -> nx::Result<void, ClientError>;

    /// Plays frame after frame until the client ends, then writes config.cfg.
    [[nodiscard]] auto run() -> nx::Result<void, ClientError>;

private:
    [[nodiscard]] auto registerCommands() -> nx::Result<void, ClientError>;
    /// Registers the weapon commands (cs::registerWeaponCommands) over the game's content.
    [[nodiscard]] auto registerWeaponCommands() -> nx::Result<void, ClientError>;
    [[nodiscard]] auto mapCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError>;
    /// Builds the agents' mesh of the map played again, what the cache keeps of it left aside, and plays
    /// the map again on it from the next frame (nav_generate), as Condition Zero restarts the game once it
    /// analyzed a map. Errors: InvalidArguments, CommandFailed.
    [[nodiscard]] auto generateNavigationCommand(const nx::console::CommandLine& line)
        -> nx::Result<void, ConsoleError>;
    /// The map loaded last, as the commands that follow `map` on a line act on it; none before the first.
    [[nodiscard]] auto playedSession() const noexcept -> Session*;
    /// The navigation profiles of data/cstrike/navigation.toml. Errors: CommandFailed.
    [[nodiscard]] auto readNavigation() const -> nx::Result<nx::game::NavigationData, ConsoleError>;
    [[nodiscard]] auto playCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError>;
    /// Prints the names of the sound outputs, which s_device takes. Errors: CommandFailed.
    [[nodiscard]] auto listSoundOutputs() -> nx::Result<void, ConsoleError>;
    [[nodiscard]] auto playVolumeCommand(const nx::console::CommandLine& line)
        -> nx::Result<void, ConsoleError>;
    /// Plays the sound `name` names at `volume`, heard everywhere, as `play` and `playvol` do.
    [[nodiscard]] auto playSound(std::string_view name, float volume) -> nx::Result<void, ConsoleError>;
    void noclipCommand();
    void lastWeaponCommand();
    /// Opens the console's window, or closes it.
    void toggleConsole();
    /// The cursor shows while the console's window or the buy menu is open; else the mouse turns the view.
    void showCursorAsNeeded();
    /// Whether the buy menu, open, takes `event`.
    [[nodiscard]] auto isTakenByBuyMenu(const nx::platform::WindowEvent& event) -> bool;
    /// Opens the console's window and cl_showfps's label for a screen `screenHeight` pixels tall, in the
    /// fonts of the installation's windows; one that cannot be had is logged, and the client goes on
    /// without it.
    void openWindowText(std::uint32_t screenHeight);
    [[nodiscard]] auto readInput() -> nx::Result<void, ClientError>;
    /// What `event` does in the console's window, open.
    void handleInConsole(const nx::platform::WindowEvent& event);
    /// Plays the map the console asked for, if any: its scene is made with the next frame, and
    /// r_decals is held as entering a game of more than one player holds it.
    void startNextSession();
    /// Plays the sound on the output s_device names, when it names one: the system's default stays
    /// when none has that name, logged.
    void openNamedSoundOutput();
    /// Plays `elapsed` of the session's game, as a listen server plays it: each tick due, the command the
    /// player's input makes is the server's game's tick, whose snapshot the client's game takes.
    void playSession(nx::Seconds elapsed);
    /// Turns the view the way the session's game turned its player, if it did (the original's
    /// fixangle): as the player spawns.
    void turnAsTheGameDid();
    /// Reads, before the session plays the frame, the view models of the weapons made or changed since
    /// (cs::Level::updateViewModels), which upload puts on the GPU, and their sounds.
    void updateWeapons();
    /// Reads the sounds the player hears of `weapons` in hand (cs::soundFilesOf), so that no frame
    /// waits on a file of one it plays: those of every weapon as a session starts, those of a weapon
    /// as it is made or changed. One that cannot be read is logged, and the game plays on.
    void readWeaponSounds(std::span<const cs::WeaponIndex> weapons);
    /// Reads the sounds of the player's feet, breath, pain, death, use and purchases
    /// (cs::FootstepRules::soundFiles, cs::BreathRules::soundFiles, cs::PlayerHarmRules::soundFiles,
    /// cs::UseRules::soundFiles, cs::BuyingRules::soundFiles) as a session starts, so that none waits on its
    /// file. One that cannot be read is logged, and the game plays on.
    void readPlayerSounds();
    /// What tells the player what happened in the frame played.
    [[nodiscard]] auto frameNotices() noexcept -> FrameNotices;
    /// Records the frame begun in `list`, and hands it to the GPU to show.
    [[nodiscard]] auto drawFrame(nx::rhi::CommandList& list) -> nx::Result<void, ClientError>;
    [[nodiscard]] auto upload(nx::rhi::CommandList& list, nx::rhi::Format format)
        -> nx::Result<void, ClientError>;
    /// What the session's scene shows of the frame: the player's view, turned by the kick its weapon
    /// gave it, the brush entities where the game puts them, the weapon in hand, and what nav_draw draws.
    [[nodiscard]] auto sessionView() -> cs::SceneView;
    /// The camera the session's scene is drawn from: the player's view turned by its weapon's kick.
    [[nodiscard]] auto kickedCamera() const -> nx::render::Camera;
    /// The screen's pictures over the frame, of `size`: the HUD, cl_showfps's label, and the console's
    /// window when open.
    void appendScreen(nx::rhi::Extent2d size);
    [[nodiscard]] auto finish() -> nx::Result<void, ClientError>;

    const ClientOptions* m_options;
    nx::log::Logger m_logger;                    ///< The client's, the console's window among its sinks.
    nx::ui::ConsoleLogSink* m_logSink = nullptr; ///< m_logger's.
    nx::log::DefaultLoggerScope m_loggerScope;   ///< m_logger installed while the client lives.
    PrintedOutput m_output;
    nx::settings::UserConfigFiles m_files;
    nx::console::Console m_console;
    nx::settings::KeyBindings m_bindings;
    nx::settings::BoundInput m_keys;
    std::optional<ClientSettings> settings; ///< Registered as the client starts.
    cs::FogControl m_fog;                   ///< The fog the frames show, as the fog's cvars ask for it.
    NavigationView navigationView;          ///< What nav_draw draws over the scene.
    /// Read as the client starts: each map's agents' mesh is built by it as the map loads.
    nx::Result<nx::game::NavigationData, ConsoleError> navigationData{};
    std::optional<cs::GameContent> m_content;
    std::filesystem::path dataFolder;
    /// The maps' meshes are built in it as they load, and their agents decide in it each tick.
    std::unique_ptr<nx::jobs::JobSystem> m_jobs;
    std::optional<ClientBots> bots; ///< Their rules read as the client starts, so that a frame reads nothing.
    std::vector<cs::ShownEntity> shownEntities;
    std::unique_ptr<cs::ClientAudio> m_audio; ///< None without a sound output: the game plays on, silent.
    std::optional<cs::SceneShaders> m_shaders;
    std::optional<cs::FrameShaders> m_frameShaders; ///< Until the frame renderer is made of them.
    std::unique_ptr<Session> m_session;             ///< The map played.
    std::unique_ptr<Session> m_nextSession;         ///< The map `map` loaded, played from the next frame.
    /// Made or changed since the scene put their view models on the GPU.
    std::vector<cs::WeaponIndex> m_changedWeapons;
    nx::log::RateLimit m_soundFailures{kSoundFailureInterval};
    nx::Random m_bulletHoles{0, kBulletHoleStream};
    std::optional<Video> m_video;
    std::optional<cs::Scene> m_scene;          ///< Of the session's level, made with the first image of it.
    std::optional<cs::FrameRenderer> m_frames; ///< What draws the frames, made with the first image.
    std::optional<cs::Hud> m_hud;              ///< None when its pictures could not be read.
    std::vector<nx::render::ScreenQuad> m_screenQuads;    ///< What the screen draws in a frame.
    std::vector<nx::render::ModelPlacement> m_placements; ///< Where the frame draws the brush entities.
    std::vector<cs::ShownHostage> m_hostages;             ///< How the frame shows the game's hostages.
    std::vector<cs::ShownPlayer> m_players;               ///< How the frame shows the other players.
    nx::ui::FontCatalog m_fonts;
    std::unique_ptr<cs::ConsoleWindow> m_consoleWindow; ///< None when the console has no window.
    std::optional<cs::FpsLabel> m_fpsLabel;             ///< None when its font could not be had.
    std::optional<cs::DamageNumbers> m_damageNumbers;   ///< None when its font could not be had.
    std::optional<cs::StepIndicator> m_stepIndicator;   ///< Once the window's text is open.
    std::optional<cs::KillFeed> killFeed;     ///< None when its font or its pictures could not be had.
    std::optional<cs::Scoreboard> scoreboard; ///< None when its font could not be had.
    std::optional<ClientBuyMenu> buyMenu;     ///< Made with the game's content.
    bool isCursorShown = false;
    nx::platform::FrameRateMeter m_frameRate; ///< Of the frames shown, which cl_showfps shows.
    std::vector<nx::platform::WindowEvent> m_events;
    nx::render::Camera m_camera{};
    nx::platform::Stopwatch m_frameClock;
    nx::platform::Stopwatch m_mapClock; ///< Since the map started: the light styles' time.
    nx::platform::Instant m_frameStart = nx::platform::MonotonicClock::now();
    bool m_isRunning = true;
    bool m_isFocused = true; ///< The window is, as it opens.
};

/// The installation `options` name (`-basedir`), else the content configuration's
/// (nx::assets::findContentConfig, from the working folder and `userFolder`); none when neither
/// names one, for the one Steam installed. Errors: those of the configuration's finding and reading.
[[nodiscard]] auto configuredInstall(const ClientOptions& options, const std::filesystem::path& userFolder)
    -> nx::Result<std::optional<std::filesystem::path>, nx::assets::ContentError> {
    if (options.install) {
        return options.install;
    }
    // Empty when it cannot be told, so that content.toml is looked for relative to it all the same.
    std::error_code error;
    const std::filesystem::path workingFolder = std::filesystem::current_path(error);
    auto file = nx::assets::findContentConfig(options.contentConfig, workingFolder, userFolder);
    if (!file) {
        return std::unexpected(std::move(file.error()));
    }
    if (!*file) {
        return std::optional<std::filesystem::path>{};
    }
    const std::string name = nx::platform::toUtf8(**file);
    auto text = nx::platform::readTextFile(**file);
    if (!text) {
        return nx::fail(nx::assets::ContentError::ConfigInvalid, name, std::move(text.error()));
    }
    auto config = nx::assets::readContentConfig(*text, name);
    if (!config) {
        return std::unexpected(std::move(config.error()));
    }
    nx::log::info(kLog, "the content configuration: {}", name);
    if (!config->goldsrcInstall || config->goldsrcInstall->path.empty()) {
        return std::optional<std::filesystem::path>{};
    }
    return std::optional{nx::platform::fromUtf8(config->goldsrcInstall->path)};
}

auto Client::start() -> nx::Result<void, ClientError> {
    auto clientSettings = registerClientSettings(m_console, m_bindings);
    if (!clientSettings) {
        return std::unexpected{std::move(clientSettings.error())};
    }
    settings.emplace(std::move(*clientSettings));
    if (auto registered = registerCommands(); !registered) {
        return registered;
    }
    auto executable = nx::platform::executablePath();
    if (!executable) {
        return nx::fail(ClientError::ContentUnavailable, std::move(executable.error()));
    }
    auto install = configuredInstall(*m_options, m_files.directory());
    if (!install) {
        return nx::fail(ClientError::ContentUnavailable, std::move(install.error()));
    }
    const std::filesystem::path data = m_options->data.value_or(executable->parent_path() / "data");
    dataFolder = data;
    auto jobs = nx::jobs::JobSystem::create();
    nx::invariant(jobs.has_value(), "Client: the default worker count is within the Job System's limit");
    m_jobs = std::move(jobs).value_or(nullptr);
    navigationData = readNavigation();
    // The bots of the map loaded last, as the commands that follow `map` on a line act on it.
    bots.emplace(readBotRules(dataFolder), *m_jobs, settings->bots);
    if (auto registered = bots->registerCommands(m_console, [this] { return playedSession(); });
        !registered) {
        return registered;
    }
    auto content = cs::openGameContent(data, *install, cs::addonsFolderOf(m_files.directory()));
    if (!content) {
        return nx::fail(ClientError::ContentUnavailable, std::move(content.error()));
    }
    m_content = std::move(*content);
    if (auto registered = registerWeaponCommands(); !registered) {
        return registered;
    }
    // The menu buys for the player of the map played; its notices are timed as the map is.
    buyMenu.emplace(*m_content, [this] { return m_session.get(); }, m_mapClock);
    if (auto registered = buyMenu->registerCommands(m_console, m_content->weapons); !registered) {
        return registered;
    }
    if (auto registered = registerZombieCommands(m_console, *m_content, dataFolder, *m_jobs,
                                                 [this] { return playedSession(); });
        !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    auto shaders = cs::readSceneShaders(executable->parent_path() / "shaders");
    if (!shaders) {
        return nx::fail(ClientError::ContentUnavailable, std::move(shaders.error()));
    }
    m_shaders = std::move(*shaders);
    auto frameShaders = cs::readFrameShaders(executable->parent_path() / "shaders");
    if (!frameShaders) {
        return nx::fail(ClientError::ContentUnavailable, std::move(frameShaders.error()));
    }
    m_frameShaders = std::move(*frameShaders);
    if (auto audio = cs::ClientAudio::open(m_content->vfs, settings->client.audio.volume); audio) {
        m_audio = std::move(*audio);
    } else {
        nx::log::warning(kLog, "no sound: {}", nx::describe(audio.error()));
    }
    // The player's files, then the command line's commands, as the original's valve.rc runs them; the
    // output s_device names is opened once they ran, the default's sounds heard until then.
    nx::settings::runStartupFiles(m_console, m_files);
    m_console.addText(m_options->bootCommands);
    std::ignore = m_console.execute(); // what a `wait` leaves runs with the frames
    openNamedSoundOutput();
    auto video = openVideo(*m_options, settings->client.video.vsync.value(), readPipelineCache());
    if (!video) {
        return std::unexpected{std::move(video.error())};
    }
    m_video = std::move(*video);
    auto size = m_video->window.pixelSize();
    if (!size) {
        return nx::fail(ClientError::WindowUnavailable, std::move(size.error()));
    }
    openWindowText(size->height);
    return {};
}

auto Client::registerCommands() -> nx::Result<void, ClientError> {
    using nx::console::CommandLine;
    using nx::console::Console;
    const auto add = [this](nx::console::CommandDesc desc, nx::console::CommandHandler handler) {
        return m_console.registerCommand(desc, std::move(handler));
    };
    for (auto&& registered :
         {add({.name = "map", .description = "Plays a map: map <name>."},
              [this](Console&, const CommandLine& line) { return mapCommand(line); }),
          add({.name = "nav_draw",
               .description = "Draws the agents' navigation mesh and the way each bot and zombie follows "
                              "over the scene, or stops."},
              [this](Console&, const CommandLine& line) {
                  return navigationView.toggle(line, m_session.get());
              }),
          add({.name = "nav_generate",
               .description = "Builds the agents' navigation mesh of the map again, the cache's left "
                              "aside, and plays the map again on it."},
              [this](Console&, const CommandLine& line) { return generateNavigationCommand(line); }),
          add({.name = "play", .description = "Plays a sound heard everywhere: play <sound>."},
              [this](Console&, const CommandLine& line) { return playCommand(line); }),
          add({.name = "playvol",
               .description =
                   "Plays a sound heard everywhere at a volume from 0 to 1: playvol <sound> <volume>."},
              [this](Console&, const CommandLine& line) { return playVolumeCommand(line); }),
          add({.name = "s_devices", .description = "Lists the sound outputs s_device can name."},
              [this](Console&, const CommandLine&) { return listSoundOutputs(); }),
          add({.name = "noclip", .description = "Flies through everything, or walks again."},
              [this](Console&, const CommandLine&) {
                  noclipCommand();
                  return nx::Result<void, ConsoleError>{};
              }),
          add({.name = "lastinv", .description = "Draws the weapon held before."},
              [this](Console&, const CommandLine&) {
                  lastWeaponCommand();
                  return nx::Result<void, ConsoleError>{};
              }),
          add({.name = "quit", .description = "Ends the client."},
              [this](Console&, const CommandLine&) {
                  m_isRunning = false;
                  return nx::Result<void, ConsoleError>{};
              }),
          add({.name = "toggleconsole", .description = "Opens the console, or closes it."},
              [this](Console&, const CommandLine&) {
                  toggleConsole();
                  return nx::Result<void, ConsoleError>{};
              })}) {
        if (!registered) {
            return nx::fail(ClientError::SettingsUnavailable, registered.error());
        }
    }
    return {};
}

auto Client::isTakenByBuyMenu(const nx::platform::WindowEvent& event) -> bool {
    if (!buyMenu->isOpen()) {
        return false;
    }
    const auto size = m_video->window.pixelSize();
    return size && buyMenu->handle(event, m_video->window.mousePosition(), extentOf(*size));
}

auto Client::registerWeaponCommands() -> nx::Result<void, ClientError> {
    cs::WeaponActions actions{
        .select = [this](cs::WeaponIndex weapon) -> nx::Result<void, cs::WeaponActionError> {
            if (!m_session) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            settings->input.selectWeapon(weapon);
            return {};
        },
        .give = [this](cs::WeaponIndex weapon) -> nx::Result<void, cs::WeaponActionError> {
            if (!m_session) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            m_session->game->give(m_session->player, weapon);
            return {};
        },
        .giveArmor = [this](cs::ArmorKind kind) -> nx::Result<void, cs::WeaponActionError> {
            if (!m_session) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            m_session->game->giveArmor(m_session->player, kind);
            return {};
        }};
    if (auto registered = cs::registerWeaponCommands(m_console, m_content->weapons, std::move(actions));
        !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return {};
}

auto Client::playedSession() const noexcept -> Session* {
    return m_nextSession ? m_nextSession.get() : m_session.get();
}

auto Client::readNavigation() const -> nx::Result<nx::game::NavigationData, ConsoleError> {
    const auto file = dataFolder / "cstrike" / "navigation.toml";
    auto text = nx::platform::readTextFile(file);
    if (!text) {
        return nx::fail(ConsoleError::CommandFailed, std::move(text.error()));
    }
    auto navigation = nx::game::readNavigationData(*text, nx::platform::toUtf8(file));
    if (!navigation) {
        return nx::fail(ConsoleError::CommandFailed, std::move(navigation.error()));
    }
    return std::move(*navigation);
}

auto Client::mapCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError> {
    if (line.arguments().size() != 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: map <name>");
    }
    const std::string& name = line.arguments().front();
    auto session = startSession(*m_content, name, settings->client.player, settings->game, *m_jobs,
                                navigationData, KeptMesh::Read);
    if (!session) {
        return std::unexpected{std::move(session.error())};
    }
    m_nextSession = std::move(*session);
    return {};
}

auto Client::generateNavigationCommand(const nx::console::CommandLine& line)
    -> nx::Result<void, ConsoleError> {
    if (!line.arguments().empty()) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: nav_generate");
    }
    if (!m_session) {
        return nx::fail(ConsoleError::CommandFailed, "nav_generate: load a map first");
    }
    auto session = startSession(*m_content, m_session->map->name(), settings->client.player, settings->game,
                                *m_jobs, navigationData, KeptMesh::Ignored);
    if (!session) {
        return std::unexpected{std::move(session.error())};
    }
    m_nextSession = std::move(*session);
    return {};
}

auto Client::listSoundOutputs() -> nx::Result<void, ConsoleError> {
    auto names = nx::platform::audioOutputNames();
    if (!names) {
        return nx::fail(ConsoleError::CommandFailed, "s_devices", std::move(names.error()));
    }
    for (const std::string& name : *names) {
        m_console.print(std::format("\"{}\"", name));
    }
    return {};
}

auto Client::playCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError> {
    if (line.arguments().size() != 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: play <sound>");
    }
    return playSound(line.arguments().front(), kFullVolume);
}

auto Client::playVolumeCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError> {
    constexpr std::string_view kUsage = "usage: playvol <sound> <volume>";
    if (line.arguments().size() != 2) {
        return nx::fail(ConsoleError::InvalidArguments, std::string{kUsage});
    }
    const std::string& text = line.arguments()[1];
    float volume = 0.0F;
    if (const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), volume);
        error != std::errc{} || end != text.data() + text.size()) {
        return nx::fail(ConsoleError::InvalidArguments, std::string{kUsage});
    }
    return playSound(line.arguments().front(), volume);
}

auto Client::playSound(std::string_view name, float volume) -> nx::Result<void, ConsoleError> {
    if (m_audio == nullptr) {
        m_console.print("Can't play a sound, no sound output");
        return {};
    }
    const std::string file = soundFileOf(name);
    if (auto played = m_audio->playLocal(file, volume); !played) {
        return nx::fail(ConsoleError::CommandFailed, std::format("play {}", file), std::move(played.error()));
    }
    return {};
}

void Client::noclipCommand() {
    if (!m_session) {
        m_console.print("Can't \"noclip\", not playing a map");
        return;
    }
    m_console.print(m_session->game->toggleNoclip(m_session->player) ? "noclip ON" : "noclip OFF");
}

void Client::lastWeaponCommand() {
    if (!m_session) {
        m_console.print("Can't \"lastinv\", not playing a map");
        return;
    }
    if (const std::optional<cs::WeaponIndex> last = m_session->client.inventory().lastWeapon()) {
        settings->input.selectWeapon(*last);
    }
}

void Client::openWindowText(std::uint32_t screenHeight) {
    if (auto added = m_fonts.addFolder(dataFolder / kFontsFolder); !added) {
        nx::log::warning(kLog, "the project's fonts cannot be read: {}", nx::describe(added.error()));
    } else {
        for (const nx::Failure<nx::ui::FontError>& leftOut : added->leftOut) {
            nx::log::warning(kLog, "a font file of the project's is left out: {}", nx::describe(leftOut));
        }
    }
    auto fonts = cs::WindowFonts::open(m_content->vfs, m_fonts);
    if (!fonts) {
        nx::log::warning(kLog, "the console has no window, and cl_showfps shows nothing: {}",
                         nx::describe(fonts.error()));
        return;
    }
    if (auto window = cs::ConsoleWindow::open(*fonts, m_console, screenHeight); window) {
        m_consoleWindow = std::move(*window);
    } else {
        nx::log::warning(kLog, "the console has no window: {}", nx::describe(window.error()));
    }
    if (auto label = cs::FpsLabel::open(*fonts, screenHeight); label) {
        m_fpsLabel.emplace(std::move(*label));
    } else {
        nx::log::warning(kLog, "cl_showfps shows nothing: {}", nx::describe(label.error()));
    }
    if (auto numbers = cs::DamageNumbers::open(m_fonts, m_content->damageNumbers, screenHeight); numbers) {
        m_damageNumbers.emplace(std::move(*numbers));
    } else {
        nx::log::warning(kLog, "the damage of the hits shows no numbers: {}", nx::describe(numbers.error()));
    }
    m_stepIndicator.emplace(m_content->stepIndicator);
    if (auto feed = cs::KillFeed::open(*fonts, screenHeight); feed) {
        killFeed.emplace(std::move(*feed));
    } else {
        nx::log::warning(kLog, "the kill feed shows nothing: {}", nx::describe(feed.error()));
    }
    if (auto board = cs::Scoreboard::open(*fonts, screenHeight); board) {
        scoreboard.emplace(std::move(*board));
    } else {
        nx::log::warning(kLog, "the scoreboard shows nothing: {}", nx::describe(board.error()));
    }
}

void Client::toggleConsole() {
    if (m_consoleWindow == nullptr || m_consoleWindow->dialog() == nullptr) {
        m_console.print("The console has no window: its font could not be found");
        return;
    }
    const bool isOpen = m_consoleWindow->toggle();
    if (isOpen) {
        m_keys.releaseAll(); // the keys held for the game are let go
    }
    // The keys type while it is open.
    if (auto typing =
            m_video->window.setTextInput(isOpen ? nx::platform::TextInput::On : nx::platform::TextInput::Off);
        !typing) {
        nx::log::warning(kLog, "{}", nx::describe(typing.error()));
    }
    showCursorAsNeeded();
}

void Client::showCursorAsNeeded() {
    const bool isNeeded = (m_consoleWindow != nullptr && m_consoleWindow->isOpen()) || buyMenu->isOpen();
    if (isNeeded == isCursorShown) {
        return;
    }
    isCursorShown = isNeeded;
    if (auto set = m_video->window.setMouseMode(isNeeded ? nx::platform::MouseMode::Absolute
                                                         : nx::platform::MouseMode::Relative);
        !set) {
        nx::log::warning(kLog, "{}", nx::describe(set.error()));
    }
}

void Client::handleInConsole(const nx::platform::WindowEvent& event) {
    if (const auto* typed = std::get_if<nx::platform::TextEntered>(&event)) {
        m_consoleWindow->dialog()->insertText(typed->text);
        return;
    }
    const auto* key = std::get_if<nx::platform::KeyChanged>(&event);
    if (key == nullptr || !key->isDown) {
        return;
    }
    // Escape, and the keys bound to toggleconsole, close it; the rest are the window's.
    const std::optional<nx::console::KeyId> bound = nx::settings::keyOf(key->key);
    const bool isToggle = bound && m_bindings.commandOf(*bound) == "toggleconsole";
    if (key->key == nx::platform::Key::Escape || isToggle) {
        toggleConsole();
        return;
    }
    std::ignore = m_consoleWindow->dialog()->pressKey(key->key); // a key it does not take does nothing
}

auto Client::run() -> nx::Result<void, ClientError> {
    while (m_isRunning) {
        // fps_max: the limiter waits before the input is read, so that the frame shows the latest
        // (docs/05-rendering.md, section 12). The local game is one of more than one player, as a
        // Counter-Strike server's.
        const nx::settings::GamePlayers players =
            m_session ? nx::settings::GamePlayers::MoreThanOne : nx::settings::GamePlayers::One;
        const nx::Seconds interval = nx::settings::frameIntervalOf(settings->client.video, players);
        m_video->sleepTimer.sleepUntil(
            m_frameStart + std::chrono::duration_cast<nx::platform::MonotonicClock::duration>(interval));
        m_frameStart = nx::platform::MonotonicClock::now();
        m_frameRate.frameStarted(m_frameStart);
        // The frame's waits, for the GPU and for the frame before to be taken to the GPU, before the
        // input is read too (docs/adr/0029-submission-thread.md).
        auto commands = m_video->device->beginFrame();
        if (!commands) {
            return nx::fail(ClientError::GpuFailed, std::move(commands.error()));
        }
        if (auto read = readInput(); !read) {
            return read;
        }
        // What the keys queued, and what a `wait` left, before the frame's commands are made.
        std::ignore = m_console.execute();
        buyMenu->update();
        showCursorAsNeeded();
        if (m_audio != nullptr) {
            // The original is silent while its window is not focused.
            const bool isHeard = m_isFocused || settings->client.audio.playUnfocused.value();
            m_audio->setMuting(isHeard ? cs::Muting::Off : cs::Muting::On);
            m_audio->update();
        }
        startNextSession();
        updateWeapons();
        const nx::Seconds elapsed = m_frameClock.restart();
        settings->input.turn(m_camera.angles, elapsed);
        m_fog.update(settings->fog, elapsed);
        // A session plays once its scene is made, from the frame after the first image of it: what the
        // player hears of it, they see.
        if (m_session && m_scene) {
            const cs::Hearing hearing = m_session->client.hearing(); // from what the last frame showed
            // The player's own setting, as its client tells the server of it.
            m_session->game->setAutoSwitch(m_session->player, autoSwitchOf(settings->client.player));
            playSession(elapsed);
            m_camera.position = m_session->client.eye();
            m_session->client.updatePlayers(elapsed, settings->corpses.stayTime(), m_players);
            turnAsTheGameDid();
            hearSession(*m_session, m_audio.get(), hearing, m_soundFailures);
            // r_decals as it is now, as the original reads it at each decal.
            m_scene->setDecalLimit(decalLimitOf(settings->client.video));
            stampMarks(*m_session, *m_scene, *m_content, m_bulletHoles);
            const nx::Seconds now{m_mapClock.elapsed()};
            showNotices(*m_session, frameNotices(), m_content->weapons, now);
            showEffects(*m_session, *m_scene, *m_content, now, elapsed);
        }
        if (auto drawn = drawFrame(commands->get()); !drawn) {
            return drawn;
        }
    }
    return finish();
}

auto Client::readInput() -> nx::Result<void, ClientError> {
    m_events.clear();
    m_video->window.pollEvents(m_events);
    for (const nx::platform::WindowEvent& event : m_events) {
        if (const auto* focus = std::get_if<nx::platform::FocusChanged>(&event)) {
            m_isFocused = focus->isFocused;
        }
        const auto* key = std::get_if<nx::platform::KeyChanged>(&event);
        const bool isConsoleOpen = m_consoleWindow != nullptr && m_consoleWindow->isOpen();
        if (!isConsoleOpen && isTakenByBuyMenu(event)) {
            continue;
        }
        if (isConsoleOpen && !std::holds_alternative<nx::platform::CloseRequested>(event) &&
            !std::holds_alternative<nx::platform::Resized>(event)) {
            handleInConsole(event);
        } else if (std::holds_alternative<nx::platform::CloseRequested>(event) ||
                   (key != nullptr && key->key == nx::platform::Key::Escape && key->isDown)) {
            m_isRunning = false; // until the menus come, Escape ends the client, whatever is bound to it
        } else if (const auto* moved = std::get_if<nx::platform::MouseMoved>(&event)) {
            look(m_camera.angles, *moved, settings->client.mouse);
        } else if (const auto* resized = std::get_if<nx::platform::Resized>(&event)) {
            if (auto recreated = m_video->swapchain->resize(extentOf(resized->size)); !recreated) {
                return nx::fail(ClientError::GpuFailed, std::move(recreated.error()));
            }
        } else {
            m_keys.handle(event, settings->client.keyboard.keymap.value());
        }
    }
    return {};
}

void Client::openNamedSoundOutput() {
    const std::string& name = settings->client.audio.device.value();
    if (name.empty()) {
        return;
    }
    auto audio = cs::ClientAudio::open(m_content->vfs, settings->client.audio.volume, name);
    if (!audio) {
        nx::log::warning(kLog, "s_device {:?}: {}; the default plays", name, nx::describe(audio.error()));
        return;
    }
    m_audio = std::move(*audio);
}

void Client::startNextSession() {
    if (!m_nextSession) {
        return;
    }
    m_video->device->waitIdle(); // the old scene may still be drawing
    m_scene.reset();
    m_session = std::exchange(m_nextSession, nullptr);
    // The local game is one of more than one player, as a Counter-Strike server's: r_decals is held
    // to mp_decals as the original's client holds it when it enters one.
    if (auto held = nx::settings::enterMultiplayerGame(settings->client.video); !held) {
        nx::log::warning(kLog, "r_decals not held to mp_decals: {}", nx::describe(held.error()));
    }
    if (m_damageNumbers) {
        m_damageNumbers->clear(); // over the old game's bodies
    }
    if (m_stepIndicator) {
        m_stepIndicator->clear(); // the old game's bodies
    }
    if (killFeed) {
        killFeed->clear(); // the old game's deaths
    }
    buyMenu->reset();
    m_changedWeapons.clear(); // the new level read every weapon's view model
    readWeaponSounds(m_session->level->weapons());
    readPlayerSounds();
    m_camera.position = m_session->client.eye();
    turnAsTheGameDid();
    std::ignore = m_frameClock.restart(); // the loading is no time of the game's
}

void Client::playSession(nx::Seconds elapsed) {
    Session& session = *m_session;
    const nx::platform::Stopwatch ticking;
    for (std::uint32_t due = session.client.ticksDue(elapsed, cs::CatchUp::Limited);
         due > 0 && ticking.elapsed() < kMostTickingInAFrame; --due) {
        bots->keepQuota(session);
        std::vector<cs::PlayerCommand> commands{
            cs::PlayerCommand{.player = session.player,
                              .command = session.client.nextCommand(settings->input, m_camera.angles)}};
        if (session.bots) {
            std::ranges::move(session.bots->commands(), std::back_inserter(commands));
        }
        const std::span<const cs::EntityCommand> entityCommands =
            session.horde ? session.horde->commands() : std::span<const cs::EntityCommand>{};
        session.game->tick(commands, entityCommands, m_jobs.get());
        session.client.receive(session.game->snapshotFor(session.player));
    }
}

void Client::turnAsTheGameDid() {
    if (const std::optional<nx::EulerAngles> turned = m_session->client.takeTurnedView()) {
        m_camera.angles = *turned;
    }
}

void Client::updateWeapons() {
    if (!m_session) {
        return;
    }
    const std::vector<cs::WeaponIndex> changed =
        m_session->level->updateViewModels(m_content->vfs, m_content->weapons);
    readWeaponSounds(changed);
    m_changedWeapons.insert(m_changedWeapons.end(), changed.begin(), changed.end());
}

void Client::readWeaponSounds(std::span<const cs::WeaponIndex> weapons) {
    if (m_audio == nullptr) {
        return;
    }
    for (const cs::WeaponIndex weapon : weapons) {
        if (!m_content->weapons.isComplete(weapon)) {
            continue; // it makes no sound until its values make a weapon, which changes it again
        }
        const cs::Weapon& made = m_content->weapons.weapon(weapon);
        for (const std::string_view file : cs::soundFilesOf(made, m_session->level->viewModel(weapon))) {
            if (auto read = m_audio->preload(file); !read) {
                nx::log::warning(kLog, "a sound of {} cannot be read: {}", made.id,
                                 nx::describe(read.error()));
            }
        }
    }
}

void Client::readPlayerSounds() {
    if (m_audio == nullptr) {
        return;
    }
    for (const auto& [what, files] : {std::pair{"footsteps", m_content->footsteps.soundFiles()},
                                      std::pair{"breath", m_content->breath.soundFiles()},
                                      std::pair{"harm", m_content->playerHarm.soundFiles()},
                                      std::pair{"use", m_content->use.soundFiles()},
                                      std::pair{"purchases", m_content->buying.soundFiles()}}) {
        for (const std::string_view file : files) {
            if (auto read = m_audio->preload(file); !read) {
                nx::log::warning(kLog, "a sound of the {} cannot be read: {}", what,
                                 nx::describe(read.error()));
            }
        }
    }
}

auto Client::frameNotices() noexcept -> FrameNotices {
    return {.console = m_console,
            .killFeed = killFeed,
            .damageNumbers = m_damageNumbers,
            .damageNumberCvars = settings->damageNumbers,
            .stepIndicator = m_stepIndicator,
            .stepIndicatorCvars = settings->stepIndicator};
}

auto Client::drawFrame(nx::rhi::CommandList& list) -> nx::Result<void, ClientError> {
    nx::rhi::Device& device = *m_video->device;
    auto image = m_video->swapchain->acquire();
    if (!image) {
        return nx::fail(ClientError::GpuFailed, std::move(image.error()));
    }
    if (!*image) {
        if (auto submitted = device.submitFrame(); !submitted) {
            return nx::fail(ClientError::GpuFailed, std::move(submitted.error()));
        }
        m_video->sleepTimer.sleepUntil(nx::platform::MonotonicClock::now() +
                                       kMinimizedWait); // nothing to show
        return {};
    }
    const nx::rhi::SwapchainImage& frame = **image;
    if (auto uploaded = upload(list, frame.format); !uploaded) {
        return uploaded;
    }
    appendScreen(frame.size);
    std::optional<cs::FrameScene> scene;
    if (m_session) {
        scene.emplace(cs::FrameScene{.scene = *m_scene, .level = *m_session->level, .view = sessionView()});
    }
    if (auto recorded = m_frames->record(list, frame.texture, frame.size, scene, m_screenQuads); !recorded) {
        return nx::fail(ClientError::GpuFailed, std::move(recorded.error()));
    }
    list.transition(frame.texture, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::Present);
    if (auto submitted = device.submitFrame(*m_video->swapchain); !submitted) {
        return nx::fail(ClientError::GpuFailed, std::move(submitted.error()));
    }
    if (!m_video->window.isShown()) {
        if (auto shown = m_video->window.show(); !shown) { // with its first image
            return nx::fail(ClientError::WindowUnavailable, std::move(shown.error()));
        }
    }
    return {};
}

/// Puts on the GPU what the frame draws that is not there yet, its uploads recorded in `list` before
/// the frame renders: what draws the frames, the HUD's pictures and the console's atlas, and the scene
/// of the map played.
auto Client::upload(nx::rhi::CommandList& list, nx::rhi::Format format) -> nx::Result<void, ClientError> {
    if (!m_frames) {
        auto frames = cs::FrameRenderer::create(*m_video->device,
                                                std::move(*std::exchange(m_frameShaders, std::nullopt)),
                                                format, gammaOf(settings->client.video));
        if (!frames) {
            return nx::fail(ClientError::GpuFailed, std::move(frames.error()));
        }
        m_frames.emplace(std::move(*frames));
        if (auto hud = cs::Hud::load(m_content->vfs, m_content->weapons.ammo(), m_frames->screen(), list);
            hud) {
            m_hud.emplace(std::move(*hud));
        } else {
            nx::log::warning(kLog, "the HUD is not drawn: {}",
                             nx::describe(hud.error())); // the game plays on
        }
        if (m_consoleWindow != nullptr) {
            auto size = m_video->window.pixelSize();
            if (!size) {
                return nx::fail(ClientError::WindowUnavailable, std::move(size.error()));
            }
            if (auto loaded = m_consoleWindow->load(m_frames->screen(), list, extentOf(*size)); !loaded) {
                return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
            }
            m_output.window = m_consoleWindow->dialog();
        }
        if (m_fpsLabel) {
            if (auto loaded = m_fpsLabel->load(m_frames->screen(), list); !loaded) {
                return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
            }
        }
        if (m_damageNumbers) {
            if (auto loaded = m_damageNumbers->load(m_frames->screen(), list); !loaded) {
                return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
            }
        }
        if (m_stepIndicator) {
            if (auto loaded = m_stepIndicator->load(m_frames->screen(), list); !loaded) {
                return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
            }
        }
        if (killFeed) {
            if (auto loaded = killFeed->load(m_content->vfs, m_frames->screen(), list); !loaded) {
                nx::log::warning(kLog, "the kill feed shows nothing: {}", nx::describe(loaded.error()));
                killFeed.reset(); // the game plays on, as without the HUD
            }
        }
        if (scoreboard) {
            if (auto loaded = scoreboard->load(m_frames->screen(), list); !loaded) {
                return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
            }
        }
    }
    if (buyMenu->needsLoading()) {
        // What it shows, read before it opens; a weapon made since, and another height, as it does.
        auto size = m_video->window.pixelSize();
        if (!size) {
            return nx::fail(ClientError::WindowUnavailable, std::move(size.error()));
        }
        buyMenu->load(m_fonts, m_content->vfs, m_frames->screen(), list, extentOf(*size).height);
    }
    if (m_session && !m_scene) {
        // r_fidelity, read as the map's scene is made.
        const nx::render::StudioLook look =
            settings->client.video.fidelity.value() == nx::settings::Fidelity::Enhanced
                ? nx::render::StudioLook::Enhanced
                : nx::render::StudioLook::Legacy;
        auto scene =
            cs::Scene::create(*m_video->device, list, *m_session->level, *m_shaders,
                              m_frames->sceneFormatOf(look), m_frames->effectsFormatOf(look),
                              gammaOf(settings->client.video), look, decalLimitOf(settings->client.video));
        if (!scene) {
            return nx::fail(ClientError::GpuFailed, std::move(scene.error()));
        }
        m_scene.emplace(std::move(*scene));
        m_changedWeapons.clear();             // the scene put every weapon's view model on the GPU
        std::ignore = m_mapClock.restart();   // the light styles start with the map
        std::ignore = m_frameClock.restart(); // the scene's loading is no time of the game's
    }
    if (m_scene && !m_changedWeapons.empty()) {
        // The weapons made or changed since (updateWeapons): their view models put on the GPU now.
        m_video->device->waitIdle(); // the frames in flight may still draw a model replaced
        if (auto loaded = m_scene->loadWeapons(list, *m_session->level, m_changedWeapons); !loaded) {
            return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
        }
        m_changedWeapons.clear();
    }
    if (m_scene) {
        if (auto loaded = m_scene->loadEntities(list, m_content->entities, m_content->vfs); !loaded) {
            return nx::fail(ClientError::GpuFailed, std::move(loaded.error()));
        }
    }
    if (m_scene) {
        return navigationView.upload(*m_session, *m_scene, list, navigationData);
    }
    return {};
}

auto Client::kickedCamera() const -> nx::render::Camera {
    // The view is drawn turned by the kick the player's weapon gave it, as the original's client adds
    // the punch to it.
    nx::render::Camera kicked = m_camera;
    const nx::EulerAngles punch = m_session->client.punch();
    kicked.angles = nx::EulerAngles{.pitch = m_camera.angles.pitch + punch.pitch,
                                    .yaw = m_camera.angles.yaw + punch.yaw,
                                    .roll = m_camera.angles.roll + punch.roll};
    return kicked;
}

auto Client::sessionView() -> cs::SceneView {
    const nx::render::Camera kicked = kickedCamera();
    m_session->client.writePlacements(m_placements);
    m_session->client.writeHostages(m_hostages);
    m_session->client.writeCorpses(m_content->entities, settings->corpses.stayTime(), m_jobs.get(),
                                   shownEntities);
    const nx::Seconds time{m_mapClock.elapsed()};
    return {.camera = kicked,
            .time = time,
            .lightStyles = m_session->client.lightStylesAt(time),
            .placements = m_placements,
            .hostages = m_hostages,
            .players = m_players,
            .entities = shownEntities,
            .crowd = m_session->client.crowd(),
            .viewModel = m_session->viewPose,
            .tempEntities = m_session->effects.shown(),
            .particles = m_session->particles.shown(),
            .streaks = m_session->particles.shownStreaks(),
            .blood = m_session->blood.shown(time, m_content->blood.enhancedLook),
            .clientEvents = m_session->clientEvents,
            .hand = settings->hud.hand(),
            .overlay = navigationView.overlay(*m_session, *m_jobs),
            .fog = m_fog.shown(),
            .jobs = m_jobs.get()};
}

void Client::appendScreen(nx::rhi::Extent2d size) {
    m_screenQuads.clear();
    if (m_session && m_damageNumbers && settings->client.video.hudDraw.value() && size.height > 0) {
        // Over the scene, walls and all, under the HUD; the view the scene is drawn from.
        const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
        m_damageNumbers->append(m_screenQuads, nx::render::viewProjectionMatrix(kickedCamera(), aspect), size,
                                nx::Seconds{m_mapClock.elapsed()});
    }
    if (m_session && m_stepIndicator && settings->client.video.hudDraw.value()) {
        // About the middle of the screen, under the HUD, the way each body heard is from the view.
        const nx::render::Camera view = kickedCamera();
        m_stepIndicator->append(m_screenQuads, view.position, view.angles.yaw, size,
                                nx::Seconds{m_mapClock.elapsed()});
    }
    const bool showsBuyMenu = buyMenu->isShown();
    buyMenu->appendBackdrop(m_screenQuads, size); // under the HUD, which stays as bright
    if (m_session && m_hud && settings->client.video.hudDraw.value()) {
        // The HUD, over the scene, the knife's crosshair in the middle but under the buy menu.
        const cs::PlayerStatus& status = m_session->client.status();
        m_hud->append(
            m_screenQuads,
            {.health = status.health,
             .armor = status.armor,
             .armorKind = status.armorKind,
             .money = status.money,
             .ammo = cs::hudAmmoOf(m_session->client.inventory(), m_content->weapons),
             .crosshairDistance = showsBuyMenu ? std::nullopt : std::optional{cs::kKnifeCrosshairDistance},
             .roundTime = m_session->client.roundTimeShown()},
            settings->hud.crosshair(), size);
    }
    if (m_session && killFeed && settings->client.video.hudDraw.value()) {
        killFeed->append(m_screenQuads, size, nx::Seconds{m_mapClock.elapsed()},
                         settings->killFeed.shownFor(), !m_session->client.status().isAlive());
    }
    buyMenu->append(m_screenQuads, size);
    if (m_session && scoreboard && settings->scoreboard.shown.isDown()) {
        // One of the client's panels, over the HUD whether it is drawn or not.
        const std::string hostname = settings->scoreboard.hostname.value();
        scoreboard->append(m_screenQuads,
                           {.hostname = hostname,
                            .rows = m_session->client.scores(),
                            .self = m_session->client.self(),
                            .wins = m_session->client.teamWins()},
                           size);
    }
    // cl_showfps, over the HUD whether it is drawn or not, as the original's engine writes it over its
    // client's HUD, while a map is played.
    const std::optional<double> framesPerSecond = m_frameRate.framesPerSecond();
    if (m_session && m_fpsLabel && framesPerSecond && settings->client.video.showFps.value()) {
        m_fpsLabel->append(m_screenQuads, *framesPerSecond);
    }
    buyMenu->appendNotice(m_screenQuads, size);
    if (m_consoleWindow != nullptr && m_consoleWindow->dialog() != nullptr) {
        m_logSink->showIn(*m_consoleWindow->dialog());
        m_consoleWindow->paint(m_screenQuads, size);
    }
}

auto Client::finish() -> nx::Result<void, ClientError> {
    m_video->device->waitIdle();
    savePipelineCache(*m_video->device);
    if (auto saved = nx::settings::saveConfig(m_console, m_bindings, m_files); !saved) {
        nx::log::warning(kLog, "{}", nx::describe(saved.error()));
    }
    if (const std::uint64_t errors = m_video->device->validationErrors(); errors != 0) {
        return nx::fail(ClientError::ValidationFailed, std::format("{} errors", errors));
    }
    return {};
}

} // namespace

auto runClient(const ClientOptions& options) -> nx::Result<void, ClientError> {
    std::filesystem::path userFolder;
    if (options.userFolder) {
        userFolder = *options.userFolder;
    } else {
        auto directory = nx::platform::userDirectory();
        if (!directory) {
            return nx::fail(ClientError::SettingsUnavailable, std::move(directory.error()));
        }
        userFolder = std::move(*directory);
    }
    Client client{options, userFolder};
    if (auto started = client.start(); !started) {
        return started;
    }
    return client.run();
}

} // namespace app
