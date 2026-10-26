// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.collision;
import nx.ecs;
import nx.game;

namespace cs {

namespace {

/// The class of the spawn points of each team.
constexpr nx::EnumArray<Team, std::string_view> kSpawnClasses{
    nx::EnumArray<Team, std::string_view>::Storage{"info_player_deathmatch", "info_player_start"}};

} // namespace

auto spawnPointsOf(const nx::assets::BspView& map, Team team) -> std::vector<SpawnPoint> {
    std::vector<SpawnPoint> points;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (entity.find("classname") != kSpawnClasses[team]) {
            continue;
        }
        const nx::Vec3 origin = entity.findVector("origin").value_or(nx::Vec3{});
        if (origin == nx::Vec3{}) {
            continue;
        }
        const std::optional<nx::Vec3> angles = entity.findVector("angles");
        points.push_back({.origin = origin, .yaw = nx::Degrees{angles ? angles->y : 0.0F}});
    }
    return points;
}

auto firstSpawnOf(const nx::assets::BspView& map) -> std::optional<TeamSpawn> {
    for (const Team team : {Team::CounterTerrorists, Team::Terrorists}) {
        if (const std::vector<SpawnPoint> points = spawnPointsOf(map, team); !points.empty()) {
            return TeamSpawn{.point = points.front(), .team = team};
        }
    }
    return std::nullopt;
}

auto spawnTeam(const nx::assets::BspView& map) -> Team {
    const std::optional<TeamSpawn> spawn = firstSpawnOf(map);
    return spawn ? spawn->team : Team::CounterTerrorists;
}

auto mapObjectivesOf(const nx::assets::BspView& map) -> MapObjectives {
    MapObjectives objectives;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (const std::optional<MapObjective> objective =
                objectiveOfClass(entity.find("classname").value_or(""))) {
            objectives.set(*objective);
        }
    }
    return objectives;
}

auto hostagesOf(const nx::assets::BspView& map) -> std::uint32_t {
    return nx::narrow<std::uint32_t>(
        std::ranges::count_if(map.entities(), [](const nx::assets::BspEntity& entity) {
            return entity.find("classname") == kHostageClass;
        }));
}

auto spawnGame(const nx::assets::BspView& map, const nx::game::EntityClasses& classes, nx::ecs::World& world)
    -> nx::Result<void, GameMapError> {
    if (auto registered = nx::game::registerComponents(world); !registered) {
        return nx::fail(GameMapError::Unspawnable, std::move(registered.error()));
    }
    if (auto spawned = nx::game::spawnBrushEntities(world, map.entities(), map.models(), classes); !spawned) {
        return nx::fail(GameMapError::Unspawnable, std::move(spawned.error()));
    }
    return {};
}

auto GameMap::load(const nx::assets::Vfs& vfs, std::string_view name)
    -> nx::Result<std::unique_ptr<GameMap>, GameMapError> {
    const auto path = nx::assets::VfsPath::parse(std::format("maps/{}.bsp", name));
    if (!path) {
        return nx::fail(GameMapError::Unreadable, std::string{name}, path.error());
    }
    auto bytes = vfs.readAll(*path);
    if (!bytes) {
        return nx::fail(GameMapError::Unreadable, std::string{name}, std::move(bytes.error()));
    }
    return read(std::move(*bytes), name);
}

auto GameMap::read(std::vector<std::byte> bytes, std::string_view name)
    -> nx::Result<std::unique_ptr<GameMap>, GameMapError> {
    auto made = std::make_unique<GameMap>(CreateToken{});
    GameMap& gameMap = *made;
    gameMap.m_name = std::string{name};
    gameMap.m_bytes = std::move(bytes);
    auto map = nx::assets::BspView::parse(gameMap.m_bytes);
    if (!map) {
        return nx::fail(GameMapError::Unreadable, std::string{name}, std::move(map.error()));
    }
    gameMap.m_map = std::move(*map);
    const nx::assets::BspView& bsp = *gameMap.m_map;
    auto hulls = nx::collision::BrushHulls::create(bsp);
    if (!hulls) {
        return nx::fail(GameMapError::HullsInvalid, std::string{name}, std::move(hulls.error()));
    }
    gameMap.m_hulls = std::move(*hulls);
    for (const Team team : nx::EnumArray<Team, std::vector<SpawnPoint>>::keys()) {
        gameMap.m_spawnPoints[team] = spawnPointsOf(bsp, team);
    }
    gameMap.m_objectives = mapObjectivesOf(bsp);
    gameMap.m_hostages = hostagesOf(bsp);
    gameMap.m_surfaceExtents = nx::assets::surfaceExtentsOf(bsp);
    return made;
}

auto GameMap::materialAlong(const TextureMaterials& materials, nx::assets::ModelIndex model, nx::Vec3 start,
                            nx::Vec3 end) const -> TextureMaterial {
    const nx::assets::BspView& map = *m_map;
    const std::optional<nx::assets::FaceIndex> face =
        nx::assets::surfaceAt(map, m_surfaceExtents, model, start, end);
    if (!face) {
        return TextureMaterial::Concrete;
    }
    const nx::assets::TextureInfo& faceTexture =
        map.textureInfos()[map.faces()[face->value].textureInfo.value];
    const std::optional<nx::assets::MipTexture>& texture = map.textures()[faceTexture.texture.value];
    return texture ? materials.materialOf(lookedUpTextureName(texture->name)) : TextureMaterial::Concrete;
}

} // namespace cs
