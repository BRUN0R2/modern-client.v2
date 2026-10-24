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

/// The extension of the project's data files.
constexpr std::string_view kDataExtension = ".toml";

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

/// The mark a bullet leaves on each brush model of `map`: Gunshot on the world and on a model no entity
/// is, bulletMarkOf its entity on the others.
[[nodiscard]] auto bulletMarksOf(const nx::assets::BspView& map) -> std::vector<BulletMark> {
    std::vector<BulletMark> marks(map.models().size(), BulletMark::Gunshot);
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::optional<nx::assets::ModelIndex> model = entity.findBrushModel();
        if (model && *model != nx::assets::kWorldModel && model->value < marks.size()) {
            marks[model->value] = bulletMarkOf(entity);
        }
    }
    return marks;
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

/// The game's weapons, from `data`, the project's data/ folder: cstrike/ammo.toml and each
/// cstrike/weapons/<id>.toml, which must hold the weapon `id`. Errors: ContentUnavailable.
[[nodiscard]] auto readWeaponCatalog(const std::filesystem::path& data)
    -> nx::Result<WeaponCatalog, LevelError> {
    const std::filesystem::path ammoFile = data / "cstrike" / "ammo.toml";
    auto ammoText = nx::platform::readTextFile(ammoFile);
    if (!ammoText) {
        return nx::fail(LevelError::ContentUnavailable, std::move(ammoText.error()));
    }
    auto ammo = readAmmoTypes(*ammoText, nx::platform::toUtf8(ammoFile));
    if (!ammo) {
        return nx::fail(LevelError::ContentUnavailable, std::move(ammo.error()));
    }
    const std::filesystem::path folder = data / "cstrike" / "weapons";
    auto files = nx::platform::listFiles(folder);
    if (!files) {
        return nx::fail(LevelError::ContentUnavailable, std::move(files.error()));
    }
    std::vector<Weapon> weapons;
    for (const std::filesystem::path& file : *files) {
        if (file.has_parent_path() || file.extension() != kDataExtension) {
            continue;
        }
        const std::filesystem::path path = folder / file;
        auto text = nx::platform::readTextFile(path);
        if (!text) {
            return nx::fail(LevelError::ContentUnavailable, std::move(text.error()));
        }
        auto weapon = readWeapon(*text, nx::platform::toUtf8(path));
        if (!weapon) {
            return nx::fail(LevelError::ContentUnavailable, std::move(weapon.error()));
        }
        if (weapon->id != nx::platform::toUtf8(file.stem())) {
            return nx::fail(LevelError::ContentUnavailable,
                            std::format("{}: the weapon {:?}, in a file not named after it",
                                        nx::platform::toUtf8(path), weapon->id));
        }
        weapons.push_back(std::move(*weapon));
    }
    auto catalog = WeaponCatalog::make(std::move(*ammo), std::move(weapons));
    if (!catalog) {
        return nx::fail(LevelError::ContentUnavailable, nx::platform::toUtf8(folder),
                        std::move(catalog.error()));
    }
    return std::move(*catalog);
}

/// The first spawn of a map with an origin, and the team it spawns.
struct FirstSpawn {
    const nx::assets::BspEntity* entity{};
    nx::Vec3 origin{};
    Team team{};
};

/// The first spawn of `map` with an origin: a counter-terrorist one (info_player_start), else a
/// terrorist one (info_player_deathmatch); none.
[[nodiscard]] auto firstSpawnOf(const nx::assets::BspView& map) -> std::optional<FirstSpawn> {
    for (const auto& [classname, team] :
         {std::pair{std::string_view{"info_player_start"}, Team::CounterTerrorists},
          std::pair{std::string_view{"info_player_deathmatch"}, Team::Terrorists}}) {
        for (const nx::assets::BspEntity& entity : map.entities()) {
            if (entity.find("classname") != classname) {
                continue;
            }
            if (const std::optional<nx::Vec3> origin = entity.findVector("origin")) {
                return FirstSpawn{.entity = &entity, .origin = *origin, .team = team};
            }
        }
    }
    return std::nullopt;
}

/// `names`, separated by commas.
template <class Names>
[[nodiscard]] auto listed(const Names& names) -> std::string {
    return names | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
}

} // namespace

auto addonsFolderOf(const std::filesystem::path& userFolder) -> std::filesystem::path {
    return userFolder / "addons" / "cstrike";
}

