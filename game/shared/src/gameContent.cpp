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

/// Read definitions from one folder. A local definition can replace one with the same id.
[[nodiscard]] auto readWeapons(const std::filesystem::path& folder, std::vector<Weapon>& weapons,
                               bool optional) -> nx::Result<void, GameContentError> {
    auto files = nx::platform::listFiles(folder);
    if (!files) {
        if (optional && files.error().code == nx::platform::FileError::NotFound) {
            return {};
        }
        return nx::fail(GameContentError::Unavailable, std::move(files.error()));
    }
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
        const auto existing = std::ranges::find(weapons, weapon->id, &Weapon::id);
        if (existing == weapons.end()) {
            weapons.push_back(std::move(*weapon));
        } else {
            *existing = std::move(*weapon);
        }
    }
    return {};
}

[[nodiscard]] auto readWeaponCatalog(const std::filesystem::path& data,
                                     const std::optional<std::filesystem::path>& addons)
    -> nx::Result<WeaponCatalog, GameContentError> {
    auto ammo = readDataFile(gameDataOf(data) / "ammo.toml", readAmmoTypes);
    if (!ammo) {
        return std::unexpected{std::move(ammo.error())};
    }
    const auto folder = gameDataOf(data) / "weapons";
    std::vector<Weapon> weapons;
    if (auto read = readWeapons(folder, weapons, false); !read) {
        return std::unexpected{std::move(read.error())};
    }
    if (addons) {
        const auto ammoFile = *addons / "ammo.toml";
        auto text = nx::platform::readTextFile(ammoFile);
        if (text) {
            auto extra = readAmmoTypes(*text, nx::platform::toUtf8(ammoFile));
            if (!extra) {
                return nx::fail(GameContentError::Unavailable, std::move(extra.error()));
            }
            std::ranges::move(extra->ammo, std::back_inserter(ammo->ammo));
        } else if (text.error().code != nx::platform::FileError::NotFound) {
            return nx::fail(GameContentError::Unavailable, std::move(text.error()));
        }
        if (auto read = readWeapons(*addons / "weapons", weapons, true); !read) {
            return std::unexpected{std::move(read.error())};
        }
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
    // The game's own: the user's content, and the first of the installation's folders, over its base.
    const std::size_t gameMounts = mounts.size() + std::min<std::size_t>(installation->mounts().size(), 1);
    std::ranges::copy(installation->mounts(), std::back_inserter(mounts));
    // Steam keeps shared UI resources such as TrackerScheme.res in this optional folder.
    const auto platformFolder = installation->folder() / "platform";
    if (std::error_code error; std::filesystem::is_directory(platformFolder, error)) {
        mounts.push_back(platformFolder);
    }
    auto vfs = nx::assets::Vfs::create(mounts);
    if (!vfs) {
        return nx::fail(GameContentError::Unavailable, std::move(vfs.error()));
    }
    auto content = readGameContent(data, std::move(*vfs), addons);
    if (content) {
        content->gameMounts = gameMounts;
    }
    return content;
}

auto readGameContent(const std::filesystem::path& data, nx::assets::Vfs vfs,
                     const std::optional<std::filesystem::path>& addons)
    -> nx::Result<GameContent, GameContentError> {
    auto classes = readDataFile(gameDataOf(data) / "entities.toml", nx::game::readEntityClasses);
    if (!classes) {
        return std::unexpected{std::move(classes.error())};
    }
    auto weapons = readWeaponCatalog(data, addons);
    if (!weapons) {
        return std::unexpected{std::move(weapons.error())};
    }
    auto loadoutFile = gameDataOf(data) / "loadouts.toml";
    if (addons) {
        const auto localLoadouts = *addons / "loadouts.toml";
        std::error_code error;
        if (std::filesystem::exists(localLoadouts, error) || error) {
            loadoutFile = localLoadouts;
        }
    }
    auto loadouts = readDataFile(loadoutFile, [&weapons](std::string_view text, std::string_view sourceName) {
        return readLoadouts(text, sourceName, *weapons);
    });
    if (!loadouts) {
        return std::unexpected{std::move(loadouts.error())};
    }
    auto economy = readDataFile(gameDataOf(data) / "economy.toml", readEconomyRules);
    if (!economy) {
        return std::unexpected{std::move(economy.error())};
    }
    auto buying = readDataFile(gameDataOf(data) / "buying.toml", readBuyingRules);
    if (!buying) {
        return std::unexpected{std::move(buying.error())};
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
    auto blood = readDataFile(gameDataOf(data) / "blood.toml", readBloodRules);
    if (!blood) {
        return std::unexpected{std::move(blood.error())};
    }
    auto playerHarm = readDataFile(gameDataOf(data) / "playerHarm.toml", readPlayerHarmRules);
    if (!playerHarm) {
        return std::unexpected{std::move(playerHarm.error())};
    }
    auto use = readDataFile(gameDataOf(data) / "use.toml", readUseRules);
    if (!use) {
        return std::unexpected{std::move(use.error())};
    }
    auto characters = readDataFile(gameDataOf(data) / "characters.toml", readCharacters);
    if (!characters) {
        return std::unexpected{std::move(characters.error())};
    }
    auto lightStyles = readDataFile(gameDataOf(data) / "lightStyles.toml", readWorldLightStyles);
    if (!lightStyles) {
        return std::unexpected{std::move(lightStyles.error())};
    }
    auto environment = readDataFile(gameDataOf(data) / "environment.toml", readWorldEnvironment);
    if (!environment) {
        return std::unexpected{std::move(environment.error())};
    }
    auto buyMenu = readDataFile(gameDataOf(data) / "buyMenu.toml", readBuyMenuLook);
    if (!buyMenu) {
        return std::unexpected{std::move(buyMenu.error())};
    }
    auto mainMenu = readDataFile(gameDataOf(data) / "mainMenu.toml", readMainMenuLook);
    if (!mainMenu) {
        return std::unexpected{std::move(mainMenu.error())};
    }
    auto damageNumbers = readDataFile(gameDataOf(data) / "damageNumbers.toml", readDamageNumberLook);
    if (!damageNumbers) {
        return std::unexpected{std::move(damageNumbers.error())};
    }
    auto stepIndicator = readDataFile(gameDataOf(data) / "stepIndicator.toml", readStepIndicatorLook);
    if (!stepIndicator) {
        return std::unexpected{std::move(stepIndicator.error())};
    }
    TextureMaterials materials = readTextureMaterials(vfs);
    const std::size_t gameMounts = vfs.mountCount();
    return GameContent{.vfs = std::move(vfs),
                       .gameMounts = gameMounts,
                       .entityClasses = std::move(*classes),
                       .weapons = std::move(*weapons),
                       .loadouts = std::move(*loadouts),
                       .economy = std::move(*economy),
                       .buying = std::move(*buying),
                       .buyMenu = std::move(*buyMenu),
                       .mainMenu = std::move(*mainMenu),
                       .footsteps = std::move(*footsteps),
                       .breath = std::move(*breath),
                       .hitGroups = std::move(*hitGroups),
                       .blood = std::move(*blood),
                       .playerHarm = std::move(*playerHarm),
                       .use = std::move(*use),
                       .characters = std::move(*characters),
                       .materials = std::move(materials),
                       .lightStyles = std::move(*lightStyles),
                       .environment = std::move(*environment),
                       .damageNumbers = std::move(*damageNumbers),
                       .stepIndicator = std::move(*stepIndicator)};
}

} // namespace cs
