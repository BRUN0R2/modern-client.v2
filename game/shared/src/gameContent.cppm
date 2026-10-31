// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game's content (docs/06-assets-and-resources.md): the Counter-Strike 1.6 installation through its
/// VFS, below the user's own content, and the project's data the game is played by, read once and
/// shared by the server and the client of one process.
export module cs.shared:gameContent;

import std;
import nx.core;
import nx.assets;
import nx.game;
import :weapons;
import :loadouts;
import :economy;
import :materials;
import :footsteps;
import :breath;
import :hitGroups;
import :blood;
import :damageNumbers;
import :playerHarm;
import :use;
import :characters;
import :lightStyles;
import :environment;
import :entityCatalog;

export namespace cs {

/// Why the game's content could not be read.
enum class GameContentError : std::uint8_t {
    Unavailable, ///< The project's data or the installation could not be read (cause).
};

/// The game's files: the Counter-Strike 1.6 installation through its VFS, the classes the game makes
/// the map's entities by (data/cstrike/entities.toml), its weapons (data/cstrike/ammo.toml and
/// data/cstrike/weapons/<id>.toml), what a player of each team spawns with
/// (data/cstrike/loadouts.toml), the money of its rounds (data/cstrike/economy.toml), the sounds of a
/// player's feet (data/cstrike/footsteps.toml) and of its breath in water (data/cstrike/breath.toml),
/// what the hit groups make of an attack's damage (data/cstrike/hitGroups.toml), the blood a hit sheds
/// (data/cstrike/blood.toml), how a player is hurt
/// and killed (data/cstrike/playerHarm.toml), what it hears as it uses (data/cstrike/use.toml), the
/// characters each team plays as (data/cstrike/characters.toml), the world's light styles
/// (data/cstrike/lightStyles.toml), what it shows of every map's world over what the map sets
/// (data/cstrike/environment.toml), how the damage of a player's hits shows
/// (data/cstrike/damageNumbers.toml), and what the textures are made of (the installation's
/// sound/materials.txt; none tagged without it).
struct GameContent {
    nx::assets::Vfs vfs;
    nx::game::EntityClasses entityClasses;
    WeaponCatalog weapons;
    Loadouts loadouts;
    EconomyRules economy;
    FootstepRules footsteps;
    BreathRules breath;
    HitGroupRules hitGroups;
    BloodRules blood;
    PlayerHarmRules playerHarm;
    UseRules use;
    Characters characters;
    TextureMaterials materials;
    WorldLightStyles lightStyles;
    WorldEnvironment environment;
    DamageNumberLook damageNumbers;
    EntityCatalog entities{}; ///< Append-only definitions loaded explicitly by the host or a mod.
};

/// The user's own content of the game in `userFolder` (nx::platform::userDirectory): its
/// addons/cstrike/ folder, the original's cstrike_addon (docs/06-assets-and-resources.md, section
/// 2.1).
[[nodiscard]] auto addonsFolderOf(const std::filesystem::path& userFolder) -> std::filesystem::path;

/// Opens the game's content: the installation at `install`, or the one Steam installed, as `data`, the
/// project's data/ folder, describes its files, below `addons`, the user's own content
/// (addonsFolderOf), when it is given and is a folder: a file there is read instead of the
/// installation's; then the rest of the content, as readGameContent reads it. Errors: Unavailable.
[[nodiscard]] auto openGameContent(const std::filesystem::path& data,
                                   const std::optional<std::filesystem::path>& install,
                                   const std::optional<std::filesystem::path>& addons)
    -> nx::Result<GameContent, GameContentError>;

/// The game's content of `vfs`, the installation's files: from `data`, the project's data/ folder, the
/// entity classes, the weapons, each in the file named after its id, the loadouts, the money of the
/// rounds, the footsteps, the breath, the hit groups, the player's harm, its use, the characters and the
/// world's light styles; what
/// the textures are made of, from `vfs`. Errors: Unavailable.
[[nodiscard]] auto readGameContent(const std::filesystem::path& data, nx::assets::Vfs vfs)
    -> nx::Result<GameContent, GameContentError>;

} // namespace cs
