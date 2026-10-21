// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// nxview: the map viewer (TODO.md, M2). It opens a map of the Counter-Strike 1.6 installation and
// walks a player through its world, moved by the game's own movement, a knife in hand, or flies a
// free camera through it; or it renders one frame of it to a BMP file.

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
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;
import tool.nxview;

namespace {

constexpr int kExitUnusable = 2; // the arguments, the content, the window or the GPU
constexpr std::string_view kUsage = R"(usage: nxview [--map <name>] [--install <folder>] [--data <folder>]
              [--position "<x> <y> <z>"] [--angles "<pitch> <yaw> <roll>"]
              [--screenshot <file.bmp> [--time <seconds>]] [--validation]

Opens a map of the Counter-Strike 1.6 installation and walks through it as a player with a knife:
the mouse looks, W A S D move, Space jumps, Ctrl ducks, Shift walks slowly. F flies instead, and
back: W A S D move, Space and Ctrl rise and sink, Shift is faster. Escape ends it.

  --map <name>            the map of maps/<name>.bsp (default: de_dust2)
  --install <folder>      the installation: the folder with cstrike/ and valve/ (default: the one
                          Steam installed)
  --data <folder>         the data/ folder of the project (default: the one next to nxview)
  --position "<x> <y> <z>" where the camera starts, the eye of the player (default: a standing
                          player's eye at the first spawn)
  --angles "<p> <y> <r>"  where it looks, in degrees: pitch (down is positive), yaw, roll (default:
                          the spawn's yaw)
  --screenshot <file.bmp> renders one 1280x720 frame from the camera's start into the file, without
                          a window, and ends
  --time <seconds>        when the frame of --screenshot is, after the map starts (default: 0):
                          what animates (light styles, textures, water, the knife) is drawn as it
                          is then
  --validation            the Khronos validation layer, when it is installed: its messages are
                          logged and nxview exits with 1 when it reported an error
)";

constexpr std::string_view kDefaultMap = "de_dust2";
constexpr nx::rhi::Extent2d kScreenshotExtent{.width = 1280, .height = 720};
/// What no face covers: the void outside a map, seen through its leaks.
constexpr nx::rhi::ClearColor kVoidColor{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
/// The flying camera's speed in map units per second, and how much faster it goes with Shift.
constexpr float kSpeed = 320.0F;
constexpr float kFastFactor = 3.0F;
/// The speed a movement key asks for, CS 1.6's cl_forwardspeed, cl_backspeed and cl_sidespeed; and
/// the share of it Shift walks at, its cl_movespeedkey.
constexpr float kKeySpeed = 400.0F;
constexpr float kWalkFactor = 0.52F;
/// The speed limit of a player holding a knife.
constexpr nx::UnitsPerSecond kKnifeSpeed{250.0F};
/// A user command each this long, the classic rate of 100 a second (docs/07, section 2.2); a frame
/// catches up on this much time at most, so that a stalled one does not run away.
constexpr nx::Milliseconds kCommandInterval{10.0F};
constexpr nx::Seconds kMaxCatchUp{0.25F};
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
    nx::Seconds time{};             ///< Of the screenshot, after the map starts.
    bool isValidationEnabled = false;
};

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
        } else if (option == "--time") {
            float seconds = 0.0F;
            if (std::from_chars(value.data(), value.data() + value.size(), seconds).ec != std::errc{} ||
                seconds < 0.0F) {
                return std::nullopt;
            }
            options.time = nx::Seconds{seconds};
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

/// The map, its WADs, its sky, its hulls, the classes of its entities, its sprites, the knife and the
/// shaders, read once: the views point into these bytes.
struct Content {
    std::vector<std::byte> mapBytes;
    std::optional<nx::assets::BspView> map;
    nx::game::EntityClasses entityClasses;
    std::vector<std::vector<std::byte>> wadBytes;
    std::vector<nx::assets::WadView> wads;
    std::vector<nx::assets::RgbaImage> sky; ///< Its sides, in the order of nx::render::kSkySuffixes.
    nx::render::LightStyles lightStyles;
    nx::render::SkyLight skyLight;
    std::optional<nx::collision::BrushHulls> hulls;
    std::optional<nxview::GlowBlockers> glowBlockers;
    std::optional<nxview::MapSprites> sprites;
    std::optional<nxview::ViewModel> knife;
    std::vector<std::byte> worldShader;
    std::vector<std::byte> studioShader;
    std::vector<std::byte> spriteShader;
};

/// Where a player first stands: the first counter-terrorist spawn, else the first terrorist one.
[[nodiscard]] auto spawnCamera(const nx::assets::BspView& map) -> nx::render::Camera {
    for (const std::string_view classname : {"info_player_start", "info_player_deathmatch"}) {
        for (const nx::assets::BspEntity& entity : map.entities()) {
            if (entity.find("classname") != classname) {
                continue;
            }
            const std::optional<nx::Vec3> origin = entity.findVector("origin");
            const std::optional<nx::Vec3> angles = entity.findVector("angles");
            if (!origin) {
                continue;
            }
            return nx::render::Camera{.position = *origin + nx::Vec3{.z = cs::kStandingViewHeight.value()},
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

/// Reads the map `options.map` of the installation, its WADs, its sky, its sprites, the knife and the
/// shaders, and builds its hulls.
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
    content.lightStyles = nxview::mapLightStyles(*content.map);
    content.skyLight = nxview::skyLightOf(*content.map);
    auto hulls = nx::collision::BrushHulls::create(*content.map);
    if (!hulls) {
        return unusable(hulls.error());
    }
    content.hulls = std::move(*hulls);
    content.glowBlockers.emplace(*content.map);
    const std::filesystem::path classesFile = data / "cstrike" / "entities.toml";
    auto classesText = nx::platform::readTextFile(classesFile);
    if (!classesText) {
        return unusable(classesText.error());
    }
    auto classes = nx::game::readEntityClasses(*classesText, nx::platform::toUtf8(classesFile));
    if (!classes) {
        return unusable(classes.error());
    }
    content.entityClasses = std::move(*classes);
    readWads(*vfs, content);
    readSky(*vfs, content);
    content.sprites = nxview::MapSprites::read(*vfs, *content.map);
    if (!content.sprites->missing().empty()) {
        std::println("nxview: sprites the map's entities show that the installation does not have: {}",
                     content.sprites->missing() | std::views::join_with(std::string_view{", "}) |
                         std::ranges::to<std::string>());
    }
    auto knife = nxview::ViewModel::read(*vfs);
    if (!knife) {
        return unusable(knife.error());
    }
    content.knife = std::move(*knife);
    for (const auto& [file, shader] :
         {std::pair{"world.spv", &content.worldShader}, std::pair{"studio.spv", &content.studioShader},
          std::pair{"sprite.spv", &content.spriteShader}}) {
        auto code = nx::platform::readFile(executable->parent_path() / "shaders" / file);
        if (!code) {
            return unusable(code.error());
        }
        *shader = std::move(*code);
    }
    return EXIT_SUCCESS;
}

/// What nxview draws: the world, the light it gives, the sprites of its entities and the knife on the
/// GPU.
struct Scene {
    nx::render::BspWorld world;
    nx::render::WorldLight light;
    nx::render::SpriteRenderer sprites;
    std::vector<nx::render::SpriteModel> spriteModels; ///< Of content.sprites, by file.
    nx::render::StudioRenderer studio;
    nx::render::StudioModel knife;
};

/// Why a frame of the scene could not be drawn.
enum class SceneError : std::uint8_t {
    DrawFailed, ///< The GPU refused the room it needed (cause: the renderer's error).
};

/// The scene of `content` for targets of `format`, its uploads recorded in `commands`; none when the
/// GPU refused it, which is printed.
[[nodiscard]] auto createScene(nx::rhi::Device& device, nx::rhi::CommandList& commands,
                               const Content& content, nx::rhi::Format format) -> std::optional<Scene> {
    auto world = nx::render::BspWorld::create(device, commands,
                                              {.map = *content.map,
                                               .wads = content.wads,
                                               .shaderCode = content.worldShader,
                                               .colorFormat = format,
                                               .sky = content.sky});
    if (!world) {
        std::ignore = unusable(world.error());
        return std::nullopt;
    }
    auto sprites = nx::render::SpriteRenderer::create(
        device, {.shaderCode = content.spriteShader, .colorFormat = format});
    auto spriteModels = sprites ? content.sprites->load(*sprites, commands)
                                : nx::Result<std::vector<nx::render::SpriteModel>, nx::render::SpriteError>{};
    if (!sprites || !spriteModels) {
        std::ignore = unusable(!sprites ? sprites.error() : spriteModels.error());
        return std::nullopt;
    }
    auto studio = nx::render::StudioRenderer::create(
        device, commands, {.shaderCode = content.studioShader, .colorFormat = format});
    if (!studio) {
        std::ignore = unusable(studio.error());
        return std::nullopt;
    }
    auto knife = content.knife->load(*studio, commands);
    if (!knife) {
        std::ignore = unusable(knife.error());
        return std::nullopt;
    }
    return Scene{.world = std::move(*world),
                 .light = nx::render::WorldLight{*content.map},
                 .sprites = std::move(*sprites),
                 .spriteModels = std::move(*spriteModels),
                 .studio = std::move(*studio),
                 .knife = std::move(*knife)};
}

/// Where the map's entities put its brush models, unturned: where they rest until the game moves them.
[[nodiscard]] auto restingPlacementsOf(const nx::assets::BspView& map)
    -> std::vector<nx::render::ModelPlacement> {
    std::vector<nx::render::ModelPlacement> placements;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (const std::optional<nx::assets::ModelIndex> model = entity.findBrushModel();
            model && *model != nx::assets::kWorldModel && model->value < map.models().size()) {
            placements.push_back(
                {.model = *model, .origin = entity.findVector("origin").value_or(nx::Vec3{})});
        }
    }
    return placements;
}

/// What a frame of the scene shows.
struct SceneView {
    nx::render::Camera camera{};
    float aspect = 1.0F;
    nx::Seconds time{}; ///< Since the map started.
    std::span<const nx::render::ModelPlacement> placements{};
    std::optional<nx::Seconds> sinceDeploy{}; ///< Since the knife came out; none without a player.
};

/// Draws `view` of `scene`: the world; the sprites of its entities, a glow the player's line of sight
/// does not reach hidden; then the knife in the player's hand, lit where the player's eye is, as the
/// original lights the view's own model. Errors: DrawFailed.
[[nodiscard]] auto drawScene(nx::rhi::CommandList& list, Scene& scene, const Content& content,
                             const SceneView& view) -> nx::Result<void, SceneError> {
    const nx::render::LightStyleValues styles = content.lightStyles.valuesAt(view.time);
    scene.world.draw(list, {.camera = view.camera,
                            .aspect = view.aspect,
                            .time = view.time,
                            .lightStyles = styles,
                            .placements = view.placements});
    std::vector<nx::render::SpriteInstance> sprites =
        content.sprites->instances(scene.spriteModels, view.time);
    for (nx::render::SpriteInstance& sprite : sprites) {
        if (sprite.mode == nx::render::RenderMode::Glow) {
            sprite.isHidden = content.glowBlockers->hides(*content.hulls, view.placements,
                                                          view.camera.position, sprite.origin);
        }
    }
    if (auto drawn = scene.sprites.draw(list,
                                        {.camera = view.camera,
                                         .aspect = view.aspect,
                                         .time = view.time,
                                         .lightStyles = styles,
                                         .instances = sprites},
                                        scene.light);
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the sprites", std::move(drawn.error()));
    }
    if (!view.sinceDeploy) {
        return {};
    }
    const std::vector<nx::Mat3x4> bones = content.knife->pose(view.camera, *view.sinceDeploy);
    const std::array knife{nx::render::StudioInstance{
        .model = scene.knife,
        .bones = bones,
        .light = scene.light.modelLightAt({.origin = view.camera.position, .sky = content.skyLight}, styles),
        .depth = nx::render::StudioDepth::ViewModel}};
    if (auto drawn =
            scene.studio.draw(list, {.camera = view.camera, .aspect = view.aspect, .instances = knife});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the knife", std::move(drawn.error()));
    }
    return {};
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
         .size = std::uint64_t{kScreenshotExtent.width} * kScreenshotExtent.height * kRgbaBytes,
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
    std::optional<Scene> scene = createScene(**device, list, content, nx::rhi::Format::Rgba8Unorm);
    if (!scene) {
        return kExitUnusable;
    }
    const nx::render::Camera camera = startCamera(options, *content.map);
    const float aspect =
        static_cast<float>(kScreenshotExtent.width) / static_cast<float>(kScreenshotExtent.height);
    list.transition(target->get(), nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    list.transition(depth->get(), nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
    const std::array colors{nx::rhi::ColorAttachment{.texture = target->get(), .clear = kVoidColor}};
    list.beginRendering({.colors = colors,
                         .depth = nx::rhi::DepthAttachment{.texture = depth->get(), .clear = 0.0F},
                         .area = kScreenshotExtent});
    // The brush entities where the map puts them, and the knife the player has held since it started.
    const std::vector<nx::render::ModelPlacement> placements = restingPlacementsOf(*content.map);
    if (auto drawn = drawScene(list, *scene, content,
                               {.camera = camera,
                                .aspect = aspect,
                                .time = options.time,
                                .placements = placements,
                                .sinceDeploy = options.time});
        !drawn) {
        return unusable(drawn.error());
    }
    list.endRendering();
    list.transition(target->get(), nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::CopySource);
    list.transition(readback->get(), nx::rhi::BufferState::Undefined, nx::rhi::BufferState::CopyDestination);
    list.copyTextureToBuffer(
        {.buffer = readback->get(), .texture = target->get(), .extent = kScreenshotExtent});
    list.transition(readback->get(), nx::rhi::BufferState::CopyDestination, nx::rhi::BufferState::HostRead);
    if (auto submitted = (*device)->submitFrame(); !submitted) {
        return unusable(submitted.error());
    }
    (*device)->waitIdle();
    if (auto written = nx::platform::writeFile(
            *options.screenshot, bmpOf(kScreenshotExtent, (*device)->mappedBytes(readback->get())));
        !written) {
        return unusable(written.error());
    }
    const nx::render::FrameStats& drawn = scene->world.lastFrame();
    std::println("nxview: {} from {} at {}x{}: {} of {} faces, {} triangles, in {} leaves",
                 nx::platform::toUtf8(*options.screenshot), options.map, kScreenshotExtent.width,
                 kScreenshotExtent.height, drawn.faces, scene->world.stats().faces, drawn.triangles,
                 drawn.leaves);
    return (*device)->validationErrors() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/// The keys a player walks with and the camera flies with.
struct HeldKeys {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool space = false;   ///< Jumps; rises in flight.
    bool control = false; ///< Ducks; sinks in flight.
    bool shift = false;   ///< Walks slowly; flies faster.

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
            space = isDown;
            break;
        case Key::LeftControl:
            control = isDown;
            break;
        case Key::LeftShift:
            shift = isDown;
            break;
        default:
            break;
        }
    }
};

/// 1 when only the positive key of an axis is held, -1 when only the negative one is, else 0.
[[nodiscard]] constexpr auto axisOf(bool positive, bool negative) noexcept -> float {
    return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
}

/// Moves `camera` by what the held keys ask for over `seconds`.
void fly(nx::render::Camera& camera, const HeldKeys& keys, float seconds) noexcept {
    const nx::AngleVectors axes = nx::angleVectors(camera.angles);
    const nx::Vec3 direction = (axes.forward * axisOf(keys.forward, keys.back)) +
                               (axes.right * axisOf(keys.right, keys.left)) +
                               (nx::Vec3{.z = 1.0F} * axisOf(keys.space, keys.control));
    if (nx::lengthSquared(direction) == 0.0F) {
        return;
    }
    camera.position = camera.position +
                      (nx::normalized(direction) * (kSpeed * (keys.shift ? kFastFactor : 1.0F) * seconds));
}

/// A player walking through the map, moved one user command at a time by the game's movement.
struct Walker {
    cs::MovementState player{};
    nx::Vec3 previousEye{}; ///< Before the last command.
};

[[nodiscard]] auto eyeOf(const cs::MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

/// A standing player whose eye is at `eye`.
[[nodiscard]] auto walkerAt(nx::Vec3 eye) -> Walker {
    return Walker{
        .player = {.origin = eye - nx::Vec3{.z = cs::kStandingViewHeight.value()}, .maxSpeed = kKnifeSpeed},
        .previousEye = eye};
}

/// The game of the map as nxview plays it, a tick each kCommandInterval: its entities, the player's
/// body among them, and its clock.
struct LocalGame {
    nx::ecs::Entity body{};
    nx::Seconds pending{}; ///< Of the time given to the game, what no tick has taken yet.
    nx::Seconds time{};    ///< Since the map started.
};

/// The command of what `keys` and `view` ask for.
[[nodiscard]] auto commandOf(const HeldKeys& keys, nx::EulerAngles view) -> cs::UserCommand {
    const float speed = kKeySpeed * (keys.shift ? kWalkFactor : 1.0F);
    nx::Flags<cs::Button> buttons;
    for (const auto& [isHeld, button] :
         {std::pair{keys.space, cs::Button::Jump}, std::pair{keys.control, cs::Button::Duck},
          std::pair{keys.forward, cs::Button::Forward}, std::pair{keys.back, cs::Button::Back},
          std::pair{keys.left, cs::Button::MoveLeft}, std::pair{keys.right, cs::Button::MoveRight}}) {
        if (isHeld) {
            buttons.set(button);
        }
    }
    return cs::UserCommand{.duration = kCommandInterval,
                           .viewAngles = view,
                           .move = {.forward = nx::UnitsPerSecond{speed * axisOf(keys.forward, keys.back)},
                                    .side = nx::UnitsPerSecond{speed * axisOf(keys.right, keys.left)}},
                           .buttons = buttons};
}

/// Gives `elapsed` to the game, tick by tick: the command of `keys` and `view` moves `walker`, when
/// one walks, among the entities of `game` and the world of `hulls`, and what it meets is touched;
/// then the pushers move, pushing the player's body. Returns the player's eye, between where the
/// last two ticks left it by the time left; none without a walker.
auto play(nx::ecs::World& game, LocalGame& local, const nx::collision::BrushHulls& hulls, Walker* walker,
          const HeldKeys& keys, nx::EulerAngles view, nx::Seconds elapsed) -> std::optional<nx::Vec3> {
    local.pending = std::min(local.pending + elapsed, kMaxCatchUp);
    while (local.pending >= kCommandInterval) {
        if (walker != nullptr) {
            walker->previousEye = eyeOf(walker->player);
            const std::vector<cs::BrushEntity> entities = cs::brushEntitiesOf(game);
            const cs::MovementResult moved = cs::simulateMovement(walker->player, commandOf(keys, view),
                                                                  cs::MovementSettings{}, hulls, entities);
            walker->player = moved.state;
            nx::game::touchBrushEntities(game, hulls, moved.touchedModels, local.body);
            game.get<nx::game::Transform>(local.body)->origin = walker->player.origin;
            *game.get<nx::game::Body>(local.body) = cs::bodyOf(walker->player);
        }
        nx::game::runPushers(game, hulls, kCommandInterval, local.time);
        if (walker != nullptr) {
            walker->player.origin = game.get<nx::game::Transform>(local.body)->origin;
        }
        local.time += kCommandInterval;
        local.pending -= kCommandInterval;
    }
    if (walker == nullptr) {
        return std::nullopt;
    }
    return nx::lerp(walker->previousEye, eyeOf(walker->player), local.pending / kCommandInterval);
}

[[nodiscard]] auto extentOf(nx::platform::PixelSize size) noexcept -> nx::rhi::Extent2d {
    return {.width = size.width, .height = size.height};
}

/// Walks or flies through the map in a window until it is closed.
[[nodiscard]] auto view(const Options& options, const Content& content) -> int {
    const nx::collision::BrushHulls& hulls = *content.hulls;
    nx::ecs::World game;
    if (auto registered = nx::game::registerComponents(game); !registered) {
        return unusable(registered.error());
    }
    if (auto spawned = nx::game::spawnBrushEntities(game, content.map->entities(), content.map->models(),
                                                    content.entityClasses);
        !spawned) {
        return unusable(spawned.error());
    }
    std::vector<nx::render::ModelPlacement> placements; // where the game puts its brush entities
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
    auto size = window->pixelSize();
    if (!size) {
        return unusable(size.error());
    }
    auto swapchain = (*device)->createSwapchain({.size = extentOf(*size)});
    if (!swapchain) {
        return unusable(swapchain.error());
    }
    auto sleepTimer = nx::platform::SleepTimer::create();
    if (!sleepTimer) {
        return unusable(sleepTimer.error());
    }
    if (auto captured = window->setMouseMode(nx::platform::MouseMode::Relative); !captured) {
        std::println(stderr, "nxview: {}", nx::describe(captured.error())); // the keys still fly
    }

    nx::render::Camera camera = startCamera(options, *content.map);
    Walker walker = walkerAt(camera.position);
    LocalGame local{
        .body = game.spawn(nx::game::Transform{.origin = walker.player.origin}, cs::bodyOf(walker.player))};
    bool isFlying = false;
    std::optional<Scene> scene; // made with the first image, which gives the format
    std::optional<nx::rhi::OwnedTexture> depth;
    nx::rhi::Extent2d depthExtent;
    HeldKeys keys;
    std::vector<nx::platform::WindowEvent> events;
    nx::platform::Stopwatch frameClock;
    nx::platform::Stopwatch mapClock;
    nx::platform::Stopwatch deployClock; ///< Since the player drew the knife.
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
                if (key->key == nx::platform::Key::F && key->isDown && !key->isRepeat) {
                    isFlying = !isFlying;
                    if (!isFlying) {
                        walker = walkerAt(camera.position);  // the player lands where the camera flew
                        std::ignore = deployClock.restart(); // and draws the knife again
                    }
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
        const nx::Seconds elapsed = frameClock.restart();
        if (isFlying) {
            fly(camera, keys, elapsed.count());
            // No walker, no eye: the game runs on while the camera flies.
            std::ignore = play(game, local, hulls, nullptr, keys, camera.angles, elapsed);
        } else {
            camera.position =
                play(game, local, hulls, &walker, keys, camera.angles, elapsed).value_or(camera.position);
        }

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
        if (!scene) {
            std::optional<Scene> created = createScene(**device, list, content, frame.format);
            if (!created) {
                return kExitUnusable;
            }
            scene.emplace(std::move(*created));
            std::ignore = mapClock.restart();    // the light styles start with the map
            std::ignore = deployClock.restart(); // and the player draws the knife
        }
        if (!depth || depthExtent != frame.size) {
            auto created = (*device)->createTexture({.label = "nxview.depth",
                                                     .format = nx::rhi::Format::Depth32Float,
                                                     .extent = frame.size,
                                                     .usage = {nx::rhi::TextureUsage::DepthTarget}});
            if (!created) {
                return unusable(created.error());
            }
            depth = std::move(*created); // the old one, if any, goes
            depthExtent = frame.size;
        }
        list.transition(frame.texture, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
        list.transition(depth->get(), nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
        const std::array colors{nx::rhi::ColorAttachment{.texture = frame.texture, .clear = kVoidColor}};
        list.beginRendering({.colors = colors,
                             .depth = nx::rhi::DepthAttachment{.texture = depth->get(), .clear = 0.0F},
                             .area = frame.size});
        const float aspect = static_cast<float>(frame.size.width) / static_cast<float>(frame.size.height);
        const nx::Seconds time{mapClock.elapsed()};
        placements.clear();
        nx::ecs::Query<const nx::game::BrushModel&, const nx::game::Transform&>{game}.forEach(
            [&placements](const nx::game::BrushModel& model, const nx::game::Transform& transform) {
                placements.push_back(
                    {.model = model.index, .origin = transform.origin, .angles = transform.angles});
            });
        const std::optional<nx::Seconds> sinceDeploy =
            isFlying ? std::nullopt : std::optional{nx::Seconds{deployClock.elapsed()}};
        if (auto drawn = drawScene(list, *scene, content,
                                   {.camera = camera,
                                    .aspect = aspect,
                                    .time = time,
                                    .placements = placements,
                                    .sinceDeploy = sinceDeploy});
            !drawn) {
            return unusable(drawn.error());
        }
        list.endRendering();
        list.transition(frame.texture, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::Present);
        if (auto submitted = (*device)->submitFrame(**swapchain); !submitted) {
            return unusable(submitted.error());
        }
    }

    (*device)->waitIdle();
    const std::uint64_t validationErrors = (*device)->validationErrors();
    scene.reset();
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
