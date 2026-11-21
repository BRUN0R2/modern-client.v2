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
/// The stream of random numbers that chooses which decal a bullet leaves when its mark has several, as
/// the original's game chooses it among them.
constexpr std::string_view kBulletHoleStream = "bulletHoles";
/// The command that opens the console's window, and closes it: the keys bound to it do so over the main
/// menu too.
constexpr std::string_view kToggleConsoleCommand = "toggleconsole";
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

/// How the picture is made as the video cvars set it (r_fidelity).
[[nodiscard]] auto lookOf(const nx::settings::VideoCvars& video) noexcept -> nx::render::StudioLook {
    return video.fidelity.value() == nx::settings::Fidelity::Enhanced ? nx::render::StudioLook::Enhanced
                                                                      : nx::render::StudioLook::Legacy;
}

/// The most decals on faces the video cvars keep: `r_decals`, never negative.
[[nodiscard]] auto decalLimitOf(const nx::settings::VideoCvars& video) -> std::size_t {
    return nx::narrow<std::size_t>(video.decals.value());
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

/// The client: the player's settings and console, the map it plays (MapChange), and the window and GPU it
/// plays it in. It stays where it is made: the console's commands and the binds' input point into it.
class Client {
public:
    Client(const ClientOptions& options, const std::filesystem::path& userFolder)
        : m_options{&options}, m_loggerScope{m_logger}, m_files{userFolder}, m_console{m_output, m_files},
          m_bindings{defaultClientBindings()}, m_keys{m_console, m_bindings} {
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
    void followWindowMode();
    /// Registers the weapon commands (cs::registerWeaponCommands) over the game's content.
    [[nodiscard]] auto registerWeaponCommands() -> nx::Result<void, ClientError>;
    /// Loads the map asked for, once a frame told that it is loading (MapChange::loadAsked); one that cannot
    /// be loaded is reported as its command would report it, and the client stays where it was, on its menu
    /// when it plays no map.
    void loadMapAsked();
    /// The navigation profiles of data/cstrike/navigation.toml. Errors: CommandFailed.
    [[nodiscard]] auto readNavigation() const -> nx::Result<nx::game::NavigationData, ConsoleError>;
    void noclipCommand();
    void lastWeaponCommand();
    /// Opens the console's window, or closes it.
    void toggleConsole();
    /// The cursor shows while the console's window, the main menu or the buy menu is open; else the mouse
    /// turns the view.
    void showCursorAsNeeded();
    /// The keys type text (the platform's TextEntered) while the console's window is open or the main menu
    /// has a text typed, and only then.
    void typeAsNeeded();
    /// The player's language as cl_language names it now, whoever named it (ADR-0036): the texts read again
    /// in another, and the main menu laid out in them; one the data does not have is logged, the texts
    /// staying as they were.
    void followLanguage();
    /// Whether a map is played under the main menu.
    [[nodiscard]] auto menuGame() const noexcept -> cs::MenuGame;
    /// Opens the main menu over the map played, or over none: the keys held for the game are let go.
    void openMainMenu();
    /// Whether `key` is bound to `toggleconsole`.
    [[nodiscard]] auto togglesConsole(const nx::platform::KeyChanged& key) const -> bool;
    /// What `event` does in the main menu, open: the keys bound to `toggleconsole` open the console's
    /// window over it, unless the menu waits for a key to bind, and the rest are the menu's.
    void handleInMainMenu(const nx::platform::WindowEvent& event);
    /// Whether the buy menu, open, takes `event`.
    [[nodiscard]] auto isTakenByBuyMenu(const nx::platform::WindowEvent& event) -> bool;
    /// Opens the console's window and cl_showfps's label for a screen `screenHeight` pixels tall, in the
    /// fonts of the installation's windows; one that cannot be had is logged, and the client goes on
    /// without it.
    void openWindowText(std::uint32_t screenHeight);
    /// Reads what happened to the window: it closes or changes size, and the rest is input (handleInput).
    [[nodiscard]] auto readInput() -> nx::Result<void, ClientError>;
    /// Hands `event` to what takes the keys and the mouse now: the console's window, the main menu or the
    /// buy menu when open, else the game, where Escape opens the main menu.
    void handleInput(const nx::platform::WindowEvent& event);
    /// What `event` does in the console's window, open.
    void handleInConsole(const nx::platform::WindowEvent& event);
    /// Plays from this frame what the console asked for, when a switch is due (MapChange::switchMap): the map
    /// loaded, its scene made with the next frame, r_decals held as entering a game of more than one player
    /// holds it; or none, on the menu, once the console asked to leave the map played.
    void switchMap();
    /// Plays `elapsed` of the session's game, as a listen server plays it: each tick due, the command the
    /// player's input makes is the server's game's tick, whose snapshot the client's game takes.
    void playSession(nx::Seconds elapsed);
    /// Turns the view the way the session's game turned its player, if it did (the original's
    /// fixangle): as the player spawns.
    void turnAsTheGameDid();
    /// Reads, before the session plays the frame, the view models of the weapons made or changed since
    /// (cs::Level::updateViewModels), which upload puts on the GPU, and their sounds.
    void updateWeapons();
    /// Records the frame begun in `list`, and hands it to the GPU to show.
    [[nodiscard]] auto drawFrame(nx::rhi::CommandList& list) -> nx::Result<void, ClientError>;
    /// The scene of the map played, made by the video cvars as they are now (gammaOf, lookOf), its uploads
    /// recorded in `list`. Errors: GpuFailed.
    [[nodiscard]] auto makeScene(nx::rhi::CommandList& list) -> nx::Result<cs::Scene, ClientError>;
    /// Makes again what the picture's cvars made otherwise than they are now, its uploads recorded in
    /// `list`, as they change, whoever changed them: the HUD's pictures, through the screen's new gamma; and
    /// the scene of the map played, its decals and the overlay it kept carried over, the map's time going
    /// on. Errors: GpuFailed.
    [[nodiscard]] auto followPicture(nx::rhi::CommandList& list) -> nx::Result<void, ClientError>;
    [[nodiscard]] auto upload(nx::rhi::CommandList& list, const nx::rhi::SwapchainImage& frame)
        -> nx::Result<void, ClientError>;
    /// What the session's scene shows of the frame: the player's view, turned by the kick its weapon
    /// gave it, the brush entities where the game puts them, the weapon in hand, and what nav_draw draws.
    [[nodiscard]] auto sessionView() -> cs::SceneView;
    /// The camera the session's scene is drawn from: the player's view turned by its weapon's kick.
    [[nodiscard]] auto kickedCamera() const -> nx::render::Camera;
    /// The screen's pictures over the frame, of `size`: the HUD, cl_showfps's label, the main menu and the
    /// console's window when open.
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
    std::optional<ClientSounds> sounds; ///< On the system's sound output, once the content is open.
    std::optional<cs::SceneShaders> m_shaders;
    std::optional<cs::FrameShaders> m_frameShaders; ///< Until the frame renderer is made of them.
    /// The map played, and how it changes; each loaded by the game's content and the settings as they are.
    MapChange<Session> mapChange{
        [this](std::string_view name, KeptMesh kept) -> nx::Result<std::unique_ptr<Session>, ConsoleError> {
            return startSession(*m_content, name, settings->client.player, settings->game, *m_jobs,
                                navigationData, kept);
        }};
    /// Made or changed since the scene put their view models on the GPU.
    std::vector<cs::WeaponIndex> m_changedWeapons;
    nx::Random m_bulletHoles{0, kBulletHoleStream};
    std::optional<Video> m_video;
    std::optional<cs::Scene> m_scene;          ///< Of the session's level, made with the first image of it.
    std::optional<cs::FrameRenderer> m_frames; ///< What draws the frames, made with the first image.
    std::vector<nx::render::ScreenQuad> m_screenQuads;    ///< What the screen draws in a frame.
    std::vector<nx::render::ModelPlacement> m_placements; ///< Where the frame draws the brush entities.
    std::vector<cs::ShownHostage> m_hostages;             ///< How the frame shows the game's hostages.
    std::vector<cs::ShownPlayer> m_players;               ///< How the frame shows the other players.
    nx::ui::FontCatalog m_fonts;
    std::unique_ptr<cs::ConsoleWindow> m_consoleWindow; ///< None when the console has no window.
    ClientHud hud;                                      ///< What it draws over the scene of the map played.
    std::optional<ClientBuyMenu> buyMenu;               ///< Made with the game's content.
    std::optional<cs::PlayerTexts> texts;   ///< What the client writes, in the language of `cl_language`.
    std::optional<ClientMainMenu> mainMenu; ///< Made with the game's content and the texts.
    bool isCursorShown = false;
    nx::platform::FrameRateMeter m_frameRate; ///< Of the frames shown, which cl_showfps shows.
    std::vector<nx::platform::WindowEvent> m_events;
    nx::render::Camera m_camera{};
    nx::platform::Stopwatch m_frameClock;
    nx::platform::Stopwatch m_mapClock; ///< Since the map started: the light styles' time.
    nx::platform::Instant m_frameStart = nx::platform::MonotonicClock::now();
    bool m_isRunning = true;
    bool m_isFocused = true; ///< The window is, as it opens.
    bool m_isMinimized = false;
    bool m_needsSurfaceRefresh = false;
    nx::platform::WindowMode m_appliedWindowMode = nx::platform::WindowMode::Windowed;
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
    if (auto registered =
            bots->registerCommands(m_console, [this] -> Session* { return mapChange.played(); });
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
    buyMenu.emplace(*m_content, [this]() -> Session* { return mapChange.played(); }, m_mapClock);
    if (auto registered = buyMenu->registerCommands(m_console, m_content->weapons); !registered) {
        return registered;
    }
    if (auto registered = registerZombieCommands(m_console, *m_content, dataFolder, *m_jobs,
                                                 [this] -> Session* { return mapChange.played(); });
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
    sounds.emplace(m_content->vfs, settings->client.audio);
    if (auto registered = sounds->registerCommands(m_console); !registered) {
        return registered;
    }
    // The player's files, then the command line's commands, as the original's valve.rc runs them; the
    // output s_device names is opened once they ran, the default's sounds heard until then. What a `wait`
    // leaves, and what a map asked for holds until it is loaded, runs with the frames.
    nx::settings::runStartupFiles(m_console, m_files);
    m_console.addText(m_options->bootCommands);
    std::ignore = m_console.execute();
    // The language the player's files and the command line chose, once they ran.
    const std::string& language = settings->client.player.language.value();
    auto written = cs::PlayerTexts::open(dataFolder, language);
    if (!written) {
        return nx::fail(ClientError::ContentUnavailable,
                        std::format("cl_language {:?}: start with +cl_language {} to choose again", language,
                                    nx::settings::kDefaultLanguage),
                        std::move(written.error()));
    }
    texts.emplace(std::move(*written));
    mainMenu.emplace(*m_content, texts->written(),
                     clientOptionsOf(*settings, m_bindings, texts->languages()));
    if (!mapChange.askedMap()) {
        mainMenu->open(cs::MenuGame::None); // no map was asked for: the client opens on its menu
    }
    sounds->openNamedOutput();
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
    if (auto registered = mapChange.registerCommands(m_console); !registered) {
        return registered;
    }
    for (auto&& registered :
         {add({.name = "nav_draw",
               .description = "Draws the agents' navigation mesh and the way each bot and zombie follows "
                              "over the scene, or stops."},
              [this](Console&, const CommandLine& line) {
                  return navigationView.toggle(line, mapChange.played());
              }),
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
          add({.name = kToggleConsoleCommand, .description = "Opens the console, or closes it."},
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
            if (mapChange.played() == nullptr) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            settings->input.selectWeapon(weapon);
            return {};
        },
        .give = [this](cs::WeaponIndex weapon) -> nx::Result<void, cs::WeaponActionError> {
            const Session* session = mapChange.played();
            if (session == nullptr) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            session->game->give(session->player, weapon);
            return {};
        },
        .giveArmor = [this](cs::ArmorKind kind) -> nx::Result<void, cs::WeaponActionError> {
            const Session* session = mapChange.played();
            if (session == nullptr) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            session->game->giveArmor(session->player, kind);
            return {};
        }};
    if (auto registered = cs::registerWeaponCommands(m_console, m_content->weapons, std::move(actions));
        !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return {};
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

void Client::loadMapAsked() {
    if (auto loaded = mapChange.loadAsked(); !loaded) {
        m_output.reportFailure(loaded.error());
        if (mapChange.played() == nullptr && !mainMenu->isOpen()) {
            openMainMenu(); // asked for as the client started: it opens on its menu after all
        }
    }
}

void Client::noclipCommand() {
    const Session* session = mapChange.played();
    if (session == nullptr) {
        m_console.print("Can't \"noclip\", not playing a map");
        return;
    }
    m_console.print(session->game->toggleNoclip(session->player) ? "noclip ON" : "noclip OFF");
}

void Client::lastWeaponCommand() {
    const Session* session = mapChange.played();
    if (session == nullptr) {
        m_console.print("Can't \"lastinv\", not playing a map");
        return;
    }
    if (const std::optional<cs::WeaponIndex> last = session->client.inventory().lastWeapon()) {
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
    hud.open(*fonts, m_fonts, *m_content, screenHeight);
}

void Client::toggleConsole() {
    if (m_consoleWindow == nullptr || m_consoleWindow->dialog() == nullptr) {
        m_console.print("The console has no window: its font could not be found");
        return;
    }
    if (m_consoleWindow->toggle()) {
        m_keys.releaseAll(); // the keys held for the game are let go
    }
    typeAsNeeded();
    showCursorAsNeeded();
}

void Client::showCursorAsNeeded() {
    const bool isNeeded =
        (m_consoleWindow != nullptr && m_consoleWindow->isOpen()) || mainMenu->isOpen() || buyMenu->isOpen();
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

void Client::typeAsNeeded() {
    const bool isNeeded = (m_consoleWindow != nullptr && m_consoleWindow->isOpen()) || mainMenu->isTyping();
    if (isNeeded == m_video->window.isTextInputOn()) {
        return;
    }
    if (auto typing = m_video->window.setTextInput(isNeeded ? nx::platform::TextInput::On
                                                            : nx::platform::TextInput::Off);
        !typing) {
        nx::log::warning(kLog, "{}", nx::describe(typing.error()));
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
    if (key->key == nx::platform::Key::Escape || togglesConsole(*key)) {
        toggleConsole();
        return;
    }
    std::ignore = m_consoleWindow->dialog()->pressKey(key->key); // a key it does not take does nothing
}

auto Client::togglesConsole(const nx::platform::KeyChanged& key) const -> bool {
    const std::optional<nx::console::KeyId> bound = nx::settings::keyOf(key.key);
    return bound && m_bindings.commandOf(*bound) == kToggleConsoleCommand;
}

auto Client::menuGame() const noexcept -> cs::MenuGame {
    return mapChange.played() != nullptr ? cs::MenuGame::Played : cs::MenuGame::None;
}

void Client::openMainMenu() {
    mainMenu->open(menuGame());
    m_keys.releaseAll();
}

void Client::handleInMainMenu(const nx::platform::WindowEvent& event) {
    const auto* key = std::get_if<nx::platform::KeyChanged>(&event);
    if (key != nullptr && key->isDown && togglesConsole(*key) && !mainMenu->takesEveryKey()) {
        toggleConsole();
        return;
    }
    const auto size = m_video->window.pixelSize();
    if (!size) {
        return; // a window of no size has nothing the mouse could be over
    }
    mainMenu->handle(event, m_video->window.mousePosition(), extentOf(*size), menuGame(), m_console);
}

auto Client::run() -> nx::Result<void, ClientError> {
    while (m_isRunning) {
        followWindowMode();
        // fps_max: the limiter waits before the input is read, so that the frame shows the latest
        // (docs/05-rendering.md, section 12). The local game is one of more than one player, as a
        // Counter-Strike server's.
        const nx::settings::GamePlayers players = mapChange.played() != nullptr
                                                      ? nx::settings::GamePlayers::MoreThanOne
                                                      : nx::settings::GamePlayers::One;
        const nx::Seconds interval = nx::settings::frameIntervalOf(settings->client.video, players);
        m_video->sleepTimer.sleepUntil(
            m_frameStart + std::chrono::duration_cast<nx::platform::MonotonicClock::duration>(interval));
        m_frameStart = nx::platform::MonotonicClock::now();
        m_frameRate.frameStarted(m_frameStart);
        // gl_vsync as it is now, whoever set it: the swapchain is made again between two frames as it
        // changes.
        if (auto presented =
                m_video->swapchain->setPresentMode(presentModeOf(settings->client.video.vsync.value()));
            !presented) {
            return nx::fail(ClientError::GpuFailed, std::move(presented.error()));
        }
        // The frame's waits, for the GPU and for the frame before to be taken to the GPU, before the
        // input is read too (docs/adr/0029-submission-thread.md).
        auto commands = m_video->device->beginFrame();
        if (!commands) {
            return nx::fail(ClientError::GpuFailed, std::move(commands.error()));
        }
        if (auto read = readInput(); !read) {
            return read;
        }
        loadMapAsked();
        // What the keys queued, and what a `wait` left, before the frame's commands are made; what follows
        // a map asked for is held by the console until it is loaded, so that it acts on it.
        std::ignore = m_console.execute();
        followLanguage();
        buyMenu->update();
        showCursorAsNeeded();
        typeAsNeeded();
        sounds->update(m_isFocused);
        switchMap();
        updateWeapons();
        const nx::Seconds elapsed = m_frameClock.restart();
        settings->input.turn(m_camera.angles, elapsed);
        m_fog.update(settings->fog, elapsed);
        // A session plays once its scene is made, from the frame after the first image of it: what the
        // player hears of it, they see.
        if (Session* session = mapChange.played(); session != nullptr && m_scene) {
            const cs::Hearing hearing = session->client.hearing(); // from what the last frame showed
            // The player's own setting, as its client tells the server of it.
            session->game->setAutoSwitch(session->player, autoSwitchOf(settings->client.player));
            playSession(elapsed);
            m_camera.position = session->client.eye();
            session->client.updatePlayers(elapsed, settings->corpses.stayTime(), m_players);
            turnAsTheGameDid();
            sounds->hear(*session, hearing, kickedCamera());
            // r_decals as it is now, as the original reads it at each decal.
            m_scene->setDecalLimit(decalLimitOf(settings->client.video));
            stampMarks(*session, *m_scene, *m_content, m_bulletHoles);
            const nx::Seconds now{m_mapClock.elapsed()};
            showNotices(*session, hud.notices(m_console, *settings), m_content->weapons, now);
            showEffects(*session, *m_scene, *m_content, now, elapsed);
        }
        if (auto drawn = drawFrame(commands->get()); !drawn) {
            return drawn;
        }
        mapChange.frameShown(); // by the frame drawn, or by none while the window shows none
    }
    return finish();
}

void Client::followLanguage() {
    auto asked = texts->ask(settings->client.player.language.value());
    if (!asked) {
        nx::log::warning(kLog, "cl_language: {}", nx::describe(asked.error()));
    } else if (*asked == cs::LanguageAsked::Read) {
        mainMenu->writeIn(texts->written());
    }
}

void Client::followWindowMode() {
    auto& video = *m_video;
    const auto setting = settings->client.video.windowMode;
    const auto requested = setting.value();
    if (requested == m_appliedWindowMode || m_isMinimized || !video.window.isShown()) {
        return;
    }
    video.device->waitIdle();
    if (const auto applied = video.window.setMode(requested); !applied) {
        nx::log::warning(kLog, "vid_mode {}: {}", nx::enumName(requested), nx::describe(applied.error()));
        if (const auto restored = video.window.setMode(m_appliedWindowMode); !restored) {
            nx::log::warning(kLog, "restoring the window mode: {}", nx::describe(restored.error()));
        }
        setting.setAccepted(video.window.mode());
    } else {
        nx::log::info(kLog, "window mode: {}", nx::enumName(requested));
    }
    m_appliedWindowMode = setting.value();
    m_needsSurfaceRefresh = true;
}

auto Client::readInput() -> nx::Result<void, ClientError> {
    m_events.clear();
    m_video->window.pollEvents(m_events);
    bool refreshSurface = std::exchange(m_needsSurfaceRefresh, false);
    for (const nx::platform::WindowEvent& event : m_events) {
        if (const auto* focus = std::get_if<nx::platform::FocusChanged>(&event)) {
            m_isFocused = focus->isFocused;
            refreshSurface = refreshSurface || m_isFocused;
        }
        if (std::holds_alternative<nx::platform::CloseRequested>(event)) {
            m_isRunning = false;
        } else if (const auto* minimized = std::get_if<nx::platform::Minimized>(&event)) {
            m_isMinimized = minimized->isMinimized;
            refreshSurface = true;
        } else if (std::holds_alternative<nx::platform::Resized>(event)) {
            refreshSurface = true;
        } else {
            handleInput(event);
        }
    }
    if (refreshSurface && m_isRunning) {
        nx::platform::PixelSize size{};
        if (!m_isMinimized) {
            auto current = m_video->window.pixelSize();
            if (!current) {
                return nx::fail(ClientError::WindowUnavailable, std::move(current.error()));
            }
            size = *current;
        }
        if (auto recreated = m_video->swapchain->resize(extentOf(size)); !recreated) {
            return nx::fail(ClientError::GpuFailed, std::move(recreated.error()));
        }
    }
    return {};
}

void Client::handleInput(const nx::platform::WindowEvent& event) {
    // Whatever is open over the game takes the keys and the mouse, the one on top first.
    if (m_consoleWindow != nullptr && m_consoleWindow->isOpen()) {
        handleInConsole(event);
        return;
    }
    if (mainMenu->isOpen()) {
        handleInMainMenu(event);
        return;
    }
    if (isTakenByBuyMenu(event)) {
        return;
    }
    const auto* key = std::get_if<nx::platform::KeyChanged>(&event);
    if (key != nullptr && key->key == nx::platform::Key::Escape) {
        // Escape opens the main menu whatever is bound to it, as in the original.
        if (key->isDown && !key->isRepeat) {
            openMainMenu();
        }
    } else if (const auto* moved = std::get_if<nx::platform::MouseMoved>(&event)) {
        look(m_camera.angles, *moved, settings->client.mouse);
    } else {
        m_keys.handle(event, settings->client.keyboard.keymap.value());
    }
}

void Client::switchMap() {
    const MapSwitch due = mapChange.switchDue();
    if (due == MapSwitch::Stay) {
        return;
    }
    m_video->device->waitIdle(); // the old scene may still be drawing
    m_scene.reset();
    mapChange.switchMap();
    hud.clear();
    buyMenu->reset();
    m_changedWeapons.clear(); // the new level read every weapon's view model
    if (due == MapSwitch::Leave) {
        openMainMenu(); // the map was left: none is played
        return;
    }
    mainMenu->close();
    // The local game is one of more than one player, as a Counter-Strike server's: r_decals is held
    // to mp_decals as the original's client holds it when it enters one.
    if (auto held = nx::settings::enterMultiplayerGame(settings->client.video); !held) {
        nx::log::warning(kLog, "r_decals not held to mp_decals: {}", nx::describe(held.error()));
    }
    const Session& session = *mapChange.played();
    sounds->readWeapons(m_content->weapons, *session.level, session.level->weapons());
    sounds->readPlayer(*m_content);
    m_camera.position = session.client.eye();
    turnAsTheGameDid();
    std::ignore = m_frameClock.restart(); // the loading is no time of the game's
}

void Client::playSession(nx::Seconds elapsed) {
    Session& session = *mapChange.played();
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
    if (const std::optional<nx::EulerAngles> turned = mapChange.played()->client.takeTurnedView()) {
        m_camera.angles = *turned;
    }
}

void Client::updateWeapons() {
    const Session* session = mapChange.played();
    if (session == nullptr) {
        return;
    }
    const std::vector<cs::WeaponIndex> changed =
        session->level->updateViewModels(m_content->vfs, m_content->weapons);
    sounds->readWeapons(m_content->weapons, *session->level, changed);
    m_changedWeapons.insert(m_changedWeapons.end(), changed.begin(), changed.end());
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
    if (auto uploaded = upload(list, frame); !uploaded) {
        return uploaded;
    }
    appendScreen(frame.size);
    std::optional<cs::FrameScene> scene;
    if (const Session* session = mapChange.played(); session != nullptr) {
        scene.emplace(cs::FrameScene{.scene = *m_scene, .level = *session->level, .view = sessionView()});
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

auto Client::makeScene(nx::rhi::CommandList& list) -> nx::Result<cs::Scene, ClientError> {
    const nx::settings::VideoCvars& video = settings->client.video;
    const nx::render::StudioLook look = lookOf(video);
    auto scene = cs::Scene::create(*m_video->device, list, *mapChange.played()->level, *m_shaders,
                                   m_frames->sceneFormatOf(look), m_frames->effectsFormatOf(look),
                                   gammaOf(video), look, decalLimitOf(video));
    if (!scene) {
        return nx::fail(ClientError::GpuFailed, std::move(scene.error()));
    }
    return std::move(*scene);
}

auto Client::followPicture(nx::rhi::CommandList& list) -> nx::Result<void, ClientError> {
    const nx::render::GammaSettings gamma = gammaOf(settings->client.video);
    nx::render::ScreenRenderer& screen = m_frames->screen();
    if (screen.gamma() != gamma) {
        screen.setGamma(gamma);
        hud.loadPictures(*m_content, screen, list);
    }
    if (!m_scene || (m_scene->gamma() == gamma && m_scene->look() == lookOf(settings->client.video))) {
        return {};
    }
    auto scene = makeScene(list);
    if (!scene) {
        return std::unexpected{std::move(scene.error())};
    }
    scene->keepDecalsOf(*m_scene);
    m_scene.emplace(std::move(*scene));
    m_changedWeapons.clear();             // the scene put every weapon's view model on the GPU
    std::ignore = m_frameClock.restart(); // making it is no time of the game's
    return NavigationView::keepOn(*m_scene, *mapChange.played(), list);
}

/// Puts on the GPU what the frame draws that is not there yet, its uploads recorded in `list` before
/// the frame renders: what draws the frames, the HUD's pictures and the console's atlas, the main menu's
/// text while it is open, and the scene of the map played; and again what the picture's cvars changed.
auto Client::upload(nx::rhi::CommandList& list, const nx::rhi::SwapchainImage& frame)
    -> nx::Result<void, ClientError> {
    if (!m_frames) {
        auto frames = cs::FrameRenderer::create(*m_video->device,
                                                std::move(*std::exchange(m_frameShaders, std::nullopt)),
                                                frame.format, gammaOf(settings->client.video));
        if (!frames) {
            return nx::fail(ClientError::GpuFailed, std::move(frames.error()));
        }
        m_frames.emplace(std::move(*frames));
        if (auto loaded = hud.load(*m_content, m_frames->screen(), list); !loaded) {
            return loaded;
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
    }
    if (auto followed = followPicture(list); !followed) {
        return followed;
    }
    if (mainMenu->isOpen() || mapChange.askedMap()) {
        if (auto loaded = mainMenu->load(m_fonts, m_frames->screen(), list, frame.size); !loaded) {
            return loaded;
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
    Session* session = mapChange.played();
    if (session != nullptr && !m_scene) {
        auto scene = makeScene(list);
        if (!scene) {
            return std::unexpected{std::move(scene.error())};
        }
        m_scene.emplace(std::move(*scene));
        m_changedWeapons.clear();             // the scene put every weapon's view model on the GPU
        std::ignore = m_mapClock.restart();   // the light styles start with the map
        std::ignore = m_frameClock.restart(); // the scene's loading is no time of the game's
    }
    if (m_scene && !m_changedWeapons.empty()) {
        // The weapons made or changed since (updateWeapons): their view models put on the GPU now.
        m_video->device->waitIdle(); // the frames in flight may still draw a model replaced
        if (auto loaded = m_scene->loadWeapons(list, *session->level, m_changedWeapons); !loaded) {
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
        return navigationView.upload(*session, *m_scene, list, navigationData);
    }
    return {};
}

auto Client::kickedCamera() const -> nx::render::Camera {
    // The view is drawn turned by the kick the player's weapon gave it, as the original's client adds
    // the punch to it.
    nx::render::Camera kicked = m_camera;
    const nx::EulerAngles punch = mapChange.played()->client.punch();
    kicked.angles = nx::EulerAngles{.pitch = m_camera.angles.pitch + punch.pitch,
                                    .yaw = m_camera.angles.yaw + punch.yaw,
                                    .roll = m_camera.angles.roll + punch.roll};
    return kicked;
}

auto Client::sessionView() -> cs::SceneView {
    const nx::render::Camera kicked = kickedCamera();
    Session& session = *mapChange.played();
    session.client.writePlacements(m_placements);
    session.client.writeHostages(m_hostages);
    session.client.writeCorpses(m_content->entities, settings->corpses.stayTime(), m_jobs.get(),
                                shownEntities);
    const nx::Seconds time{m_mapClock.elapsed()};
    return {.camera = kicked,
            .time = time,
            .lightStyles = session.client.lightStylesAt(time),
            .placements = m_placements,
            .hostages = m_hostages,
            .players = m_players,
            .entities = shownEntities,
            .crowd = session.client.crowd(),
            .viewModel = session.viewPose,
            .tempEntities = session.effects.shown(),
            .particles = session.particles.shown(),
            .streaks = session.particles.shownStreaks(),
            .blood = session.blood.shown(time, m_content->blood.enhancedLook),
            .clientEvents = session.clientEvents,
            .hand = settings->hud.hand(),
            .overlay = navigationView.overlay(session, *m_jobs),
            .fog = m_fog.shown(),
            .jobs = m_jobs.get()};
}

void Client::appendScreen(nx::rhi::Extent2d size) {
    m_screenQuads.clear();
    // Measured each frame, whether it shows or not.
    const std::optional<double> framesPerSecond = m_frameRate.framesPerSecond();
    if (const Session* session = mapChange.played(); session != nullptr) {
        const HudFrame frame{.session = *session,
                             .content = *m_content,
                             .settings = *settings,
                             .view = kickedCamera(),
                             .now = nx::Seconds{m_mapClock.elapsed()},
                             .size = size};
        const bool showsBuyMenu = buyMenu->isShown();
        hud.appendUnderMenu(m_screenQuads, frame);
        buyMenu->appendBackdrop(m_screenQuads, size); // under the HUD, which stays as bright
        hud.appendHud(m_screenQuads, frame, showsBuyMenu);
        buyMenu->append(m_screenQuads, size);
        hud.appendPanels(m_screenQuads, frame, framesPerSecond);
        buyMenu->appendNotice(m_screenQuads, size);
    }
    mainMenu->append(m_screenQuads, size, menuGame());
    if (const std::optional<std::string_view> asked = mapChange.askedMap()) {
        mainMenu->appendLoading(m_screenQuads, size, *asked);
    }
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
