// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The brush entities of a map, as the game makes them when the map starts (docs/03-ecs.md,
/// section 11): an entity of the ECS world for each one whose class data/cstrike/entities.toml
/// describes, with its brush model, where it is, how solid it is, and what moves it.
export module nx.game:brushEntities;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;
import nx.ecs;
import nx.assets;
import nx.collision;
import :components;

export namespace nx::game {

/// What can be wrong with the game's entities.
enum class GameError : std::uint8_t {
    ConfigInvalid, ///< The entity classes break their schema or their rules (cause: the DataError,
                   ///< when there is one); the detail names the file or the class.
    UnknownModel,  ///< An entity is a brush model the map does not have; the detail names it.
};

/// What the entities of a class of brush entities do.
enum class BrushBehavior : std::uint8_t {
    SlidingDoor,  ///< Slide open when a player touches them, and back (CBaseDoor).
    RotatingDoor, ///< Turn open when a player touches them, away from it, and back (CRotDoor).
    Button,       ///< Go in when a player uses them, or touches them when flagged so, and back out
                  ///< (CBaseButton).
    Wall,         ///< Show their other textures when used (CFuncWall).
    Trigger,      ///< Fire their targets when a player walks into them, again after their "wait"
                  ///< (CTriggerMultiple).
    TriggerOnce,  ///< The same, once (CTriggerOnce).
    Count,
};

/// A class of the brush entities of maps ("func_wall"): how the game makes them when a map starts.
struct BrushClass {
    std::string classname{};
    Solid solid = Solid::Bsp;
    /// Spawnflags any of which makes one not solid ("passable", "starts off").
    std::optional<std::int32_t> notSolidFlags{};
    /// A door: a "skin" other than 0 makes it not solid, holding those contents (func_water).
    std::optional<bool> skinMakesNotSolid{};
    /// The contents the game gives each, whatever its "skin", numbered as the original numbers them
    /// (nx::assets::Contents; func_ladder: -16, a ladder).
    std::optional<std::int32_t> contents{};
    /// What they do; nothing when absent: they stay as they are.
    std::optional<BrushBehavior> behavior{};
    /// The game restarts them as each round starts (CleanUpMap); not when absent.
    std::optional<bool> restartsEachRound{};
};

/// data/cstrike/entities.toml: the classes of the entities of Counter-Strike's maps.
struct EntityClasses {
    std::vector<BrushClass> brushClasses{};
};

/// Reads `text`, the file `sourceName`, in which each class appears once. Errors: ConfigInvalid.
[[nodiscard]] auto readEntityClasses(std::string_view text, std::string_view sourceName)
    -> Result<EntityClasses, GameError>;

/// Makes in `world`, in their order, the brush entities of `entities`, a map's whose brush models are
/// `models`, that are of a class of `classes`, with their BrushModel, Transform and BrushSolidity,
/// what their class does (a Door, its Pusher, its Waves and its Targets; a Button, its Pusher and its
/// Targets; a Wall; a Trigger and its Targets), the textures they show (AlternateTextures, by their
/// "frame"), whether their class restarts each round (RestartsEachRound), and their name when they
/// have one (Named); the others are left to what makes them.
/// Returns how many it made. Errors: UnknownModel.
[[nodiscard]] auto spawnBrushEntities(ecs::World& world, std::span<const assets::BspEntity> entities,
                                      std::span<const assets::BspModel> models, const EntityClasses& classes)
    -> Result<std::size_t, GameError>;

/// Restarts the brush entities of `world`, a map's whose hulls are `hulls`, whose class restarts each
/// round, as the game's CleanUpMap restarts them as a round starts, in their order: a door closes
/// again from where it is (CBaseDoor::Restart). The others of those classes (func_breakable, the
/// trains, func_vehicle) restart with their behaviors.
void restartBrushEntities(ecs::World& world, const collision::BrushHulls& hulls);

} // namespace nx::game

template <>
struct nx::meta::Description<nx::game::BrushClass> {
    static constexpr std::string_view kName = "nx::game::BrushClass";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::game::BrushClass::classname>("classname"),
        nx::meta::field<&nx::game::BrushClass::solid>("solid"),
        nx::meta::field<&nx::game::BrushClass::notSolidFlags>("notSolidFlags",
                                                              nx::data::Range{.minimum = 1.0}),
        nx::meta::field<&nx::game::BrushClass::skinMakesNotSolid>("skinMakesNotSolid"),
        nx::meta::field<&nx::game::BrushClass::contents>(
            "contents", nx::data::Range{.minimum = std::to_underlying(nx::assets::Contents::Ladder),
                                        .maximum = std::to_underlying(nx::assets::Contents::Empty)}),
        nx::meta::field<&nx::game::BrushClass::behavior>("behavior"),
        nx::meta::field<&nx::game::BrushClass::restartsEachRound>("restartsEachRound"),
    };
};

template <>
struct nx::meta::Description<nx::game::EntityClasses> {
    static constexpr std::string_view kName = "nx::game::EntityClasses";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::game::EntityClasses::brushClasses>("brushClasses"),
    };
};
