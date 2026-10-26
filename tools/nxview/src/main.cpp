// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// nxview: the map viewer (TODO.md, M2). It renders one frame of a map of the Counter-Strike 1.6
// installation to a BMP file, as the client draws it (cs.client), from where it is told to look, and
// the HUD and the console's window over it when asked.

#include <cstdio>
#include <cstdlib>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import nx.ui;
import nx.settings;
import cs.shared;
import cs.client;

namespace {

constexpr int kExitUnusable = 2; // the arguments, the content or the GPU
constexpr std::string_view kUsage =
    R"(usage: nxview --screenshot <file.bmp> [--map <name>] [--install <folder>]
              [--data <folder>] [--user-folder <folder>] [--look legacy|enhanced] [--weapon <id>]
              [--position "<x> <y> <z>"] [--angles "<pitch> <yaw> <roll>"]
              [--time <seconds>] [--size <width>x<height>] [--hud] [--console <line>]...
              [--typing <text>] [--validation]

Renders one frame of a map of the Counter-Strike 1.6 installation into a BMP file, without a window,
as the client draws it with the original's default settings: the world as the game makes it when
the map starts, its sprites and studio models, and the weapon in the player's hand, what a player
of the first spawn's team draws as it spawns. To play a map, run next.

  --screenshot <file.bmp> the file the frame is written to
  --map <name>            the map of maps/<name>.bsp (default: de_dust2)
  --install <folder>      the installation: the folder with cstrike/ and valve/ (default: the one
                          Steam installed)
  --data <folder>         the data/ folder of the project (default: the one next to nxview)
  --user-folder <folder>  load local custom content from this folder's addons/cstrike/;
                          omitted by default, without reading or writing the user's settings
  --look <look>           legacy (default) or enhanced: HDR, bloom and baked map reflections
  --weapon <id>           spawn with only this weapon of the catalog, by id or classname
  --position "<x> <y> <z>" where the camera is, the eye of the player (default: a standing
                          player's eye at the first spawn)
  --angles "<p> <y> <r>"  where it looks, in degrees: pitch (down is positive), yaw, roll (default:
                          the spawn's yaw)
  --time <seconds>        when the frame is, after the map starts (default: 0): what animates
                          (light styles, textures, water, the weapon in hand) is drawn as it is
                          then, the player standing still
  --size <width>x<height> the frame's size in pixels (default: 1280x720)
  --hud                   the HUD over the frame: the player's as it spawns, the knife's crosshair
                          in Counter-Strike's default style
  --console <line>        the console's window over the frame, <line> typed and sent in it; once
                          for each line, in their order (a console of the built-in commands)
  --typing <text>         the console's window over the frame, <text> typed in it and not sent,
                          the names it completes to offered
  --validation            the Khronos validation layer, when it is installed: its messages are
                          logged and nxview exits with 1 when it reported an error
)";

constexpr std::string_view kDefaultMap = "de_dust2";
constexpr nx::rhi::Extent2d kDefaultExtent{.width = 1280, .height = 720};

struct Options {
    std::string map{kDefaultMap};
    std::optional<std::filesystem::path> install;
    std::optional<std::filesystem::path> data;
    std::optional<std::filesystem::path> userFolder;
    std::optional<std::filesystem::path> screenshot;
    std::optional<std::string> weapon;
    nx::render::StudioLook look = nx::render::StudioLook::Legacy;
    std::optional<nx::Vec3> position;
    std::optional<nx::Vec3> angles; ///< Pitch, yaw and roll.
    nx::Seconds time{};             ///< Of the screenshot, after the map starts.
    nx::rhi::Extent2d size{kDefaultExtent};
    bool isHudShown = false;
    bool isValidationEnabled = false;
    std::vector<std::string> consoleLines; ///< Sent in the console's window.
    std::optional<std::string> typing;     ///< Left typed in it.

    /// Whether the console's window is drawn.
    [[nodiscard]] auto isConsoleShown() const noexcept -> bool { return !consoleLines.empty() || typing; }
};

/// The size `text` writes as <width>x<height>, both above 0; nullopt when it does not.
[[nodiscard]] auto sizeOfText(std::string_view text) -> std::optional<nx::rhi::Extent2d> {
    const std::size_t by = text.find('x');
    if (by == std::string_view::npos) {
        return std::nullopt;
    }
    nx::rhi::Extent2d size{};
    const std::string_view width = text.substr(0, by);
    const std::string_view height = text.substr(by + 1);
    const auto [widthEnd, widthError] =
        std::from_chars(width.data(), width.data() + width.size(), size.width);
    const auto [heightEnd, heightError] =
        std::from_chars(height.data(), height.data() + height.size(), size.height);
    if (widthError != std::errc{} || heightError != std::errc{} || widthEnd != width.data() + width.size() ||
        heightEnd != height.data() + height.size() || size.width == 0 || size.height == 0) {
        return std::nullopt;
    }
    return size;
}

/// The options in `arguments`, the command line without the program; nullopt when they are not
/// valid options.
[[nodiscard]] auto parseOptions(std::span<const std::string_view> arguments) -> std::optional<Options> {
    Options options;
    std::set<std::string_view> seen;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view option = arguments[index];
        if (option != "--console" && !seen.insert(option).second) {
            return std::nullopt; // repeated, but the console's lines
        }
        if (option == "--validation" || option == "--hud") {
            (option == "--hud" ? options.isHudShown : options.isValidationEnabled) = true;
            continue;
        }
        if (index + 1 == arguments.size()) {
            return std::nullopt; // the other options have a value
        }
        const std::string_view value = arguments[++index];
        if (option == "--map") {
            options.map = std::string{value};
        } else if (option == "--install") {
            options.install = nx::platform::fromUtf8(value);
        } else if (option == "--data") {
            options.data = nx::platform::fromUtf8(value);
        } else if (option == "--user-folder") {
            options.userFolder = nx::platform::fromUtf8(value);
        } else if (option == "--weapon") {
            options.weapon = std::string{value};
        } else if (option == "--look") {
            if (value != "legacy" && value != "enhanced") {
                return std::nullopt;
            }
            options.look =
                value == "enhanced" ? nx::render::StudioLook::Enhanced : nx::render::StudioLook::Legacy;
        } else if (option == "--screenshot") {
            options.screenshot = nx::platform::fromUtf8(value);
        } else if (option == "--time") {
            float seconds = 0.0F;
            if (std::from_chars(value.data(), value.data() + value.size(), seconds).ec != std::errc{} ||
                seconds < 0.0F) {
                return std::nullopt;
            }
            options.time = nx::Seconds{seconds};
        } else if (option == "--size") {
            const std::optional<nx::rhi::Extent2d> size = sizeOfText(value);
            if (!size) {
                return std::nullopt;
            }
            options.size = *size;
        } else if (option == "--console") {
            options.consoleLines.emplace_back(value);
        } else if (option == "--typing") {
            options.typing = std::string{value};
        } else if (option == "--position" || option == "--angles") {
            const std::optional<nx::Vec3> numbers = nx::assets::vectorOfValue(value);
            if (!numbers) {
                return std::nullopt;
            }
            (option == "--position" ? options.position : options.angles) = numbers;
        } else {
            return std::nullopt;
        }
    }
    return options;
}

/// The console's replies, shown in its window.
class WindowOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override { window->print(line); }
    void reportFailure(const nx::Failure<nx::console::ConsoleError>& failure) override {
        window->reportFailure(failure);
    }

