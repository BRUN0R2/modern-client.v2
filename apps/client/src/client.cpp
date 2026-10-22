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
import nx.settings;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import cs.shared;
import cs.client;

namespace app {

namespace {

using nx::console::ConsoleError;

constexpr nx::log::Category kLog{"client"};

/// What no face covers: the void outside a map, seen through its leaks, and the screen without one.
constexpr nx::rhi::ClearColor kVoidColor{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
/// The key `next` binds `noclip` to, which Counter-Strike leaves free.
constexpr std::string_view kNoclipKey = "v";
/// How long a minimized window waits before it looks for events again.
constexpr std::chrono::milliseconds kMinimizedWait{50};

/// The console's output: replies on the standard output, failures on the standard error.
class PrintedOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override { std::println("{}", line); }
    void reportFailure(const nx::Failure<ConsoleError>& failure) override {
        std::println(stderr, "next: {}", nx::describe(failure));
    }
};

/// The original's picture as the video cvars set it.
[[nodiscard]] auto gammaOf(const nx::settings::VideoCvars& video) -> nx::render::GammaSettings {
    return {.gamma = video.gamma.value(),
            .brightness = video.brightness.value(),
            .textureGamma = video.textureGamma.value(),
            .lightGamma = video.lightGamma.value()};
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

/// Opens the window and the GPU, presenting as `isVsync` asks. Errors: WindowUnavailable, GpuFailed.
[[nodiscard]] auto openVideo(const ClientOptions& options, bool isVsync) -> nx::Result<Video, ClientError> {
    auto window = nx::platform::Window::create({.title = "Modern-Client.v2"});
    if (!window) {
        return nx::fail(ClientError::WindowUnavailable, std::move(window.error()));
    }
    auto native = window->nativeWindow();
    auto size = window->pixelSize();
    if (!native || !size) {
        return !native ? nx::fail(ClientError::WindowUnavailable, std::move(native.error()))
                       : nx::fail(ClientError::WindowUnavailable, std::move(size.error()));
    }
    auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "next", .window = *native, .isValidationEnabled = options.isValidationEnabled});
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

/// A map being played: its level, the game of it, and the view its player first had. The game goes
/// before the level it plays.
struct Session {
    std::unique_ptr<cs::Level> level;
    std::unique_ptr<cs::LocalGame> game;
    nx::render::Camera start{};
};

/// The map `name` of `content`, its game started at its first spawn. Errors: those of Level::load and
/// LocalGame::start.
[[nodiscard]] auto startSession(const cs::GameContent& content, std::string_view name)
    -> nx::Result<Session, cs::LevelError> {
    auto level = cs::Level::load(content, name);
    if (!level) {
        return std::unexpected{std::move(level.error())};
    }
    const nx::render::Camera start = cs::spawnView((*level)->map());
    auto game = cs::LocalGame::start(**level, start.position);
    if (!game) {
        return std::unexpected{std::move(game.error())};
    }
    return Session{.level = std::move(*level), .game = std::move(*game), .start = start};
}

/// The client: the player's settings and console, the map it plays, and the window and GPU it plays
/// it in. It stays where it is made: the console's commands and the binds' input point into it.
class Client {
public:
    Client(const ClientOptions& options, const std::filesystem::path& userFolder)
        : m_options{&options}, m_files{userFolder}, m_console{m_output, m_files},
          m_bindings{nx::settings::defaultBindings()}, m_keys{m_console, m_bindings} {}

