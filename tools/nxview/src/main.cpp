// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// nxview: the map viewer (TODO.md, M2). It opens a map of the Counter-Strike 1.6 installation and
// flies a free camera through its world, or renders one frame of it to a BMP file.

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

namespace {

constexpr int kExitUnusable = 2; // the arguments, the content, the window or the GPU
constexpr std::string_view kUsage = R"(usage: nxview [--map <name>] [--install <folder>] [--data <folder>]
              [--position "<x> <y> <z>"] [--angles "<pitch> <yaw> <roll>"]
              [--screenshot <file.bmp>] [--validation]

Opens a map of the Counter-Strike 1.6 installation and flies through it: the mouse looks, W A S D
move, Space and Ctrl rise and sink, Shift is faster, Escape ends it.

  --map <name>            the map of maps/<name>.bsp (default: de_dust2)
  --install <folder>      the installation: the folder with cstrike/ and valve/ (default: the one
                          Steam installed)
  --data <folder>         the data/ folder of the project (default: the one next to nxview)
  --position "<x> <y> <z>" where the camera starts (default: a standing player's eye at the first
                          spawn)
  --angles "<p> <y> <r>"  where it looks, in degrees: pitch (down is positive), yaw, roll (default:
                          the spawn's yaw)
  --screenshot <file.bmp> renders one 1280x720 frame from the camera's start into the file, without
                          a window, and ends
  --validation            the Khronos validation layer, when it is installed: its messages are
                          logged and nxview exits with 1 when it reported an error
)";

constexpr std::string_view kDefaultMap = "de_dust2";
constexpr nx::rhi::Extent2d kScreenshotExtent{.width = 1280, .height = 720};
/// What no face covers: the void outside a map, seen through its leaks.
constexpr nx::rhi::ClearColor kVoidColor{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
/// A standing player's eye above the spawn's origin (the middle of the player's box), as in CS 1.6.
constexpr float kEyeHeight = 17.0F;
/// The camera's speed in map units per second, and how much faster it goes with Shift.
constexpr float kSpeed = 320.0F;
constexpr float kFastFactor = 3.0F;
/// Degrees per mouse count: the original's m_yaw and m_pitch, 0.022, at its default sensitivity, 3.
constexpr float kDegreesPerCount = 0.022F * 3.0F;
constexpr float kMaxPitch = 89.0F;
/// How long a minimized window waits before it looks for events again.
constexpr std::chrono::milliseconds kMinimizedWait{50};

struct Options {
    std::string map{kDefaultMap};
    std::optional<std::filesystem::path> install;
    std::optional<std::filesystem::path> data;
    std::optional<std::filesystem::path> screenshot;
    std::optional<nx::Vec3> position;
    std::optional<nx::Vec3> angles; ///< Pitch, yaw and roll.
    bool isValidationEnabled = false;
};

/// The three numbers of an entity's vector value ("-512 1024 36"); nullopt for other text.
[[nodiscard]] auto vectorOf(std::string_view text) -> std::optional<nx::Vec3> {
    std::array<float, 3> values{};
    const char* next = text.data();
    const char* const end = text.data() + text.size();
    for (float& value : values) {
        while (next != end && *next == ' ') {
            ++next;
        }
        const auto [stop, error] = std::from_chars(next, end, value);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        next = stop;
    }
    return nx::Vec3{.x = values[0], .y = values[1], .z = values[2]};
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
        if (option == "--validation") {
            options.isValidationEnabled = true;
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
        } else if (option == "--position" || option == "--angles") {
            const std::optional<nx::Vec3> numbers = vectorOf(value);
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

[[nodiscard]] auto unusable(std::string_view problem) -> int {
    std::println(stderr, "nxview: {}", problem);
    return kExitUnusable;
}

/// The map, its WADs, its sky and the shader, read once: the views point into these bytes.
struct Content {
    std::vector<std::byte> mapBytes;
    std::optional<nx::assets::BspView> map;
    std::vector<std::vector<std::byte>> wadBytes;
    std::vector<nx::assets::WadView> wads;
    std::vector<nx::assets::RgbaImage> sky; ///< Its sides, in the order of nx::render::kSkySuffixes.
    nx::render::LightStyles lightStyles;
    std::vector<std::byte> shader;
};

/// Where a player first stands: the first counter-terrorist spawn, else the first terrorist one.
[[nodiscard]] auto spawnCamera(const nx::assets::BspView& map) -> nx::render::Camera {
    for (const std::string_view classname : {"info_player_start", "info_player_deathmatch"}) {
        for (const nx::assets::BspEntity& entity : map.entities()) {
            if (entity.find("classname") != classname) {
                continue;
            }
            const std::optional<nx::Vec3> origin = vectorOf(entity.find("origin").value_or(""));
            const std::optional<nx::Vec3> angles = vectorOf(entity.find("angles").value_or("0 0 0"));
            if (!origin) {
                continue;
            }
            return nx::render::Camera{.position = *origin + nx::Vec3{.z = kEyeHeight},
                                      .angles = {.yaw = nx::Degrees{angles ? angles->y : 0.0F}}};
        }
    }
    return nx::render::Camera{};
}

/// Where the camera starts: at the first spawn, but where the options put it.
[[nodiscard]] auto startCamera(const Options& options, const nx::assets::BspView& map) -> nx::render::Camera {
    nx::render::Camera camera = spawnCamera(map);
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

/// The light styles the game sets for the map, as CS 1.6's game sets them: the world's patterns (the
/// Half-Life SDK's CWorld::Precache) and those of the switchable lights, from their entities.
[[nodiscard]] auto mapLightStyles(const nx::assets::BspView& map) -> nx::render::LightStyles {
    nx::render::LightStyles styles;
    for (const auto [style, pattern] : std::to_array<std::pair<std::size_t, std::string_view>>({
             {0, "m"},                                                   // normal
             {1, "mmnmmommommnonmmonqnmmo"},                             // flicker
             {2, "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba"}, // slow strong pulse
             {3, "mmmmmaaaaammmmmaaaaaabcdefgabcdefg"},                  // candle
             {4, "mamamamamama"},                                        // fast strobe
             {5, "jklmnopqrstuvwxyzyxwvutsrqponmlkj"},                   // gentle pulse
             {6, "nmonqnmomnmomomno"},                                   // flicker
             {7, "mmmaaaabcdefgmmmmaaaammmaamm"},                        // candle
             {8, "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa"},          // candle
             {9, "aaaaaaaazzzzzzzz"},                                    // slow strobe
             {10, "mmamammmmammamamaaamammma"},                          // fluorescent flicker
             {11, "abcdefghijklmnopqrrqponmlkjihgfedcba"},               // slow pulse, not to black
             {12, "mmnnmmnnnmmnn"},                                      // underwater
             {63, "a"},
         })) {
        styles.set(style, pattern);
    }
    // A light with a name and a style from 32 on can be switched: 'a' when it starts off, else its
    // pattern, else 'm' (CLight::Spawn).
    constexpr std::size_t kFirstSwitchableStyle = 32;
    constexpr int kStartsOff = 1;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::string_view classname = entity.find("classname").value_or("");
        if ((classname != "light" && classname != "light_spot" && classname != "light_environment") ||
            !entity.find("targetname")) {
            continue;
        }
        std::size_t style = 0;
        int flags = 0;
        const std::string_view styleText = entity.find("style").value_or("");
        const std::string_view flagsText = entity.find("spawnflags").value_or("0");
        if (std::from_chars(styleText.data(), styleText.data() + styleText.size(), style).ec != std::errc{} ||
            style < kFirstSwitchableStyle || style >= nx::render::kLightStyleCount) {
            continue;
        }
        std::ignore = std::from_chars(flagsText.data(), flagsText.data() + flagsText.size(), flags);
        styles.set(style, (flags & kStartsOff) != 0 ? "a" : entity.find("pattern").value_or("m"));
    }
    return styles;
}
/// The WADs the map's `wad` key names ("\half-life\cstrike\cs_dust.wad;..."), by their file names
/// at the root of the VFS, as the original looks them up. Maps often name WADs of their author's
/// computer that no texture of theirs needs; the ones the installation lacks are listed and left
/// out, and a texture none of the others has is reported by the world.
void readWads(const nx::assets::Vfs& vfs, Content& content) {
    const std::string_view list = content.map->entities().empty()
                                      ? std::string_view{}
                                      : content.map->entities()[0].find("wad").value_or("");
    std::vector<std::string_view> absent;
    for (const auto part : std::views::split(list, ';')) {
        const std::string_view entry{part};
        const std::string_view name = entry.substr(entry.find_last_of("\\/") + 1); // npos + 1 is 0
        if (name.empty()) {
            continue;
        }
        const auto path = nx::assets::VfsPath::parse(name);
        auto bytes = path ? vfs.readAll(*path) : nx::Result<std::vector<std::byte>, nx::assets::VfsError>{};
        if (!path || !bytes) {
            absent.push_back(name);
            continue;
        }
        content.wadBytes.push_back(std::move(*bytes));
    }
    if (!absent.empty()) {
        std::println("nxview: WADs the map names that the installation does not have: {}",
                     absent | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>());
    }
    for (const std::vector<std::byte>& bytes : content.wadBytes) {
        if (auto wad = nx::assets::WadView::parse(bytes)) {
            content.wads.push_back(std::move(*wad));
        }
    }
}

/// The sides of the sky `name`, gfx/env/<name><suffix>.tga; nullopt when one is missing or unreadable.
[[nodiscard]] auto readSkySides(const nx::assets::Vfs& vfs, std::string_view name)
    -> std::optional<std::vector<nx::assets::RgbaImage>> {
    std::vector<nx::assets::RgbaImage> sides;
    for (const std::string_view suffix : nx::render::kSkySuffixes) {
        const auto path = nx::assets::VfsPath::parse(std::format("gfx/env/{}{}.tga", name, suffix));
        auto bytes = path ? vfs.readAll(*path) : nx::Result<std::vector<std::byte>, nx::assets::VfsError>{};
        auto side =
            bytes ? nx::assets::readTga(*bytes) : nx::Result<nx::assets::RgbaImage, nx::assets::TgaError>{};
        if (!path || !bytes || !side) {
            return std::nullopt;
        }
        sides.push_back(std::move(*side));
    }
    return sides;
}

/// The sky the worldspawn's `skyname` names, or the default one when the installation lacks it, as
/// the original falls back; none when neither can be read: the world draws its sky gray.
void readSky(const nx::assets::Vfs& vfs, Content& content) {
    const std::string_view name =
        content.map->entities().empty()
            ? nx::render::kDefaultSkyName
            : content.map->entities()[0].find("skyname").value_or(nx::render::kDefaultSkyName);
    const std::array candidates{name, nx::render::kDefaultSkyName};
    for (const std::string_view candidate : std::span{candidates}.first(name == candidates[1] ? 1 : 2)) {
        if (auto sides = readSkySides(vfs, candidate)) {
            content.sky = std::move(*sides);
            return;
        }
        std::println("nxview: the sky {:?} is missing or unreadable", candidate);
    }
}

/// Reads the map `options.map` of the installation, its WADs, its sky and the world shader.
[[nodiscard]] auto readContent(const Options& options, Content& content) -> int {
    auto executable = nx::platform::executablePath();
    if (!executable) {
        return unusable(executable.error());
    }
    const std::filesystem::path data = options.data.value_or(executable->parent_path() / "data");
    const std::filesystem::path descriptionFile = data / "content" / "goldsrcCstrike.toml";
    auto text = nx::platform::readTextFile(descriptionFile);
    if (!text) {
        return unusable(text.error());
    }
    auto description = nx::assets::readGoldsrcInstallDesc(*text, nx::platform::toUtf8(descriptionFile));
    if (!description) {
        return unusable(description.error());
    }
    auto install = options.install
                       ? nx::assets::GoldsrcInstall::open(*description, *options.install)
                       : nx::assets::detectGoldsrcInstall(*description, nx::platform::steamFolders());
    if (!install) {
        return unusable(install.error());
    }
    auto vfs = nx::assets::Vfs::create(install->mounts());
    if (!vfs) {
        return unusable(vfs.error());
    }
    const auto mapPath = nx::assets::VfsPath::parse(std::format("maps/{}.bsp", options.map));
    if (!mapPath) {
        return unusable(mapPath.error());
    }
    auto mapBytes = vfs->readAll(*mapPath);
    if (!mapBytes) {
        return unusable(mapBytes.error());
    }
    content.mapBytes = std::move(*mapBytes);
    auto map = nx::assets::BspView::parse(content.mapBytes);
    if (!map) {
        return unusable(map.error());
    }
    content.map = std::move(*map);
    content.lightStyles = mapLightStyles(*content.map);
    readWads(*vfs, content);
    readSky(*vfs, content);
    auto shader = nx::platform::readFile(executable->parent_path() / "shaders" / "world.spv");
    if (!shader) {
        return unusable(shader.error());
    }
    content.shader = std::move(*shader);
    return EXIT_SUCCESS;
}

/// Writes `pixels`, RGBA rows from the top, as a 24-bit BMP.
[[nodiscard]] auto writeBmp(const std::filesystem::path& path, nx::rhi::Extent2d extent,
                            std::span<const std::byte> pixels) -> bool {
    constexpr std::uint32_t kHeaderBytes = 14 + 40;
    const std::uint32_t rowBytes = ((extent.width * 3) + 3) & ~3U; // rows are padded to 4 bytes
    const std::uint32_t imageBytes = rowBytes * extent.height;
    std::vector<std::byte> file;
    const auto put = [&file]<class Value>(Value value) {
        const auto raw = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // little-endian hosts
        file.insert(file.end(), raw.begin(), raw.end());
    };
    file.insert(file.end(), {std::byte{'B'}, std::byte{'M'}});
    put(kHeaderBytes + imageBytes);
    put(std::uint32_t{0});
    put(kHeaderBytes);
    put(std::uint32_t{40});
    put(static_cast<std::int32_t>(extent.width));
    put(static_cast<std::int32_t>(extent.height)); // positive: rows from the bottom
    put(std::uint16_t{1});
    put(std::uint16_t{24});
    put(std::uint32_t{0});
    put(imageBytes);
    put(std::int32_t{2835}); // 72 dpi
    put(std::int32_t{2835});
    put(std::uint32_t{0});
    put(std::uint32_t{0});
    for (std::uint32_t row = extent.height; row-- > 0;) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const std::size_t pixel = ((std::size_t{row} * extent.width) + x) * 4;
            file.insert(file.end(), {pixels[pixel + 2], pixels[pixel + 1], pixels[pixel]});
        }
        file.resize(file.size() + (rowBytes - (extent.width * 3)), std::byte{0});
    }
    std::ofstream stream{path, std::ios::binary};
    stream.write(reinterpret_cast<const char*>(file.data()), // NOLINT: the stream's byte type
                 static_cast<std::streamsize>(file.size()));
    return stream.good();
}

/// Renders one frame from the camera's start into options.screenshot, without a window.
[[nodiscard]] auto takeScreenshot(const Options& options, const Content& content) -> int {
    auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "nxview", .isValidationEnabled = options.isValidationEnabled});
    if (!device) {
        return unusable(device.error());
    }
    auto target = (*device)->createTexture(
        {.label = "nxview.screenshot",
         .format = nx::rhi::Format::Rgba8Unorm,
         .extent = kScreenshotExtent,
         .usage = {nx::rhi::TextureUsage::RenderTarget, nx::rhi::TextureUsage::CopySource}});
    auto depth = (*device)->createTexture({.label = "nxview.depth",
                                           .format = nx::rhi::Format::Depth32Float,
                                           .extent = kScreenshotExtent,
                                           .usage = {nx::rhi::TextureUsage::DepthTarget}});
    auto readback = (*device)->createBuffer(
        {.label = "nxview.readback",
         .size = std::uint64_t{kScreenshotExtent.width} * kScreenshotExtent.height * 4,
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
    auto world = nx::render::BspWorld::create(**device, list,
                                              {.map = *content.map,
                                               .wads = content.wads,
                                               .shaderCode = content.shader,
                                               .colorFormat = nx::rhi::Format::Rgba8Unorm,
                                               .sky = content.sky});
    if (!world) {
        return unusable(world.error());
    }
    const nx::render::Camera camera = startCamera(options, *content.map);
    const float aspect =
        static_cast<float>(kScreenshotExtent.width) / static_cast<float>(kScreenshotExtent.height);
    list.transition(*target, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    list.transition(*depth, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
    const std::array colors{nx::rhi::ColorAttachment{.texture = *target, .clear = kVoidColor}};
    list.beginRendering({.colors = colors,
                         .depth = nx::rhi::DepthAttachment{.texture = *depth, .clear = 0.0F},
                         .area = kScreenshotExtent});
    world->draw(list, camera, aspect, content.lightStyles.valuesAt(nx::Seconds{0.0F}));
    list.endRendering();
    list.transition(*target, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::CopySource);
    list.transition(*readback, nx::rhi::BufferState::Undefined, nx::rhi::BufferState::CopyDestination);
    list.copyTextureToBuffer({.buffer = *readback, .texture = *target, .extent = kScreenshotExtent});
    list.transition(*readback, nx::rhi::BufferState::CopyDestination, nx::rhi::BufferState::HostRead);
    if (auto submitted = (*device)->submitFrame(); !submitted) {
        return unusable(submitted.error());
    }
    (*device)->waitIdle();
    if (!writeBmp(*options.screenshot, kScreenshotExtent, (*device)->mappedBytes(*readback))) {
        return unusable(std::format("cannot write {}", nx::platform::toUtf8(*options.screenshot)));
    }
    const nx::render::FrameStats& drawn = world->lastFrame();
    std::println("nxview: {} from {} at {}x{}: {} of {} faces, {} triangles, in {} leaves",
                 nx::platform::toUtf8(*options.screenshot), options.map, kScreenshotExtent.width,
                 kScreenshotExtent.height, drawn.faces, world->stats().faces, drawn.triangles, drawn.leaves);
    return (*device)->validationErrors() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/// The keys the camera flies with.
struct HeldKeys {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool rise = false;
    bool sink = false;
    bool fast = false;

    void set(nx::platform::Key key, bool isDown) noexcept {
        using nx::platform::Key;
        switch (key) {
        case Key::W:
            forward = isDown;
            break;
        case Key::S:
            back = isDown;
            break;
        case Key::A:
            left = isDown;
            break;
        case Key::D:
            right = isDown;
            break;
        case Key::Space:
            rise = isDown;
            break;
        case Key::LeftControl:
            sink = isDown;
            break;
        case Key::LeftShift:
            fast = isDown;
            break;
        default:
            break;
        }
    }
};

/// Moves `camera` by what the held keys ask for over `seconds`.
void fly(nx::render::Camera& camera, const HeldKeys& keys, float seconds) noexcept {
    const nx::AngleVectors axes = nx::angleVectors(camera.angles);
    const auto axis = [](bool positive, bool negative) {
        return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
    };
    const nx::Vec3 direction = (axes.forward * axis(keys.forward, keys.back)) +
                               (axes.right * axis(keys.right, keys.left)) +
                               (nx::Vec3{.z = 1.0F} * axis(keys.rise, keys.sink));
    if (nx::lengthSquared(direction) == 0.0F) {
        return;
    }
    camera.position =
        camera.position + (nx::normalized(direction) * (kSpeed * (keys.fast ? kFastFactor : 1.0F) * seconds));
}

[[nodiscard]] auto extentOf(nx::platform::PixelSize size) noexcept -> nx::rhi::Extent2d {
    return {.width = size.width, .height = size.height};
}

/// Flies through the map in a window until it is closed.
[[nodiscard]] auto view(const Options& options, const Content& content) -> int {
    auto window = nx::platform::Window::create({.title = std::format("nxview: {}", options.map)});
    if (!window) {
        return unusable(window.error());
    }
    auto native = window->nativeWindow();
    if (!native) {
        return unusable(native.error());
    }
    auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "nxview", .window = *native, .isValidationEnabled = options.isValidationEnabled});
    if (!device) {
        return unusable(device.error());
    }
    auto swapchain = (*device)->createSwapchain({.size = extentOf(window->pixelSize())});
    if (!swapchain) {
        return unusable(swapchain.error());
    }
    auto sleepTimer = nx::platform::SleepTimer::create();
    if (!sleepTimer) {
        return unusable(sleepTimer.error());
    }
    if (auto captured = window->setRelativeMouse(true); !captured) {
        std::println(stderr, "nxview: {}", nx::describe(captured.error())); // the keys still fly
    }

    nx::render::Camera camera = startCamera(options, *content.map);
    std::optional<nx::render::BspWorld> world; // made with the first image, which gives the format
    std::optional<nx::rhi::TextureHandle> depth;
    nx::rhi::Extent2d depthExtent;
    HeldKeys keys;
    std::vector<nx::platform::WindowEvent> events;
    nx::platform::Stopwatch frameClock;
    nx::platform::Stopwatch mapClock;
    bool isRunning = true;
    while (isRunning) {
        events.clear();
        window->pollEvents(events);
        for (const nx::platform::WindowEvent& event : events) {
            if (std::holds_alternative<nx::platform::CloseRequested>(event)) {
                isRunning = false;
            } else if (const auto* key = std::get_if<nx::platform::KeyChanged>(&event)) {
                if (key->key == nx::platform::Key::Escape && key->isDown) {
                    isRunning = false;
                }
                keys.set(key->key, key->isDown);
            } else if (const auto* moved = std::get_if<nx::platform::MouseMoved>(&event)) {
                camera.angles.yaw =
                    nx::normalizeAngle(camera.angles.yaw - nx::Degrees{moved->dx * kDegreesPerCount});
                camera.angles.pitch = nx::Degrees{std::clamp(
                    camera.angles.pitch.value() + (moved->dy * kDegreesPerCount), -kMaxPitch, kMaxPitch)};
            } else if (const auto* resized = std::get_if<nx::platform::Resized>(&event)) {
                if (auto recreated = (*swapchain)->resize(extentOf(resized->size)); !recreated) {
                    return unusable(recreated.error());
                }
            }
        }
        const std::chrono::duration<float> elapsed = frameClock.restart();
        fly(camera, keys, elapsed.count());

        auto commands = (*device)->beginFrame();
        if (!commands) {
            return unusable(commands.error());
        }
        nx::rhi::CommandList& list = commands->get();
        auto image = (*swapchain)->acquire();
        if (!image) {
            return unusable(image.error());
        }
        if (!*image) {
            if (auto submitted = (*device)->submitFrame(); !submitted) {
                return unusable(submitted.error());
            }
            sleepTimer->sleepUntil(nx::platform::MonotonicClock::now() + kMinimizedWait); // nothing to show
            continue;
        }
        const nx::rhi::SwapchainImage& frame = **image;
        if (!world) {
            auto created = nx::render::BspWorld::create(**device, list,
                                                        {.map = *content.map,
                                                         .wads = content.wads,
                                                         .shaderCode = content.shader,
                                                         .colorFormat = frame.format,
                                                         .sky = content.sky});
            if (!created) {
                return unusable(created.error());
            }
            world.emplace(std::move(*created));
            std::ignore = mapClock.restart(); // the light styles start with the map
        }
        if (!depth || depthExtent != frame.size) {
            if (depth) {
                (*device)->destroy(*depth);
            }
            auto created = (*device)->createTexture({.label = "nxview.depth",
                                                     .format = nx::rhi::Format::Depth32Float,
                                                     .extent = frame.size,
                                                     .usage = {nx::rhi::TextureUsage::DepthTarget}});
            if (!created) {
                return unusable(created.error());
            }
            depth = *created;
            depthExtent = frame.size;
        }
        list.transition(frame.texture, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
        list.transition(*depth, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
        const std::array colors{nx::rhi::ColorAttachment{.texture = frame.texture, .clear = kVoidColor}};
        list.beginRendering({.colors = colors,
                             .depth = nx::rhi::DepthAttachment{.texture = *depth, .clear = 0.0F},
                             .area = frame.size});
        const float aspect = static_cast<float>(frame.size.width) / static_cast<float>(frame.size.height);
        world->draw(list, camera, aspect, content.lightStyles.valuesAt(nx::Seconds{mapClock.elapsed()}));
        list.endRendering();
        list.transition(frame.texture, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::Present);
        if (auto submitted = (*device)->submitFrame(**swapchain); !submitted) {
            return unusable(submitted.error());
        }
    }

    (*device)->waitIdle();
    const std::uint64_t validationErrors = (*device)->validationErrors();
    world.reset();
    swapchain->reset(); // before its device
    if (validationErrors != 0) {
        std::println(stderr, "nxview: the validation layer reported {} errors", validationErrors);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
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
    if (!options) {
        std::print(stderr, "{}", kUsage);
        return kExitUnusable;
    }
    Content content;
    if (const int read = readContent(*options, content); read != EXIT_SUCCESS) {
        return read;
    }
    return options->screenshot ? takeScreenshot(*options, content) : view(*options, content);
}