    nx::ui::ConsoleDialog* window = nullptr;
};

/// No file for `exec` to read.
class NoFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
    }
};

/// Prints `failure` and returns kExitUnusable.
template <class Code>
[[nodiscard]] auto unusable(const nx::Failure<Code>& failure) -> int {
    std::println(stderr, "nxview: {}", nx::describe(failure));
    return kExitUnusable;
}

/// Where the camera is: at the first spawn, but where the options put it.
[[nodiscard]] auto startCamera(const Options& options, const nx::assets::BspView& map) -> nx::render::Camera {
    nx::render::Camera camera = cs::spawnView(map);
    if (options.position) {
        camera.position = *options.position;
    }
    if (options.angles) {
        camera.angles = {.pitch = nx::Degrees{options.angles->x},
                         .yaw = nx::Degrees{options.angles->y},
                         .roll = nx::Degrees{options.angles->z}};
    }
    return camera;
}

// A BMP file of 24-bit pixels: its file header, its BITMAPINFOHEADER, then its rows of blue, green
// and red bytes from the bottom up, each padded to a multiple of four bytes.
constexpr std::uint32_t kBmpFileHeaderBytes = 14;
constexpr std::uint32_t kBmpInfoHeaderBytes = 40;
constexpr std::uint16_t kBmpPlanes = 1;
constexpr std::uint32_t kBmpBytesPerPixel = 3; // blue, green and red
constexpr auto kBmpBitsPerPixel =
    nx::narrow<std::uint16_t>(kBmpBytesPerPixel * std::numeric_limits<std::uint8_t>::digits);