    Client(const Client&) = delete;
    auto operator=(const Client&) -> Client& = delete;
    Client(Client&&) = delete;
    auto operator=(Client&&) -> Client& = delete;
    ~Client() {
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
    [[nodiscard]] auto mapCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError>;
    void noclipCommand();
    [[nodiscard]] auto readInput() -> nx::Result<void, ClientError>;
    void startNextSession();
    [[nodiscard]] auto drawFrame() -> nx::Result<void, ClientError>;
    [[nodiscard]] auto upload(nx::rhi::CommandList& list, nx::rhi::Format format)
        -> nx::Result<void, ClientError>;
    [[nodiscard]] auto drawSession(nx::rhi::CommandList& list, const nx::rhi::SwapchainImage& frame)
        -> nx::Result<void, ClientError>;
    [[nodiscard]] auto finish() -> nx::Result<void, ClientError>;

    const ClientOptions* m_options;
    PrintedOutput m_output;
    nx::settings::UserConfigFiles m_files;
    nx::console::Console m_console;
    nx::settings::KeyBindings m_bindings;
    nx::settings::BoundInput m_keys;
    std::optional<nx::settings::ClientCvars> m_cvars;
    std::optional<cs::PlayerInput> m_input;
    std::optional<cs::GameContent> m_content;
    std::optional<cs::SceneShaders> m_shaders;
    std::vector<std::byte> m_screenShader;
    std::optional<Session> m_session;     ///< The map played.
    std::optional<Session> m_nextSession; ///< The map `map` loaded, played from the next frame.
    std::optional<Video> m_video;
    std::optional<cs::Scene> m_scene; ///< Of the session's level, made with the first image of it.
    std::optional<nx::render::ScreenRenderer> m_screen; ///< What draws the HUD, made with the first image.
    std::optional<cs::Hud> m_hud;                       ///< None when its pictures could not be read.
    std::vector<nx::render::ScreenQuad> m_screenQuads;  ///< What the screen draws in a frame.
    std::optional<nx::rhi::OwnedTexture> m_depth;
    nx::rhi::Extent2d m_depthExtent{};
    std::vector<nx::platform::WindowEvent> m_events;
    nx::render::Camera m_camera{};
    nx::platform::Stopwatch m_frameClock;
    nx::platform::Stopwatch m_mapClock;    ///< Since the map started: the light styles' time.
    nx::platform::Stopwatch m_deployClock; ///< Since the player drew the knife.
    nx::platform::Instant m_frameStart = nx::platform::MonotonicClock::now();
    bool m_isRunning = true;
};

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
    auto content =
        cs::openGameContent(m_options->data.value_or(executable->parent_path() / "data"), m_options->install);
    if (!content) {
        return nx::fail(ClientError::ContentUnavailable, std::move(content.error()));
    }
    m_content = std::move(*content);
    auto shaders = cs::readSceneShaders(executable->parent_path() / "shaders");
    if (!shaders) {
        return nx::fail(ClientError::ContentUnavailable, std::move(shaders.error()));
    }
    m_shaders = std::move(*shaders);
    auto screenShader = nx::platform::readFile(executable->parent_path() / "shaders" / "screen.spv");
    if (!screenShader) {
        return nx::fail(ClientError::ContentUnavailable, std::move(screenShader.error()));
    }
    m_screenShader = std::move(*screenShader);
    // The player's files, then the command line's commands, as the original's valve.rc runs them.
    nx::settings::runStartupFiles(m_console, m_files);
    m_console.addText(m_options->bootCommands);
    std::ignore = m_console.execute();
    auto video = openVideo(*m_options, m_cvars->video.vsync.value());
    if (!video) {
        return std::unexpected{std::move(video.error())};
    }
    m_video = std::move(*video);
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
          add({.name = "noclip", .description = "Flies through everything, or walks again."},
              [this](Console&, const CommandLine&) {
                  noclipCommand();
                  return nx::Result<void, ConsoleError>{};
              }),
          add({.name = "quit", .description = "Ends the client."}, [this](Console&, const CommandLine&) {
              m_isRunning = false;
              return nx::Result<void, ConsoleError>{};
          })}) {
        if (!registered) {
            return nx::fail(ClientError::SettingsUnavailable, registered.error());
        }
    }
    return {};
}

auto Client::mapCommand(const nx::console::CommandLine& line) -> nx::Result<void, ConsoleError> {
    if (line.arguments().size() != 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: map <name>");
    }
    const std::string& name = line.arguments().front();
    auto session = startSession(*m_content, name);
    if (!session) {
        return nx::fail(ConsoleError::CommandFailed, std::format("map {}", name), std::move(session.error()));
    }
    m_nextSession = std::move(*session);
    return {};
}

void Client::noclipCommand() {
    if (!m_session) {
        m_console.print("Can't \"noclip\", not playing a map");
        return;
    }
    m_console.print(m_session->game->toggleNoclip() ? "noclip ON" : "noclip OFF");
}

