// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// stderr: a macro, which `import std;` does not export.
#include <cstdio>

module app.client;

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.assets;
import nx.settings;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;

namespace app {

namespace {

using nx::console::ConsoleError;

constexpr nx::log::Category kLog{"client"};

/// The key `next` binds `noclip` to, which Counter-Strike leaves free.
constexpr std::string_view kNoclipKey = "v";
/// How long a minimized window waits before it looks for events again.
constexpr std::chrono::milliseconds kMinimizedWait{50};
/// The volume `play` plays a sound at, and the view model's sound events: as it is.
constexpr float kFullVolume = 1.0F;
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

/// Whether the game draws a weapon given heavier than the one held, as `_cl_autowepswitch` says.
[[nodiscard]] auto autoSwitchOf(const nx::settings::PlayerCvars& player) -> cs::AutoSwitch {
    return player.autoWeaponSwitch.value() ? cs::AutoSwitch::ToHeavier : cs::AutoSwitch::Off;
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

[[nodiscard]] auto extentOf(nx::platform::PixelSize size) noexcept -> nx::rhi::Extent2d {
    return {.width = size.width, .height = size.height};
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

/// The window, the GPU and the frame's pace. The swapchain goes before its device, the device before
/// its window.
struct Video {
    nx::platform::Window window;
    std::unique_ptr<nx::rhi::Device> device;
    std::unique_ptr<nx::rhi::Swapchain> swapchain;
    nx::platform::SleepTimer sleepTimer;
};

/// The file of the cache folder (nx::platform::cacheDirectory) that keeps what the GPU learned
/// building next's pipelines.
constexpr std::string_view kPipelineCacheFile = "pipelineCache.bin";

/// What the GPU learned building the pipelines the last time next ran; nothing the first time, or
/// when the file cannot be read.
[[nodiscard]] auto readPipelineCache() -> std::vector<std::byte> {
    const auto folder = nx::platform::cacheDirectory();
    if (!folder) {
        return {};
    }
    return nx::platform::readFile(*folder / kPipelineCacheFile).value_or(std::vector<std::byte>{});
}

/// Keeps what `device` learned building its pipelines for the next run; logs what fails.
void savePipelineCache(const nx::rhi::Device& device) {
    auto learned = device.pipelineCache();
    auto folder = nx::platform::cacheDirectory();
    if (!learned || !folder) {
        nx::log::warning(kLog, "the pipeline cache is not kept: {}",
                         !learned ? nx::describe(learned.error()) : nx::describe(folder.error()));
        return;
    }
    if (auto made = nx::platform::createDirectories(*folder); !made) {
        nx::log::warning(kLog, "the pipeline cache is not kept: {}", nx::describe(made.error()));
        return;
    }
    if (auto written = nx::platform::writeFile(*folder / kPipelineCacheFile, *learned); !written) {
        nx::log::warning(kLog, "the pipeline cache is not kept: {}", nx::describe(written.error()));
    }
}

/// Opens the window and the GPU, presenting as `isVsync` asks, the device given what the GPU learned
/// building the pipelines before (`pipelineCache`). Errors: WindowUnavailable, GpuFailed.
[[nodiscard]] auto openVideo(const ClientOptions& options, bool isVsync,
                             std::span<const std::byte> pipelineCache) -> nx::Result<Video, ClientError> {
    // Hidden until its first image (Client::drawFrame): never empty while the GPU starts and the map loads.
    auto window = nx::platform::Window::create({.title = "Modern-Client.v2", .isShown = false});
    if (!window) {
        return nx::fail(ClientError::WindowUnavailable, std::move(window.error()));
    }
    auto native = window->nativeWindow();
    auto size = window->pixelSize();
    if (!native || !size) {
        return !native ? nx::fail(ClientError::WindowUnavailable, std::move(native.error()))
                       : nx::fail(ClientError::WindowUnavailable, std::move(size.error()));
    }
    auto device = nx::rhi::vulkan::createDevice({.applicationName = "next",
                                                 .window = *native,
                                                 .isValidationEnabled = options.isValidationEnabled,
                                                 .pipelineCache = pipelineCache});
    if (!device) {
        return nx::fail(ClientError::GpuFailed, std::move(device.error()));
    }
    auto swapchain = (*device)->createSwapchain(
        {.size = extentOf(*size),
         .presentMode = isVsync ? nx::rhi::PresentMode::Fifo : nx::rhi::PresentMode::Immediate});
    if (!swapchain) {
        return nx::fail(ClientError::GpuFailed, std::move(swapchain.error()));
    }
    auto sleepTimer = nx::platform::SleepTimer::create();
    if (!sleepTimer) {
        return nx::fail(ClientError::GpuFailed, "the frame's pace", std::move(sleepTimer.error()));
    }
    if (auto captured = window->setMouseMode(nx::platform::MouseMode::Relative); !captured) {
        nx::log::warning(kLog, "{}", nx::describe(captured.error())); // the keys still play
    }
    return Video{.window = std::move(*window),
                 .device = std::move(*device),
                 .swapchain = std::move(*swapchain),
                 .sleepTimer = std::move(*sleepTimer)};
}

/// A map being played: its level, and the game of it, which goes before the level it plays.
struct Session {
    std::unique_ptr<cs::Level> level;
    std::unique_ptr<cs::LocalGame> game;
};

/// The map `name` of `content`, its game started by the rules `cvars` set, its player of the team of
/// its first spawn point, its weapons drawn as `autoSwitch` says. Errors: those of Level::load and
/// LocalGame::start.
[[nodiscard]] auto startSession(const cs::GameContent& content, std::string_view name,
                                cs::AutoSwitch autoSwitch, const cs::GameCvars& cvars)
    -> nx::Result<Session, cs::LevelError> {
    auto level = cs::Level::load(content, name);
    if (!level) {
        return std::unexpected{std::move(level.error())};
    }
    auto game = cs::LocalGame::start(content, **level, cs::spawnTeam((*level)->map()), autoSwitch, cvars);
    if (!game) {
        return std::unexpected{std::move(game.error())};
    }
    return Session{.level = std::move(*level), .game = std::move(*game)};
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
    [[nodiscard]] auto registerSettings() -> nx::Result<void, ClientError>;
    [[nodiscard]] auto registerCommands() -> nx::Result<void, ClientError>;
    /// Registers the weapon commands (cs::registerWeaponCommands) over the game's content.
    [[nodiscard]] auto registerWeaponCommands() -> nx::Result<void, ClientError>;
    [[nodiscard]] auto mapCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError>;
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
    /// Reads the sounds of the player's feet, breath, pain, death and use (cs::FootstepRules::soundFiles,
    /// cs::BreathRules::soundFiles, cs::PlayerHarmRules::soundFiles, cs::UseRules::soundFiles) as a
    /// session starts, so that none waits on its file. One that cannot be read is logged, and the game
    /// plays on.
    void readPlayerSounds();
    /// What the player hears of the frame the session played, as `hearing` says when: the sounds its
    /// weapons made and its view model's sound events, each as long after the frame as it came after
    /// what the last frame showed; and the client events its view model passed, which the frame shows.
    void hearSession(const cs::Hearing& hearing);
    /// Reports `played` when it failed, once in kSoundFailureInterval.
    void reportSound(const nx::Result<void, cs::PlayError>& played);
    /// Leaves a bullet's mark where each of the session's shots met the world or a brush entity in the
    /// frame, once the scene is drawn: the decal the mark of what it met leaves (cs::bulletDecalOf).
    void stampBulletHoles();
    [[nodiscard]] auto drawFrame() -> nx::Result<void, ClientError>;
    [[nodiscard]] auto upload(nx::rhi::CommandList& list, nx::rhi::Format format)
        -> nx::Result<void, ClientError>;
    /// What the session's scene shows of the frame: the player's view, turned by the kick its weapon
    /// gave it, the brush entities where the game puts them, and the weapon in hand.
    [[nodiscard]] auto sessionView() -> cs::SceneView;
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
    std::optional<nx::settings::ClientCvars> m_cvars;
    std::optional<cs::PlayerInput> m_input;
    std::optional<cs::HudCvars> m_hudCvars;
    std::optional<cs::GameCvars> m_gameCvars;
    std::optional<cs::GameContent> m_content;
    std::unique_ptr<cs::ClientAudio> m_audio; ///< None without a sound output: the game plays on, silent.
    std::optional<cs::SceneShaders> m_shaders;
    std::optional<cs::FrameShaders> m_frameShaders; ///< Until the frame renderer is made of them.
    std::optional<Session> m_session;               ///< The map played.
    std::optional<Session> m_nextSession;           ///< The map `map` loaded, played from the next frame.
    /// Made or changed since the scene put their view models on the GPU.
    std::vector<cs::WeaponIndex> m_changedWeapons;
    cs::ViewModelEvents m_viewModelEvents;
    std::optional<cs::ViewModelPose> m_viewPose; ///< Of the frame played.
    std::vector<cs::PassedEvent> m_clientEvents; ///< Its view model passed in the frame.
    nx::log::RateLimit m_soundFailures{kSoundFailureInterval};
    nx::Random m_bulletHoles{0, kBulletHoleStream};
    std::optional<Video> m_video;
    std::optional<cs::Scene> m_scene;          ///< Of the session's level, made with the first image of it.
    std::optional<cs::FrameRenderer> m_frames; ///< What draws the frames, made with the first image.
    std::optional<cs::Hud> m_hud;              ///< None when its pictures could not be read.
    std::vector<nx::render::ScreenQuad> m_screenQuads;    ///< What the screen draws in a frame.
    std::vector<nx::render::ModelPlacement> m_placements; ///< Where the frame draws the brush entities.
    std::vector<cs::ShownHostage> m_hostages;             ///< How the frame shows the game's hostages.
    nx::ui::FontCatalog m_fonts;
    std::unique_ptr<cs::ConsoleWindow> m_consoleWindow; ///< None when the console has no window.
    std::optional<cs::FpsLabel> m_fpsLabel;             ///< None when its font could not be had.
    nx::platform::FrameRateMeter m_frameRate;           ///< Of the frames shown, which cl_showfps shows.
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
    if (auto registered = registerSettings(); !registered) {
        return registered;
    }
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
    auto content = cs::openGameContent(m_options->data.value_or(executable->parent_path() / "data"), *install,
                                       cs::addonsFolderOf(m_files.directory()));
    if (!content) {
        return nx::fail(ClientError::ContentUnavailable, std::move(content.error()));
    }
    m_content = std::move(*content);
    if (auto registered = registerWeaponCommands(); !registered) {
        return registered;
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
    if (auto audio = cs::ClientAudio::open(m_content->vfs, m_cvars->audio.volume); audio) {
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
    auto video = openVideo(*m_options, m_cvars->video.vsync.value(), readPipelineCache());
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

auto Client::registerSettings() -> nx::Result<void, ClientError> {
    auto cvars = nx::settings::registerClientCvars(m_console);
    if (!cvars) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(cvars.error()));
    }
    m_cvars = std::move(*cvars);
    auto input = cs::PlayerInput::registerIn(m_console);
    if (!input) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(input.error()));
    }
    m_input = std::move(*input);
    auto hudCvars = cs::HudCvars::registerIn(m_console);
    if (!hudCvars) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(hudCvars.error()));
    }
    m_hudCvars = std::move(*hudCvars);
    auto gameCvars = cs::GameCvars::registerIn(m_console);
    if (!gameCvars) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(gameCvars.error()));
    }
    m_gameCvars = std::move(*gameCvars);
    // Counter-Strike's binds, and noclip on a key it leaves free, until config.cfg says otherwise.
    const std::optional<nx::console::KeyId> noclipKey = nx::settings::keyNamed(kNoclipKey);
    nx::invariant(noclipKey.has_value(), "Client: the noclip key has a name");
    m_bindings.bind(*noclipKey, "noclip");
    if (auto registered = nx::settings::registerBindCommands(m_console, m_bindings); !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
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

auto Client::registerWeaponCommands() -> nx::Result<void, ClientError> {
    cs::WeaponActions actions{
        .select = [this](cs::WeaponIndex weapon) -> nx::Result<void, cs::WeaponActionError> {
            if (!m_session) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            m_input->selectWeapon(weapon);
            return {};
        },
        .give = [this](cs::WeaponIndex weapon) -> nx::Result<void, cs::WeaponActionError> {
            if (!m_session) {
                return nx::fail(cs::WeaponActionError::NoMap);
            }
            m_session->game->give(weapon);
            return {};
        }};
    if (auto registered = cs::registerWeaponCommands(m_console, m_content->weapons, std::move(actions));
        !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return {};
}

auto Client::mapCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError> {
    if (line.arguments().size() != 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: map <name>");
    }
    const std::string& name = line.arguments().front();
    auto session = startSession(*m_content, name, autoSwitchOf(m_cvars->player), *m_gameCvars);
    if (!session) {
        return nx::fail(ConsoleError::CommandFailed, std::format("map {}", name), std::move(session.error()));
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
    m_console.print(m_session->game->toggleNoclip() ? "noclip ON" : "noclip OFF");
}

void Client::lastWeaponCommand() {
    if (!m_session) {
        m_console.print("Can't \"lastinv\", not playing a map");
        return;
    }
    if (const std::optional<cs::WeaponIndex> last = m_session->game->inventory().lastWeapon()) {
        m_input->selectWeapon(*last);
    }
}

void Client::openWindowText(std::uint32_t screenHeight) {
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
    // The cursor shows and the keys type while it is open; the mouse turns the view again once closed.
    for (const auto& failure :
         {m_video->window.setTextInput(isOpen ? nx::platform::TextInput::On : nx::platform::TextInput::Off),
          m_video->window.setMouseMode(isOpen ? nx::platform::MouseMode::Absolute
                                              : nx::platform::MouseMode::Relative)}) {
        if (!failure) {
            nx::log::warning(kLog, "{}", nx::describe(failure.error()));
        }
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
        const nx::Seconds interval = nx::settings::frameIntervalOf(m_cvars->video, players);
        m_video->sleepTimer.sleepUntil(
            m_frameStart + std::chrono::duration_cast<nx::platform::MonotonicClock::duration>(interval));
        m_frameStart = nx::platform::MonotonicClock::now();
        m_frameRate.frameStarted(m_frameStart);
        if (auto read = readInput(); !read) {
            return read;
        }
        // What the keys queued, and what a `wait` left, before the frame's commands are made.
        std::ignore = m_console.execute();
        if (m_audio != nullptr) {
            // The original is silent while its window is not focused.
            const bool isHeard = m_isFocused || m_cvars->audio.playUnfocused.value();
            m_audio->setMuting(isHeard ? cs::Muting::Off : cs::Muting::On);
            m_audio->update();
        }
        startNextSession();
        updateWeapons();
        const nx::Seconds elapsed = m_frameClock.restart();
        m_input->turn(m_camera.angles, elapsed);
        // A session plays once its scene is made, from the frame after the first image of it: what the
        // player hears of it, they see.
        if (m_session && m_scene) {
            const cs::Hearing hearing = m_session->game->hearing(); // from what the last frame showed
            // The player's own setting, as its client tells the server of it.
            m_session->game->setAutoSwitch(autoSwitchOf(m_cvars->player));
            m_camera.position = m_session->game->play(*m_input, m_camera.angles, elapsed);
            turnAsTheGameDid();
            hearSession(hearing);
            stampBulletHoles();
        }
        if (auto drawn = drawFrame(); !drawn) {
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
        if (isConsoleOpen && !std::holds_alternative<nx::platform::CloseRequested>(event) &&
            !std::holds_alternative<nx::platform::Resized>(event)) {
            handleInConsole(event);
        } else if (std::holds_alternative<nx::platform::CloseRequested>(event) ||
                   (key != nullptr && key->key == nx::platform::Key::Escape && key->isDown)) {
            m_isRunning = false; // until the menus come, Escape ends the client, whatever is bound to it
        } else if (const auto* moved = std::get_if<nx::platform::MouseMoved>(&event)) {
            look(m_camera.angles, *moved, m_cvars->mouse);
        } else if (const auto* resized = std::get_if<nx::platform::Resized>(&event)) {
            if (auto recreated = m_video->swapchain->resize(extentOf(resized->size)); !recreated) {
                return nx::fail(ClientError::GpuFailed, std::move(recreated.error()));
            }
        } else {
            m_keys.handle(event);
        }
    }
    return {};
}

void Client::openNamedSoundOutput() {
    const std::string& name = m_cvars->audio.device.value();
    if (name.empty()) {
        return;
    }
    auto audio = cs::ClientAudio::open(m_content->vfs, m_cvars->audio.volume, name);
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
    m_session = std::exchange(m_nextSession, std::nullopt);
    // The local game is one of more than one player, as a Counter-Strike server's: r_decals is held
    // to mp_decals as the original's client holds it when it enters one.
    if (auto held = nx::settings::enterMultiplayerGame(m_cvars->video); !held) {
        nx::log::warning(kLog, "r_decals not held to mp_decals: {}", nx::describe(held.error()));
    }
    m_viewModelEvents = {}; // a new game's first play is new
    m_viewPose.reset();     // what the old game showed went with its level
    m_clientEvents.clear();
    m_changedWeapons.clear(); // the new level read every weapon's view model
    readWeaponSounds(m_session->level->weapons());
    readPlayerSounds();
    m_camera.position = m_session->game->eye();
    turnAsTheGameDid();
    std::ignore = m_frameClock.restart(); // the loading is no time of the game's
}

void Client::turnAsTheGameDid() {
    if (const std::optional<nx::EulerAngles> turned = m_session->game->takeTurnedView()) {
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
                                      std::pair{"use", m_content->use.soundFiles()}}) {
        for (const std::string_view file : files) {
            if (auto read = m_audio->preload(file); !read) {
                nx::log::warning(kLog, "a sound of the {} cannot be read: {}", what,
                                 nx::describe(read.error()));
            }
        }
    }
}

void Client::hearSession(const cs::Hearing& hearing) {
    m_viewPose = m_session->game->viewModel();
    m_clientEvents = m_viewModelEvents.passed(
        m_viewPose ? m_session->level->viewModel(m_viewPose->weapon) : nullptr, m_viewPose);
    const std::vector<cs::SoundMade> sounds = m_session->game->takeSounds();
    if (m_audio == nullptr) {
        return;
    }
    for (const cs::SoundMade& made : sounds) {
        reportSound(m_audio->play(*made.sound, made.volume, hearing.delayOf(made.at)));
    }
    for (const cs::PassedEvent& passed : m_clientEvents) {
        if (passed.event->event == cs::kSoundEvent) {
            const nx::GameTime at = m_viewPose->start.after(passed.intoPlay, hearing.rate);
            reportSound(m_audio->playLocal(passed.event->options, kFullVolume, hearing.delayOf(at)));
        }
    }
}

void Client::stampBulletHoles() {
    const std::vector<cs::Impact> impacts = m_session->game->takeImpacts();
    if (!m_scene) {
        return;
    }
    // r_decals as it is now, as the original reads it at each decal.
    m_scene->setDecalLimit(decalLimitOf(m_cvars->video));
    const cs::Level& level = *m_session->level;
    for (const cs::Impact& impact : impacts) {
        const std::optional<std::string_view> name =
            cs::bulletDecalOf(level.bulletMarkOn(impact.on.model), m_bulletHoles);
        // One the installation lacks was reported as the level was read.
        if (const std::optional<nx::render::DecalImageIndex> decal =
                name ? level.decals().indexOf(*name) : std::nullopt) {
            std::ignore = m_scene->shootDecal(*decal, impact.position, impact.on);
        }
    }
}

void Client::reportSound(const nx::Result<void, cs::PlayError>& played) {
    if (!played && m_soundFailures.allow()) {
        nx::log::warning(kLog, "a sound not played: {} ({} others not played since the last report)",
                         nx::describe(played.error()), m_soundFailures.takeSuppressed());
    }
}

auto Client::drawFrame() -> nx::Result<void, ClientError> {
    nx::rhi::Device& device = *m_video->device;
    auto commands = device.beginFrame();
    if (!commands) {
        return nx::fail(ClientError::GpuFailed, std::move(commands.error()));
    }
    nx::rhi::CommandList& list = commands->get();
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
                                                format, gammaOf(m_cvars->video));
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
    }
    if (m_session && !m_scene) {
        // r_fidelity, read as the map's scene is made.
        const nx::render::StudioLook look =
            m_cvars->video.fidelity.value() == nx::settings::Fidelity::Enhanced
                ? nx::render::StudioLook::Enhanced
                : nx::render::StudioLook::Legacy;
        auto scene = cs::Scene::create(*m_video->device, list, *m_session->level, *m_shaders,
                                       m_frames->sceneFormatOf(look), m_frames->effectsFormatOf(look),
                                       gammaOf(m_cvars->video), look, decalLimitOf(m_cvars->video));
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
    return {};
}

auto Client::sessionView() -> cs::SceneView {
    // The view is drawn turned by the kick the player's weapon gave it, as the original's client adds
    // the punch to it.
    nx::render::Camera kicked = m_camera;
    const nx::EulerAngles punch = m_session->game->punch();
    kicked.angles = nx::EulerAngles{.pitch = m_camera.angles.pitch + punch.pitch,
                                    .yaw = m_camera.angles.yaw + punch.yaw,
                                    .roll = m_camera.angles.roll + punch.roll};
    m_placements = m_session->game->placements();
    m_hostages = m_session->game->hostages();
    return {.camera = kicked,
            .time = nx::Seconds{m_mapClock.elapsed()},
            .placements = m_placements,
            .hostages = m_hostages,
            .viewModel = m_viewPose,
            .clientEvents = m_clientEvents,
            .hand = m_hudCvars->hand()};
}

void Client::appendScreen(nx::rhi::Extent2d size) {
    m_screenQuads.clear();
    if (m_session && m_hud && m_cvars->video.hudDraw.value()) {
        // The HUD, over the scene, the knife's crosshair in the middle.
        const cs::PlayerStatus& status = m_session->game->status();
        m_hud->append(m_screenQuads,
                      {.health = status.health,
                       .armor = status.armor,
                       .money = status.money,
                       .ammo = cs::hudAmmoOf(m_session->game->inventory(), m_content->weapons),
                       .crosshairDistance = cs::kKnifeCrosshairDistance,
                       .roundTime = m_session->game->roundTimeShown()},
                      m_hudCvars->crosshair(), size);
    }
    // cl_showfps, over the HUD whether it is drawn or not, as the original's engine writes it over its
    // client's HUD, while a map is played.
    const std::optional<double> framesPerSecond = m_frameRate.framesPerSecond();
    if (m_session && m_fpsLabel && framesPerSecond && m_cvars->video.showFps.value()) {
        m_fpsLabel->append(m_screenQuads, *framesPerSecond);
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
    auto userFolder = nx::platform::userDirectory();
    if (!userFolder) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(userFolder.error()));
    }
    Client client{options, *userFolder};
    if (auto started = client.start(); !started) {
        return started;
    }
    return client.run();
}

} // namespace app