constexpr std::uint32_t kBmpUncompressed = 0;
constexpr std::int32_t kBmpPixelsPerMeter = 2835; // 72 dpi
constexpr std::uint32_t kBmpRowAlignment = 4;
/// Where a pixel of the readback, of nx::assets::kRgbaBytes, has its red, green and blue.
constexpr std::size_t kRed = 0;
constexpr std::size_t kGreen = 1;
constexpr std::size_t kBlue = 2;

/// The BMP file of `pixels`, RGBA rows from the top, of `extent`.
[[nodiscard]] auto bmpOf(nx::rhi::Extent2d extent, std::span<const std::byte> pixels)
    -> std::vector<std::byte> {
    constexpr std::uint32_t kHeaderBytes = kBmpFileHeaderBytes + kBmpInfoHeaderBytes;
    const std::uint32_t rowBytes =
        (((extent.width * kBmpBytesPerPixel) + kBmpRowAlignment - 1) / kBmpRowAlignment) * kBmpRowAlignment;
    const std::uint32_t imageBytes = rowBytes * extent.height;
    std::vector<std::byte> file;
    file.reserve(kHeaderBytes + imageBytes);
    const auto put = [&file]<class Value>(Value value) {
        const auto raw = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // little-endian hosts
        file.insert(file.end(), raw.begin(), raw.end());
    };
    file.insert(file.end(), {std::byte{'B'}, std::byte{'M'}});
    put(kHeaderBytes + imageBytes);
    put(std::uint32_t{0}); // reserved
    put(kHeaderBytes);
    put(kBmpInfoHeaderBytes);
    put(nx::narrow<std::int32_t>(extent.width));
    put(nx::narrow<std::int32_t>(extent.height)); // positive: rows from the bottom
    put(kBmpPlanes);
    put(kBmpBitsPerPixel);
    put(kBmpUncompressed);
    put(imageBytes);
    put(kBmpPixelsPerMeter);
    put(kBmpPixelsPerMeter);
    put(std::uint32_t{0}); // no palette
    put(std::uint32_t{0}); // every color matters
    for (std::uint32_t row = extent.height; row-- > 0;) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto rgba = pixels.subspan(((std::size_t{row} * extent.width) + x) * nx::assets::kRgbaBytes,
                                             nx::assets::kRgbaBytes);
            file.insert(file.end(), {rgba[kBlue], rgba[kGreen], rgba[kRed]});
        }
        file.resize(file.size() + (rowBytes - (extent.width * kBmpBytesPerPixel)), std::byte{0});
    }
    return file;
}