auto openGameContent(const std::filesystem::path& data, const std::optional<std::filesystem::path>& install,
                     const std::optional<std::filesystem::path>& addons)
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
    std::vector<std::filesystem::path> mounts;
    if (std::error_code error; addons && std::filesystem::is_directory(*addons, error)) {
        nx::log::info(kLog, "the user's own content: {}", nx::platform::toUtf8(*addons));
        mounts.push_back(*addons);
    }
    std::ranges::copy(installation->mounts(), std::back_inserter(mounts));
    auto vfs = nx::assets::Vfs::create(mounts);
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
    auto weapons = readWeaponCatalog(data);
    if (!weapons) {
        return std::unexpected{std::move(weapons.error())};
    }
    const std::filesystem::path loadoutsFile = data / "cstrike" / "loadouts.toml";
    auto loadoutsText = nx::platform::readTextFile(loadoutsFile);
    if (!loadoutsText) {
        return nx::fail(LevelError::ContentUnavailable, std::move(loadoutsText.error()));
    }
    auto loadouts = readLoadouts(*loadoutsText, nx::platform::toUtf8(loadoutsFile), *weapons);
    if (!loadouts) {
        return nx::fail(LevelError::ContentUnavailable, std::move(loadouts.error()));
    }
    return GameContent{.vfs = std::move(*vfs),
                       .entityClasses = std::move(*classes),
                       .weapons = std::move(*weapons),
                       .loadouts = std::move(*loadouts)};
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
    level.m_lights = highlightLightsOf(bsp);
    level.m_probePositions = probePositionsOf(bsp);
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
    level.m_bulletMarks = bulletMarksOf(bsp);
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
    // The decals: the bullets', and those the map stamps as its game starts, on what is there then.
    const std::vector<nx::game::StaticDecal> stamped =
        nx::game::staticDecalsOf(bsp.entities(), resting, *level.m_hulls);
    std::vector<std::string_view> decalNames = bulletDecals();
    std::ranges::transform(
        stamped, std::back_inserter(decalNames),
        [](const nx::game::StaticDecal& decal) -> std::string_view { return decal.texture; });
    level.m_decals = LevelDecals::read(content.vfs, decalNames);
    if (!level.m_decals->missing().empty()) {
        nx::log::warning(kLog, "decals the bullets leave or the map stamps that cannot be read: {}",
                         listed(level.m_decals->missing()));
    }
    for (const nx::game::StaticDecal& decal : stamped) {
        if (const std::optional<std::uint32_t> index = level.m_decals->indexOf(decal.texture)) {
            level.m_mapDecals.push_back(MapDecal{
                .decal = *index, .origin = decal.origin, .on = decalTargetOf(resting, decal.entity)});
        }
    }
    const WeaponCatalog& weapons = content.weapons;
    for (std::uint32_t index = 0; index < weapons.weapons().size(); ++index) {
        const WeaponIndex weapon{index};
        WeaponViewModel& viewModel =
            level.m_viewModels.emplace_back(WeaponViewModel{.revision = weapons.revision(weapon)});
        if (!weapons.isComplete(weapon)) {
            continue; // shown once its values make a weapon
        }
        auto read = ViewModel::read(content.vfs, weapons.weapon(weapon));
        if (!read) {
            return nx::fail(LevelError::ViewModelUnreadable, weapons.weapon(weapon).models.view,
                            std::move(read.error()));
        }
        viewModel.model = std::move(*read);
    }
    level.m_muzzleFlashes = MuzzleFlashSprites::read(content.vfs);
    if (!level.m_muzzleFlashes->missing().empty()) {
        nx::log::warning(kLog, "muzzle flash sprites that cannot be read: {}",
                         listed(level.m_muzzleFlashes->missing()));
    }
    return made;
}

auto Level::viewModel(WeaponIndex weapon) const noexcept -> const ViewModel* {
    if (weapon.value >= m_viewModels.size() || !m_viewModels[weapon.value].model) {
        return nullptr;
    }
    return &*m_viewModels[weapon.value].model;
}

auto Level::weapons() const -> std::vector<WeaponIndex> {
    return std::views::iota(std::uint32_t{0}, nx::narrow<std::uint32_t>(m_viewModels.size())) |
           std::views::transform([](std::uint32_t index) { return WeaponIndex{index}; }) |
           std::ranges::to<std::vector>();
}

auto Level::updateViewModels(const nx::assets::Vfs& vfs, const WeaponCatalog& weapons)
    -> std::vector<WeaponIndex> {
    std::vector<WeaponIndex> updated;
    for (std::uint32_t index = 0; index < weapons.weapons().size(); ++index) {
        const WeaponIndex weapon{index};
        const std::uint32_t revision = weapons.revision(weapon);
        if (index < m_viewModels.size() && m_viewModels[index].revision == revision) {
            continue;
        }
        if (index == m_viewModels.size()) {
            m_viewModels.emplace_back();
        }
        WeaponViewModel& viewModel = m_viewModels[index];
        viewModel = WeaponViewModel{.revision = revision};
        updated.push_back(weapon);
        if (!weapons.isComplete(weapon)) {
            continue;
        }
        const Weapon& made = weapons.weapon(weapon);
        auto read = ViewModel::read(vfs, made);
        if (!read) {
            nx::log::warning(kLog, "{} is not seen in hand: {}", made.id, nx::describe(read.error()));
            continue;
        }
        viewModel.model = std::move(*read);
    }
    return updated;
}

auto Level::bulletMarkOn(nx::assets::ModelIndex model) const noexcept -> BulletMark {
    nx::expects(model.value < m_bulletMarks.size(), "Level::bulletMarkOn: a brush model of the map");
    return model.value < m_bulletMarks.size() ? m_bulletMarks[model.value] : BulletMark::None;
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
    const std::optional<FirstSpawn> spawn = firstSpawnOf(map);
    if (!spawn) {
        return nx::render::Camera{};
    }
    const std::optional<nx::Vec3> angles = spawn->entity->findVector("angles");
    return nx::render::Camera{.position = spawn->origin + nx::Vec3{.z = kStandingViewHeight.value()},
                              .angles = {.yaw = nx::Degrees{angles ? angles->y : 0.0F}}};
}

auto spawnTeam(const nx::assets::BspView& map) -> Team {
    const std::optional<FirstSpawn> spawn = firstSpawnOf(map);
    return spawn ? spawn->team : Team::CounterTerrorists;
}

} // namespace cs
