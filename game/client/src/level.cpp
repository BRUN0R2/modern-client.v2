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
/// The engine's palette, in the base game's folder.
constexpr std::string_view kPalettePath = "gfx/palette.lmp";

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

/// The engine's palette in `vfs`; none, logged, when it is missing or unreadable.
[[nodiscard]] auto readPalette(const nx::assets::Vfs& vfs) -> std::optional<nx::assets::Palette> {
    const auto unread = [](std::string_view why) {
        nx::log::warning(kLog, "the engine's palette {} cannot be read: {}; no blood is sprayed",
                         kPalettePath, why);
    };
    const auto bytes =
        nx::assets::VfsPath::parse(kPalettePath).and_then([&vfs](const nx::assets::VfsPath& path) {
            return vfs.readAll(path);
        });
    if (!bytes) {
        unread(nx::describe(bytes.error()));
        return std::nullopt;
    }
    auto palette = nx::assets::Palette::read(*bytes);
    if (!palette) {
        unread(nx::describe(palette.error()));
        return std::nullopt;
    }
    return std::move(*palette);
}

/// `names`, separated by commas.
template <class Names>
[[nodiscard]] auto listed(const Names& names) -> std::string {
    return names | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
}

} // namespace

auto Level::load(const GameContent& content, const GameMap& map)
    -> nx::Result<std::unique_ptr<Level>, LevelError> {
    auto made = std::make_unique<Level>(CreateToken{});
    Level& level = *made;
    level.m_gameMap = &map;
    level.entities = &content.entities;
    const nx::assets::BspView& bsp = map.map();
    level.m_skyLight = skyLightOf(bsp);
    level.m_lights = highlightLightsOf(bsp);
    level.m_probePositions = probePositionsOf(bsp);
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
    level.enginePalette = readPalette(content.vfs);
    level.bloodRules = &content.blood;
    const std::vector<std::string> bloodSprites{content.blood.sprayLook.spraySprite,
                                                content.blood.sprayLook.dropSprite};
    if (const std::vector<std::string> missing = level.bloodSpriteFiles.read(content.vfs, bloodSprites);
        !missing.empty()) {
        nx::log::warning(kLog, "the blood's sprites that cannot be read: {}", listed(missing));
    }
    const std::array streakSprites{std::string{kStreakSprite}};
    if (const std::vector<std::string> missing = level.streakSpriteFiles.read(content.vfs, streakSprites);
        !missing.empty()) {
        nx::log::warning(kLog, "the streaks' sprite that cannot be read, no streak drawn: {}",
                         listed(missing));
    }
    level.m_players = PlayerModels::read(content.vfs, content.characters, content.weapons);
    if (!level.m_players->missing().empty()) {
        nx::log::warning(kLog, "models the players are drawn with that cannot be read: {}",
                         listed(level.m_players->missing()));
    }
    // The decals: the bullets', and those the map stamps as its game starts, on what is there then.
    nx::ecs::World resting;
    if (auto spawned = spawnGame(bsp, content.entityClasses, resting); !spawned) {
        return nx::fail(LevelError::GameUnspawnable, std::move(spawned.error()));
    }
    const std::vector<nx::game::StaticDecal> stamped =
        nx::game::staticDecalsOf(bsp.entities(), resting, map.hulls());
    std::vector<std::string_view> decalNames = bulletDecals();
    std::ranges::copy(content.blood.decals, std::back_inserter(decalNames));
    std::ranges::transform(
        stamped, std::back_inserter(decalNames),
        [](const nx::game::StaticDecal& decal) -> std::string_view { return decal.texture; });
    level.m_decals = LevelDecals::read(content.vfs, decalNames);
    if (!level.m_decals->missing().empty()) {
        nx::log::warning(kLog,
                         "decals the bullets and the blood leave or the map stamps that cannot be read: {}",
                         listed(level.m_decals->missing()));
    }
    for (const nx::game::StaticDecal& decal : stamped) {
        if (const std::optional<nx::render::DecalImageIndex> index = level.m_decals->indexOf(decal.texture)) {
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
        level.readMuzzleFlashes(content.vfs, *viewModel.model);
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
        m_players->readWeapon(vfs, weapons, weapon);
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
        readMuzzleFlashes(vfs, *viewModel.model);
    }
    return updated;
}

void Level::readMuzzleFlashes(const nx::assets::Vfs& vfs, const ViewModel& viewModel) {
    const std::vector<std::string> missing = m_muzzleFlashes.read(vfs, viewModel.muzzleFlashSprites());
    if (!missing.empty()) {
        nx::log::warning(kLog, "muzzle flash sprites that cannot be read: {}", listed(missing));
    }
}

auto Level::bulletMarkOn(nx::assets::ModelIndex model) const noexcept -> BulletMark {
    nx::expects(model.value < m_bulletMarks.size(), "Level::bulletMarkOn: a brush model of the map");
    return model.value < m_bulletMarks.size() ? m_bulletMarks[model.value] : BulletMark::None;
}

auto spawnView(const nx::assets::BspView& map) -> nx::render::Camera {
    const std::optional<TeamSpawn> spawn = firstSpawnOf(map);
    if (!spawn) {
        return nx::render::Camera{};
    }
    return nx::render::Camera{.position = spawn->point.origin + nx::Vec3{.z = kStandingViewHeight.value()},
                              .angles = {.yaw = spawn->point.yaw}};
}

} // namespace cs
