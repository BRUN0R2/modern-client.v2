// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.render;
import nx.collision;
import nx.ecs;
import nx.game;
import cs.shared;

namespace cs {

namespace {

constexpr nx::log::Category kLog{"client"};

/// The WADs the map's `wad` key names ("\half-life\cstrike\cs_dust.wad;..."), by their file names at
/// the root of the VFS, as the original looks them up: their bytes, and the names of those the
/// installation lacks. Maps often name WADs of their author's computer that no texture of theirs
/// needs; a texture none of the others has is reported by the world.
struct WadFiles {
    std::vector<std::vector<std::byte>> bytes;
    std::vector<std::string_view> absent;
};

[[nodiscard]] auto readWads(const nx::assets::Vfs& vfs, const nx::assets::BspView& map) -> WadFiles {
    const std::string_view list =
        map.entities().empty() ? std::string_view{} : map.entities()[0].find("wad").value_or("");
    WadFiles files;
    for (const auto part : std::views::split(list, ';')) {
        const std::string_view entry{part};
        const std::string_view name = entry.substr(entry.find_last_of("\\/") + 1); // npos + 1 is 0
        if (name.empty()) {
            continue;
        }
        const auto path = nx::assets::VfsPath::parse(name);
        auto bytes = path ? vfs.readAll(*path) : nx::Result<std::vector<std::byte>, nx::assets::VfsError>{};
        if (!path || !bytes) {
            files.absent.push_back(name);
            continue;
        }
        files.bytes.push_back(std::move(*bytes));
    }
    return files;
}

/// The sides of the sky `name`, gfx/env/<name><suffix>.tga; none when one is missing or unreadable.
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
[[nodiscard]] auto readSky(const nx::assets::Vfs& vfs, const nx::assets::BspView& map)
    -> std::vector<nx::assets::RgbaImage> {
    const std::string_view name =
        map.entities().empty() ? nx::render::kDefaultSkyName
                               : map.entities()[0].find("skyname").value_or(nx::render::kDefaultSkyName);
    const std::array candidates{name, nx::render::kDefaultSkyName};
    for (const std::string_view candidate : std::span{candidates}.first(name == candidates[1] ? 1 : 2)) {
        if (auto sides = readSkySides(vfs, candidate)) {
            return std::move(*sides);
        }
        nx::log::warning(kLog, "the sky {:?} is missing or unreadable", candidate);
    }
    return {};
}

/// `names`, separated by commas.
template <class Names>
[[nodiscard]] auto listed(const Names& names) -> std::string {
    return names | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
}

} // namespace

auto openGameContent(const std::filesystem::path& data, const std::optional<std::filesystem::path>& install)
    -> nx::Result<GameContent, LevelError> {
    const std::filesystem::path descriptionFile = data / "content" / "goldsrcCstrike.toml";
    auto text = nx::platform::readTextFile(descriptionFile);
    if (!text) {
        return nx::fail(LevelError::ContentUnavailable, std::move(text.error()));
    }
    auto description = nx::assets::readGoldsrcInstallDesc(*text, nx::platform::toUtf8(descriptionFile));
    if (!description) {
        return nx::fail(LevelError::ContentUnavailable, std::move(description.error()));
    }
    auto installation = install
                            ? nx::assets::GoldsrcInstall::open(*description, *install)
                            : nx::assets::detectGoldsrcInstall(*description, nx::platform::steamFolders());
    if (!installation) {
        return nx::fail(LevelError::ContentUnavailable, std::move(installation.error()));
    }
    auto vfs = nx::assets::Vfs::create(installation->mounts());
    if (!vfs) {
        return nx::fail(LevelError::ContentUnavailable, std::move(vfs.error()));
    }
    const std::filesystem::path classesFile = data / "cstrike" / "entities.toml";
    auto classesText = nx::platform::readTextFile(classesFile);
    if (!classesText) {
        return nx::fail(LevelError::ContentUnavailable, std::move(classesText.error()));
    }
    auto classes = nx::game::readEntityClasses(*classesText, nx::platform::toUtf8(classesFile));
    if (!classes) {
        return nx::fail(LevelError::ContentUnavailable, std::move(classes.error()));
    }
    return GameContent{.vfs = std::move(*vfs), .entityClasses = std::move(*classes)};
}

auto Level::load(const GameContent& content, std::string_view name)
    -> nx::Result<std::unique_ptr<Level>, LevelError> {
    const auto mapPath = nx::assets::VfsPath::parse(std::format("maps/{}.bsp", name));
    if (!mapPath) {
        return nx::fail(LevelError::MapUnreadable, std::string{name}, mapPath.error());
    }
    auto made = std::make_unique<Level>(CreateToken{});
    Level& level = *made;
    auto mapBytes = content.vfs.readAll(*mapPath);
    if (!mapBytes) {
        return nx::fail(LevelError::MapUnreadable, std::string{name}, std::move(mapBytes.error()));
    }
    level.m_mapBytes = std::move(*mapBytes);
    auto map = nx::assets::BspView::parse(level.m_mapBytes);
    if (!map) {
        return nx::fail(LevelError::MapUnreadable, std::string{name}, std::move(map.error()));
    }
    level.m_map = std::move(*map);
    const nx::assets::BspView& bsp = *level.m_map;
    level.m_entityClasses = content.entityClasses;
    level.m_lightStyles = mapLightStyles(bsp);
    level.m_skyLight = skyLightOf(bsp);
    auto hulls = nx::collision::BrushHulls::create(bsp);
    if (!hulls) {
        return nx::fail(LevelError::HullsInvalid, std::string{name}, std::move(hulls.error()));
    }
    level.m_hulls = std::move(*hulls);
    level.m_glowBlockers.emplace(bsp);

    WadFiles wads = readWads(content.vfs, bsp);
    if (!wads.absent.empty()) {
        nx::log::warning(kLog, "WADs the map names that the installation does not have: {}",
                         listed(wads.absent));
    }
    level.m_wadBytes = std::move(wads.bytes);
    for (const std::vector<std::byte>& bytes : level.m_wadBytes) {
        if (auto wad = nx::assets::WadView::parse(bytes)) {
            level.m_wads.push_back(std::move(*wad));
        }
    }
    level.m_sky = readSky(content.vfs, bsp);
    level.m_sprites = MapSprites::read(content.vfs, bsp);
    if (!level.m_sprites->missing().empty()) {
        nx::log::warning(kLog, "sprites the map's entities show that the installation does not have: {}",
                         listed(level.m_sprites->missing()));
    }
    level.m_models = MapModels::read(content.vfs, bsp);
    if (!level.m_models->missing().empty()) {
        nx::log::warning(kLog, "models the map's entities show that cannot be read: {}",
                         listed(level.m_models->missing()));
    }
    // The game drops its hostages to the floor as it makes them, among the brush entities at rest.
    nx::ecs::World resting;
    if (auto spawned = spawnGame(bsp, level.m_entityClasses, resting); !spawned) {
        return std::unexpected{std::move(spawned.error())};
    }
    level.m_models->dropToFloor(resting, *level.m_hulls);
    auto knife = ViewModel::read(content.vfs);
    if (!knife) {
        return nx::fail(LevelError::KnifeUnreadable, std::move(knife.error()));
    }
    level.m_knife = std::move(*knife);
    return made;
}

auto spawnGame(const nx::assets::BspView& map, const nx::game::EntityClasses& classes, nx::ecs::World& world)
    -> nx::Result<void, LevelError> {
    if (auto registered = nx::game::registerComponents(world); !registered) {
        return nx::fail(LevelError::GameUnspawnable, std::move(registered.error()));
    }
    if (auto spawned = nx::game::spawnBrushEntities(world, map.entities(), map.models(), classes); !spawned) {
        return nx::fail(LevelError::GameUnspawnable, std::move(spawned.error()));
    }
    return {};
}

auto spawnView(const nx::assets::BspView& map) -> nx::render::Camera {
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
            return nx::render::Camera{.position = *origin + nx::Vec3{.z = kStandingViewHeight.value()},
                                      .angles = {.yaw = nx::Degrees{angles ? angles->y : 0.0F}}};
        }
    }
    return nx::render::Camera{};
}

} // namespace cs
