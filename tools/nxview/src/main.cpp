// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// nxview: the map viewer (TODO.md, M2). It renders one frame of a map of the Counter-Strike 1.6
// installation to a BMP file, as the client draws it (cs.client), from where it is told to look, and
// the HUD over it when asked.

#include <cstdio>
#include <cstdlib>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;
import cs.shared;
import cs.client;

namespace {

constexpr int kExitUnusable = 2; // the arguments, the content or the GPU
constexpr std::string_view kUsage =
    R"(usage: nxview --screenshot <file.bmp> [--map <name>] [--install <folder>]
              [--data <folder>] [--position "<x> <y> <z>"] [--angles "<pitch> <yaw> <roll>"]
              [--time <seconds>] [--size <width>x<height>] [--hud] [--validation]

Renders one frame of a map of the Counter-Strike 1.6 installation into a BMP file, without a window,
as the client draws it with the original's default settings: the world as the game makes it when
the map starts, its sprites and studio models, and the knife in the player's hand. To play a map,
run next.

  --screenshot <file.bmp> the file the frame is written to
  --map <name>            the map of maps/<name>.bsp (default: de_dust2)
  --install <folder>      the installation: the folder with cstrike/ and valve/ (default: the one
                          Steam installed)
  --data <folder>         the data/ folder of the project (default: the one next to nxview)
  --position "<x> <y> <z>" where the camera is, the eye of the player (default: a standing
                          player's eye at the first spawn)
  --angles "<p> <y> <r>"  where it looks, in degrees: pitch (down is positive), yaw, roll (default:
                          the spawn's yaw)
  --time <seconds>        when the frame is, after the map starts (default: 0): what animates
                          (light styles, textures, water, the knife) is drawn as it is then
  --size <width>x<height> the frame's size in pixels (default: 1280x720)
  --hud                   the HUD over the frame: the player's as it spawns, the knife's crosshair
                          in Counter-Strike's default style
  --validation            the Khronos validation layer, when it is installed: its messages are
                          logged and nxview exits with 1 when it reported an error
)";

constexpr std::string_view kDefaultMap = "de_dust2";
constexpr nx::rhi::Extent2d kDefaultExtent{.width = 1280, .height = 720};
/// What no face covers: the void outside a map, seen through its leaks.
constexpr nx::rhi::ClearColor kVoidColor{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};

struct Options {
    std::string map{kDefaultMap};
    std::optional<std::filesystem::path> install;
    std::optional<std::filesystem::path> data;
    std::optional<std::filesystem::path> screenshot;
    std::optional<nx::Vec3> position;
    std::optional<nx::Vec3> angles; ///< Pitch, yaw and roll.
    nx::Seconds time{};             ///< Of the screenshot, after the map starts.
    nx::rhi::Extent2d size{kDefaultExtent};
    bool isHudShown = false;
    bool isValidationEnabled = false;
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
        if (!seen.insert(option).second) {
            return std::nullopt; // repeated
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
/// The bytes of a pixel of the readback, and where its red, green and blue are among them.
constexpr std::size_t kRgbaBytes = 4;
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
            const auto rgba =
                pixels.subspan(((std::size_t{row} * extent.width) + x) * kRgbaBytes, kRgbaBytes);
            file.insert(file.end(), {rgba[kBlue], rgba[kGreen], rgba[kRed]});
        }
        file.resize(file.size() + (rowBytes - (extent.width * kBmpBytesPerPixel)), std::byte{0});
    }
    return file;
}

/// Renders one frame of `level` from the camera's start into options.screenshot, without a window,
/// and the HUD over it, its pictures from `content` and drawn with `screenShader`, when the options ask.
[[nodiscard]] auto takeScreenshot(const Options& options, const cs::GameContent& content,
                                  const cs::Level& level, const cs::SceneShaders& shaders,
                                  std::span<const std::byte> screenShader) -> int {
    const nx::rhi::Extent2d extent = options.size;
    auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "nxview", .isValidationEnabled = options.isValidationEnabled});
    if (!device) {
        return unusable(device.error());
    }
    auto target = (*device)->createTexture(
        {.label = "nxview.screenshot",
         .format = nx::rhi::Format::Rgba8Unorm,
         .extent = extent,
         .usage = {nx::rhi::TextureUsage::RenderTarget, nx::rhi::TextureUsage::CopySource}});
    auto depth = (*device)->createTexture({.label = "nxview.depth",
                                           .format = nx::rhi::Format::Depth32Float,
                                           .extent = extent,
                                           .usage = {nx::rhi::TextureUsage::DepthTarget}});
    auto readback = (*device)->createBuffer({.label = "nxview.readback",
                                             .size = std::uint64_t{extent.width} * extent.height * kRgbaBytes,
                                             .usage = {nx::rhi::BufferUsage::CopyDestination},
                                             .location = nx::rhi::MemoryLocation::Readback});
    if (!target || !depth || !readback) {
        return unusable(!target ? target.error() : !depth ? depth.error() : readback.error());
    }
    auto commands = (*device)->beginFrame();
    if (!commands) {
        return unusable(commands.error());
    }
    nx::rhi::CommandList& list = commands->get();
    auto scene = cs::Scene::create(**device, list, level, shaders, nx::rhi::Format::Rgba8Unorm,
                                   nx::render::GammaSettings{});
    if (!scene) {
        return unusable(scene.error());
    }
    std::optional<nx::render::ScreenRenderer> screen;
    std::optional<cs::Hud> hud;
    if (options.isHudShown) {
        auto made = nx::render::ScreenRenderer::create(
            **device, {.shaderCode = screenShader, .colorFormat = nx::rhi::Format::Rgba8Unorm});
        if (!made) {
            return unusable(made.error());
        }
        screen.emplace(std::move(*made));
        auto loaded = cs::Hud::load(content.vfs, *screen, list);
        if (!loaded) {
            return unusable(loaded.error());
        }
        hud.emplace(std::move(*loaded));
    }
    const nx::render::Camera camera = startCamera(options, level.map());
    // The brush entities where the game puts them as the map starts, and the knife the player has held
    // since it started.
    auto game = cs::LocalGame::start(level, camera.position);
    if (!game) {
        return unusable(game.error());
    }
    const std::vector<nx::render::ModelPlacement> placements = (*game)->placements();
    list.transition(target->get(), nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    list.transition(depth->get(), nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
    const std::array colors{nx::rhi::ColorAttachment{.texture = target->get(), .clear = kVoidColor}};
    list.beginRendering({.colors = colors,
                         .depth = nx::rhi::DepthAttachment{.texture = depth->get(), .clear = 0.0F},
                         .area = extent});
    if (auto drawn =
            scene->draw(list, level,
                        {.camera = camera,
                         .aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height),
                         .time = options.time,
                         .placements = placements,
                         .sinceDeploy = options.time});
        !drawn) {
        return unusable(drawn.error());
    }
    if (hud) {
        const cs::PlayerStatus& status = (*game)->status();
        std::vector<nx::render::ScreenQuad> quads;
        hud->append(quads,
                    {.health = status.health,
                     .armor = status.armor,
                     .money = status.money,
                     .crosshairDistance = cs::kKnifeCrosshairDistance},
                    cs::CrosshairStyle{}, extent);
        if (auto drawn = screen->draw(list, {.size = extent, .quads = quads}); !drawn) {
            return unusable(drawn.error());
        }
    }
    list.endRendering();
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
    auto content = cs::openGameContent(options->data.value_or(programFolder / "data"), options->install);
    if (!content) {
        return unusable(content.error());
    }
    auto level = cs::Level::load(*content, options->map);
    if (!level) {
        return unusable(level.error());
    }
    auto shaders = cs::readSceneShaders(programFolder / "shaders");
    if (!shaders) {
        return unusable(shaders.error());
    }
    auto screenShader = nx::platform::readFile(programFolder / "shaders" / "screen.spv");
    if (!screenShader) {
        return unusable(screenShader.error());
    }
    return takeScreenshot(*options, *content, **level, *shaders, *screenShader);
}