/// Renders one frame of `level` from the camera's start into options.screenshot, without a window, as
/// the client draws its frames (cs::FrameRenderer, drawing with `frameShaders`), and the HUD and the
/// console's window over it, their pictures from `content`, when the options ask.
[[nodiscard]] auto takeScreenshot(const Options& options, const cs::GameContent& content,
                                  const cs::Level& level, const cs::SceneShaders& shaders,
                                  cs::FrameShaders frameShaders) -> int {
    const nx::rhi::Extent2d extent = options.size;
    constexpr nx::rhi::Format kFormat = nx::rhi::Format::Rgba8Unorm;
    auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "nxview", .isValidationEnabled = options.isValidationEnabled});
    if (!device) {
        return unusable(device.error());
    }
    auto target = (*device)->createTexture(
        {.label = "nxview.screenshot",
         .format = kFormat,
         .extent = extent,
         .usage = {nx::rhi::TextureUsage::RenderTarget, nx::rhi::TextureUsage::CopySource}});
    auto readback =
        (*device)->createBuffer({.label = "nxview.readback",
                                 .size = std::uint64_t{extent.width} * extent.height * nx::assets::kRgbaBytes,
                                 .usage = {nx::rhi::BufferUsage::CopyDestination},
                                 .location = nx::rhi::MemoryLocation::Readback});
    if (!target || !readback) {
        return unusable(!target ? target.error() : readback.error());
    }
    // The original's default settings.
    auto frames =
        cs::FrameRenderer::create(**device, std::move(frameShaders), kFormat, nx::render::GammaSettings{});
    if (!frames) {
        return unusable(frames.error());
    }
    auto commands = (*device)->beginFrame();
    if (!commands) {
        return unusable(commands.error());
    }
    nx::rhi::CommandList& uploadCommands = commands->get();
    // The original's default settings in a game of more than one player, as next plays it.
    const auto decalLimit = nx::narrow<std::size_t>(nx::settings::decalsInMultiplayer(
        nx::settings::kDefaultDecals, nx::settings::kDefaultMultiplayerDecals));
    auto scene = cs::Scene::create(**device, uploadCommands, level, shaders,
                                   frames->sceneFormatOf(options.look), frames->effectsFormatOf(options.look),
                                   nx::render::GammaSettings{}, options.look, decalLimit);
    if (!scene) {
        return unusable(scene.error());
    }
    std::optional<cs::Hud> hud;
    if (options.isHudShown) {
        auto loaded = cs::Hud::load(content.vfs, content.weapons.ammo(), frames->screen(), uploadCommands);
        if (!loaded) {
            return unusable(loaded.error());
        }
        hud.emplace(std::move(*loaded));
    }
    // The console's window, its lines sent and run, its typing left.
    WindowOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    // The rules the game is played by, which the console's lines may set.
    auto gameCvars = cs::GameCvars::registerIn(console);
    if (!gameCvars) {
        return unusable(gameCvars.error());
    }
    nx::ui::FontCatalog fonts;
    std::unique_ptr<cs::ConsoleWindow> consoleWindow;
    if (options.isConsoleShown()) {
        consoleWindow = cs::ConsoleWindow::open(content.vfs, fonts, console, extent.height);
        if (consoleWindow == nullptr) {
            std::println(stderr, "nxview: the console has no window");
            return kExitUnusable;
        }
        if (auto loaded = consoleWindow->load(frames->screen(), uploadCommands, extent); !loaded) {
            return unusable(loaded.error());
        }
        std::ignore = consoleWindow->toggle(); // open, as the frame shows it
        nx::ui::ConsoleDialog& dialog = *consoleWindow->dialog();
        output.window = &dialog;
        for (const std::string& line : options.consoleLines) {
            dialog.insertText(line);
            std::ignore = dialog.pressKey(nx::platform::Key::Enter); // Enter, which it takes
            std::ignore = console.execute();                         // its lines run as they are sent
        }
        dialog.insertText(options.typing.value_or(std::string{}));
    }
    const nx::render::Camera camera = startCamera(options, level.map());
    // The brush entities where the game puts them, and the weapon the player drew as the map started,
    // the player standing still until the frame, its eye where the options put the camera.
    // The original's default setting: _cl_autowepswitch 1.
    auto game = cs::LocalGame::start(content, level, cs::spawnTeam(level.map()), cs::AutoSwitch::ToHeavier,
                                     *gameCvars);
    if (!game) {
        return unusable(game.error());
    }
    if (options.position) {
        (*game)->placeEye(camera.position);
    }
    std::ignore = (*game)->wait(camera.angles, options.time);
    const std::vector<nx::render::ModelPlacement> placements = (*game)->placements();
    const std::vector<cs::ShownHostage> hostages = (*game)->hostages();
    // The client events of the play the view model is in, from its first frame: the muzzle flashes
    // a shot shows.
    const std::optional<cs::ViewModelPose> viewPose = (*game)->viewModel();
    cs::ViewModelEvents viewEvents;
    const std::vector<cs::PassedEvent> clientEvents =
        viewEvents.passed(viewPose ? level.viewModel(viewPose->weapon) : nullptr, viewPose);
    if (options.look == nx::render::StudioLook::Enhanced) {
        // Each bake owns a frame's world uniforms. Finish every probe at the screenshot's fixed time
        // before drawing it, so the result does not depend on how quickly the GPU runs.
        const std::size_t probeCount = std::min(level.probePositions().size(), nx::render::kMostProbes);
        for (std::size_t probe = 0; probe < probeCount; ++probe) {
            scene->bakeReflections(commands->get(), level, options.time, placements);
            if (auto submitted = (*device)->submitFrame(); !submitted) {
                return unusable(submitted.error());
            }
            (*device)->waitIdle();
            commands = (*device)->beginFrame();
            if (!commands) {
                return unusable(commands.error());
            }
        }
    }
    std::vector<nx::render::ScreenQuad> quads;
    if (hud) {
        const cs::PlayerStatus& status = (*game)->status();
        hud->append(quads,
                    {.health = status.health,
                     .armor = status.armor,
                     .money = status.money,
                     .ammo = cs::hudAmmoOf((*game)->inventory(), content.weapons),
                     .crosshairDistance = cs::kKnifeCrosshairDistance,
                     .roundTime = (*game)->roundTimeShown()},
                    cs::CrosshairStyle{}, extent);
    }
    if (consoleWindow != nullptr) {
        consoleWindow->paint(quads, extent);
    }
    nx::rhi::CommandList& list = commands->get();
    const std::optional<cs::FrameScene> shown{cs::FrameScene{.scene = *scene,
                                                             .level = level,
                                                             .view = {.camera = camera,
                                                                      .time = options.time,
                                                                      .placements = placements,
                                                                      .hostages = hostages,
                                                                      .viewModel = viewPose,
                                                                      .clientEvents = clientEvents}}};
    if (auto recorded = frames->record(list, target->get(), extent, shown, quads); !recorded) {
        return unusable(recorded.error());
    }
    list.transition(target->get(), nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::CopySource);
    list.transition(readback->get(), nx::rhi::BufferState::Undefined, nx::rhi::BufferState::CopyDestination);
    list.copyTextureToBuffer({.buffer = readback->get(), .texture = target->get(), .extent = extent});
    list.transition(readback->get(), nx::rhi::BufferState::CopyDestination, nx::rhi::BufferState::HostRead);
    if (auto submitted = (*device)->submitFrame(); !submitted) {
        return unusable(submitted.error());
    }
    (*device)->waitIdle();
    if (auto written = nx::platform::writeFile(*options.screenshot,
                                               bmpOf(extent, (*device)->mappedBytes(readback->get())));
        !written) {
        return unusable(written.error());
    }
    const nx::render::FrameStats& drawn = scene->lastFrame();
    std::println("nxview: {} from {} at {}x{}: {} of {} faces, {} triangles, in {} leaves",
                 nx::platform::toUtf8(*options.screenshot), options.map, extent.width, extent.height,
                 drawn.faces, scene->worldStats().faces, drawn.triangles, drawn.leaves);
    return (*device)->validationErrors() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