auto Client::run() -> nx::Result<void, ClientError> {
    while (m_isRunning) {
        // fps_max: the limiter waits before the input is read, so that the frame shows the latest
        // (docs/05-rendering.md, section 12).
        if (const float fpsMax = m_cvars->video.fpsMax.value(); fpsMax > 0.0F) {
            m_video->sleepTimer.sleepUntil(m_frameStart +
                                           std::chrono::duration_cast<nx::platform::MonotonicClock::duration>(
                                               nx::Seconds{1.0F / fpsMax}));
        }
        m_frameStart = nx::platform::MonotonicClock::now();
        if (auto read = readInput(); !read) {
            return read;
        }
        // What the keys queued, and what a `wait` left, before the frame's commands are made.
        std::ignore = m_console.execute();
        startNextSession();
        const nx::Seconds elapsed = m_frameClock.restart();
        m_input->turn(m_camera.angles, elapsed);
        if (m_session) {
            m_camera.position = m_session->game->play(*m_input, m_camera.angles, elapsed);
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
        const auto* key = std::get_if<nx::platform::KeyChanged>(&event);
        if (std::holds_alternative<nx::platform::CloseRequested>(event) ||
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

void Client::startNextSession() {
    if (!m_nextSession) {
        return;
    }
    m_video->device->waitIdle(); // the old scene may still be drawing
    m_scene.reset();
    m_session = std::exchange(m_nextSession, std::nullopt);
    m_camera = m_session->start;
    std::ignore = m_frameClock.restart(); // the loading is no time of the game's
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
    if (!m_depth || m_depthExtent != frame.size) {
        auto created = device.createTexture({.label = "next.depth",
                                             .format = nx::rhi::Format::Depth32Float,
                                             .extent = frame.size,
                                             .usage = {nx::rhi::TextureUsage::DepthTarget}});
        if (!created) {
            return nx::fail(ClientError::GpuFailed, std::move(created.error()));
        }
        m_depth = std::move(*created); // the old one, if any, goes
        m_depthExtent = frame.size;
    }
    if (auto uploaded = upload(list, frame.format); !uploaded) {
        return uploaded;
    }
    list.transition(frame.texture, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    list.transition(m_depth->get(), nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
    const std::array colors{nx::rhi::ColorAttachment{.texture = frame.texture, .clear = kVoidColor}};
    list.beginRendering({.colors = colors,
                         .depth = nx::rhi::DepthAttachment{.texture = m_depth->get(), .clear = 0.0F},
                         .area = frame.size});
    if (auto drawn = drawSession(list, frame); !drawn) {
        return drawn;
    }
    list.endRendering();
    list.transition(frame.texture, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::Present);
    if (auto submitted = device.submitFrame(*m_video->swapchain); !submitted) {
        return nx::fail(ClientError::GpuFailed, std::move(submitted.error()));
    }
    return {};
}

/// Puts on the GPU what the frame draws that is not there yet, its uploads recorded in `list` before
/// the frame renders: the screen and the HUD's pictures, and the scene of the map played.
auto Client::upload(nx::rhi::CommandList& list, nx::rhi::Format format) -> nx::Result<void, ClientError> {
    if (!m_screen) {
        auto screen = nx::render::ScreenRenderer::create(
            *m_video->device,
            {.shaderCode = m_screenShader, .colorFormat = format, .gamma = gammaOf(m_cvars->video)});
        if (!screen) {
            return nx::fail(ClientError::GpuFailed, std::move(screen.error()));
        }
        m_screen.emplace(std::move(*screen));
        if (auto hud = cs::Hud::load(m_content->vfs, *m_screen, list); hud) {
            m_hud.emplace(std::move(*hud));
        } else {
            nx::log::warning(kLog, "the HUD is not drawn: {}",
                             nx::describe(hud.error())); // the game plays on
        }
    }
    if (m_session && !m_scene) {
        auto scene = cs::Scene::create(*m_video->device, list, *m_session->level, *m_shaders, format,
                                       gammaOf(m_cvars->video));
        if (!scene) {
            return nx::fail(ClientError::GpuFailed, std::move(scene.error()));
        }
        m_scene.emplace(std::move(*scene));
        std::ignore = m_mapClock.restart();    // the light styles start with the map
        std::ignore = m_deployClock.restart(); // and the player draws the knife
    }
    return {};
}

auto Client::drawSession(nx::rhi::CommandList& list, const nx::rhi::SwapchainImage& frame)
    -> nx::Result<void, ClientError> {
    if (!m_session) {
        return {}; // no map: the void
    }
    const std::vector<nx::render::ModelPlacement> placements = m_session->game->placements();
    if (auto drawn = m_scene->draw(
            list, *m_session->level,
            {.camera = m_camera,
             .aspect = static_cast<float>(frame.size.width) / static_cast<float>(frame.size.height),
             .time = nx::Seconds{m_mapClock.elapsed()},
             .placements = placements,
             .sinceDeploy = nx::Seconds{m_deployClock.elapsed()}});
        !drawn) {
        return nx::fail(ClientError::GpuFailed, std::move(drawn.error()));
    }
    if (!m_hud || !m_cvars->video.hudDraw.value()) {
        return {};
    }
    // The HUD, over the scene.
    const cs::PlayerStatus& status = m_session->game->status();
    m_screenQuads.clear();
    m_hud->append(m_screenQuads, {.health = status.health, .armor = status.armor, .money = status.money},
                  frame.size);
    if (auto drawn = m_screen->draw(list, {.size = frame.size, .quads = m_screenQuads}); !drawn) {
        return nx::fail(ClientError::GpuFailed, std::move(drawn.error()));
    }
    return {};
}

auto Client::finish() -> nx::Result<void, ClientError> {
    m_video->device->waitIdle();
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
