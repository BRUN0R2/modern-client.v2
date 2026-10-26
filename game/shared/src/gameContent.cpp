// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.platform;
import nx.assets;
import nx.game;

namespace cs {

namespace {

constexpr nx::log::Category kLog{"game"};

/// The extension of the project's data files.
constexpr std::string_view kDataExtension = ".toml";
/// What the installation says its textures are made of.
constexpr std::string_view kMaterialsFile = "sound/materials.txt";

/// The folder of the game's data in the project's data/ folder.
[[nodiscard]] auto gameDataOf(const std::filesystem::path& data) -> std::filesystem::path {
    return data / "cstrike";
}

/// What `read` makes of the text of `file`, which it is given with the file's name. Errors: Unavailable.
template <class Read>
[[nodiscard]] auto readDataFile(const std::filesystem::path& file, Read read)
    -> nx::Result<typename std::invoke_result_t<Read&, std::string_view, std::string_view>::value_type,
                  GameContentError> {
    auto text = nx::platform::readTextFile(file);
    if (!text) {
        return nx::fail(GameContentError::Unavailable, std::move(text.error()));
    }
    auto made = read(std::string_view{*text}, std::string_view{nx::platform::toUtf8(file)});
    if (!made) {
        return nx::fail(GameContentError::Unavailable, std::move(made.error()));
    }
    return std::move(*made);
}

/// The game's weapons, from `data`, the project's data/ folder: cstrike/ammo.toml and each
/// cstrike/weapons/<id>.toml, which must hold the weapon `id`. Errors: Unavailable.
[[nodiscard]] auto readWeaponCatalog(const std::filesystem::path& data)
    -> nx::Result<WeaponCatalog, GameContentError> {
    auto ammo = readDataFile(gameDataOf(data) / "ammo.toml", readAmmoTypes);
    if (!ammo) {
        return std::unexpected{std::move(ammo.error())};
    }
    const std::filesystem::path folder = gameDataOf(data) / "weapons";
    auto files = nx::platform::listFiles(folder);
    if (!files) {
        return nx::fail(GameContentError::Unavailable, std::move(files.error()));
    }
    std::vector<Weapon> weapons;
    for (const std::filesystem::path& file : *files) {
        if (file.has_parent_path() || file.extension() != kDataExtension) {
            continue;
        }
        const std::filesystem::path path = folder / file;
        auto weapon = readDataFile(path, readWeapon);
        if (!weapon) {
            return std::unexpected{std::move(weapon.error())};
        }
        if (weapon->id != nx::platform::toUtf8(file.stem())) {
            return nx::fail(GameContentError::Unavailable,
                            std::format("{}: the weapon {:?}, in a file not named after it",
                                        nx::platform::toUtf8(path), weapon->id));
        }
        weapons.push_back(std::move(*weapon));
    }
    auto catalog = WeaponCatalog::make(std::move(*ammo), std::move(weapons));
    if (!catalog) {
        return nx::fail(GameContentError::Unavailable, nx::platform::toUtf8(folder),
                        std::move(catalog.error()));
    }
    return std::move(*catalog);
}

/// What the textures of `vfs`'s installation are made of: its sound/materials.txt; none tagged, and
/// logged, without it, as the original carries on.
[[nodiscard]] auto readTextureMaterials(const nx::assets::Vfs& vfs) -> TextureMaterials {
    auto bytes = nx::assets::VfsPath::parse(kMaterialsFile).and_then([&vfs](const nx::assets::VfsPath& path) {
        return vfs.readAll(path);
    });
    if (!bytes) {
        nx::log::warning(kLog, "{} is missing or unreadable: every texture sounds as concrete",
                         kMaterialsFile);
        return TextureMaterials{};
    }
    return TextureMaterials::read(*bytes | std::views::transform([](std::byte value) {
        return static_cast<char>(value);
    }) | std::ranges::to<std::string>());
}

} // namespace

auto addonsFolderOf(const std::filesystem::path& userFolder) -> std::filesystem::path {
    return userFolder / "addons" / "cstrike";
}

auto openGameContent(const std::filesystem::path& data, const std::optional<std::filesystem::path>& install,
                     const std::optional<std::filesystem::path>& addons)
    -> nx::Result<GameContent, GameContentError> {
    auto description =
        readDataFile(data / "content" / "goldsrcCstrike.toml", nx::assets::readGoldsrcInstallDesc);
    if (!description) {
        return std::unexpected{std::move(description.error())};
    }
    auto installation = install
                            ? nx::assets::GoldsrcInstall::open(*description, *install)
                            : nx::assets::detectGoldsrcInstall(*description, nx::platform::steamFolders());
    if (!installation) {
        return nx::fail(GameContentError::Unavailable, std::move(installation.error()));
    }
    std::vector<std::filesystem::path> mounts;
    if (std::error_code error; addons && std::filesystem::is_directory(*addons, error)) {
        nx::log::info(kLog, "the user's own content: {}", nx::platform::toUtf8(*addons));
        mounts.push_back(*addons);
    }
    std::ranges::copy(installation->mounts(), std::back_inserter(mounts));
    auto vfs = nx::assets::Vfs::create(mounts);
    if (!vfs) {
        return nx::fail(GameContentError::Unavailable, std::move(vfs.error()));
    }
    return readGameContent(data, std::move(*vfs));
}

auto readGameContent(const std::filesystem::path& data, nx::assets::Vfs vfs)
    -> nx::Result<GameContent, GameContentError> {
    auto classes = readDataFile(gameDataOf(data) / "entities.toml", nx::game::readEntityClasses);
    if (!classes) {
        return std::unexpected{std::move(classes.error())};
    }
    auto weapons = readWeaponCatalog(data);
    if (!weapons) {
        return std::unexpected{std::move(weapons.error())};
    }
    auto loadouts = readDataFile(gameDataOf(data) / "loadouts.toml",
                                 [&weapons](std::string_view text, std::string_view sourceName) {
                                     return readLoadouts(text, sourceName, *weapons);
                                 });
    if (!loadouts) {
        return std::unexpected{std::move(loadouts.error())};
    }
    auto economy = readDataFile(gameDataOf(data) / "economy.toml", readEconomyRules);
    if (!economy) {
        return std::unexpected{std::move(economy.error())};
    }
    auto footsteps = readDataFile(gameDataOf(data) / "footsteps.toml", readFootstepRules);
    if (!footsteps) {
        return std::unexpected{std::move(footsteps.error())};
    }
    auto breath = readDataFile(gameDataOf(data) / "breath.toml", readBreathRules);
    if (!breath) {
        return std::unexpected{std::move(breath.error())};
    }
    auto hitGroups = readDataFile(gameDataOf(data) / "hitGroups.toml", readHitGroupRules);
    if (!hitGroups) {
        return std::unexpected{std::move(hitGroups.error())};
    }
    auto playerHarm = readDataFile(gameDataOf(data) / "playerHarm.toml", readPlayerHarmRules);
    if (!playerHarm) {
        return std::unexpected{std::move(playerHarm.error())};
    }
    auto use = readDataFile(gameDataOf(data) / "use.toml", readUseRules);
    if (!use) {
        return std::unexpected{std::move(use.error())};
    }
    auto navigation = readDataFile(gameDataOf(data) / "navigation.toml", nx::game::readNavigationData);
    if (!navigation) {
        return std::unexpected{std::move(navigation.error())};
    }
    TextureMaterials materials = readTextureMaterials(vfs);
    return GameContent{.vfs = std::move(vfs),
                       .entityClasses = std::move(*classes),
                       .weapons = std::move(*weapons),
                       .loadouts = std::move(*loadouts),
                       .economy = std::move(*economy),
                       .footsteps = std::move(*footsteps),
                       .breath = std::move(*breath),
                       .hitGroups = std::move(*hitGroups),
                       .playerHarm = std::move(*playerHarm),
                       .use = std::move(*use),
                       .navigation = std::move(*navigation),
                       .materials = std::move(materials)};
}

} // namespace cs