auto main(int argumentCount, char** argumentValues) -> int {
    // UTF-8 everywhere: on Windows, the manifest of the program makes it the code page (nxProgram).
    const auto arguments = std::span{argumentValues, nx::narrow<std::size_t>(argumentCount)} |
                           std::views::drop(1) | std::ranges::to<std::vector<std::string_view>>();
    if (std::ranges::contains(arguments, std::string_view{"--help"})) {
        std::print("{}", kUsage);
        return EXIT_SUCCESS;
    }
    const std::optional<Options> options = parseOptions(arguments);
    if (!options || !options->screenshot) {
        std::print(stderr, "{}", kUsage);
        return kExitUnusable;
    }
    auto executable = nx::platform::executablePath();
    if (!executable) {
        return unusable(executable.error());
    }
    const std::filesystem::path programFolder = executable->parent_path();
    // Local custom content is opt-in; no settings are read or written by the screenshot tool.
    const auto addons = options->userFolder.transform(
        [](const std::filesystem::path& userFolder) { return cs::addonsFolderOf(userFolder); });
    if (std::error_code error; addons && !std::filesystem::is_directory(*addons, error)) {
        std::println(stderr, "nxview: no local custom content folder {}", nx::platform::toUtf8(*addons));
        return kExitUnusable;
    }
    auto content =
        cs::openGameContent(options->data.value_or(programFolder / "data"), options->install, addons);
    if (!content) {
        return unusable(content.error());
    }
    std::optional<cs::WeaponIndex> selectedWeapon;
    if (options->weapon) {
        selectedWeapon = cs::findWeaponNamed(content->weapons, *options->weapon);
        if (!selectedWeapon) {
            std::println(stderr, "nxview: no weapon {:?} in the catalog", *options->weapon);
            return kExitUnusable;
        }
        if (auto complete = content->weapons.checkComplete(*selectedWeapon); !complete) {
            return unusable(complete.error());
        }
        // Keep the normal spawn and animation path, with an isolated in-memory loadout for the study.
        const std::string& id = content->weapons.weapon(*selectedWeapon).id;
        content->loadouts.loadouts = {{.team = cs::Team::Terrorists, .weapons = {id}},
                                      {.team = cs::Team::CounterTerrorists, .weapons = {id}}};
    }
    auto level = cs::Level::load(*content, options->map);
    if (!level) {
        return unusable(level.error());
    }
    if (selectedWeapon && (*level)->viewModel(*selectedWeapon) == nullptr) {
        std::println(stderr, "nxview: weapon {:?} has no readable view model", *options->weapon);
        return kExitUnusable;
    }
    auto shaders = cs::readSceneShaders(programFolder / "shaders");
    if (!shaders) {
        return unusable(shaders.error());
    }
    auto frameShaders = cs::readFrameShaders(programFolder / "shaders");
    if (!frameShaders) {
        return unusable(frameShaders.error());
    }
    return takeScreenshot(*options, *content, **level, *shaders, std::move(*frameShaders));
}
