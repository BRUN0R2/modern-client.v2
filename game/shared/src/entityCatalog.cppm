// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Validated definitions and models for command-driven humanoid entities (ADR-0028).
export module cs.shared:entityCatalog;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;
import nx.assets;
import nx.ecs;
import :modelFiles;
import :studio;
import :hitGroups;
import :entityKnockback;

export namespace cs {

/// Why an entity's type or body could not be made.
enum class EntityError : std::uint8_t {
    InvalidDefinition, ///< A value out of its range, an id taken, a model without hitboxes, a looping death.
    ModelUnreadable,   ///< Its model could not be read (cause: the model's error).
    UnknownSequence,   ///< A sequence it names is not in its model.
    InvalidPlacement,  ///< Nonfinite, not upright, or in what is solid.
    UnknownEntity,     ///< No body of that handle lives.
};

/// A type's place in its EntityCatalog, as the catalog issued it.
struct EntityTypeIndex {
    std::uint32_t value = 0;
    [[nodiscard]] inline auto operator==(const EntityTypeIndex&) const noexcept -> bool = default;
};

/// A model's place in its EntityCatalog, as the catalog issued it.
struct EntityModelIndex {
    std::uint32_t value = 0;
    [[nodiscard]] inline auto operator==(const EntityModelIndex&) const noexcept -> bool = default;
};

/// How far a type's model is drawn from its body's origin, each way.
struct EntityModelOffset {
    nx::HammerUnits x{};
    nx::HammerUnits y{};
    nx::HammerUnits z{};
    [[nodiscard]] inline auto vector() const noexcept -> nx::Vec3 {
        return {.x = x.value(), .y = y.value(), .z = z.value()};
    }
};

/// A body moves as a player does, standing or ducked, in a player's boxes. Models may have a different
/// origin.
struct EntityDefinition {
    std::string id{};
    std::string model{};
    EntityModelOffset modelOffset{};
    nx::HitPoints health{};
    nx::UnitsPerSecond speed{};
    nx::Damage meleeDamage{};
    nx::HammerUnits meleeReach{};
    nx::Seconds meleeInterval{};
    std::string idleSequence{};
    std::string walkSequence{};
    std::string attackSequence{};
    std::string deathSequence{};
    /// What its body plays ducked, at rest and walking; none, what it plays standing.
    std::optional<std::string> crouchIdleSequence{};
    std::optional<std::string> crouchWalkSequence{};
    /// How far its model is drawn from its body's origin ducked; none, modelOffset. A ducked body's origin is
    /// 18 units lower over the same feet: a player's model has its crouched sequences there, and a model
    /// whose sequences keep their feet where they are standing is drawn that much higher.
    std::optional<EntityModelOffset> crouchedModelOffset{};
    HitGroupShares hitGroups{};
    /// The palette entry of its blood's color (m_bloodColor: 247, BLOOD_COLOR_RED); none for a body that
    /// does not bleed (DONT_BLEED).
    std::optional<std::uint8_t> bloodColor{};
    /// How the attacks that hurt its body and leave it alive knock it back; none for a body they do not.
    std::optional<EntityKnockback> knockback{};
    /// How far its body walks on the ground between its steps (EntityStep), which the others hear; none
    /// for a body whose steps are not heard.
    std::optional<nx::HammerUnits> stride{};
};

/// How far the model of `definition` is drawn from its body's origin, the body ducked or not.
[[nodiscard]] inline auto modelOffsetOf(const EntityDefinition& definition, bool isDucked) noexcept
    -> nx::Vec3 {
    return (isDucked ? definition.crouchedModelOffset.value_or(definition.modelOffset)
                     : definition.modelOffset)
        .vector();
}

/// The sequences of a type's model its body plays by its EntityMotion, found by their names; ducked, those
/// it has for that, else the ones it plays standing.
struct EntitySequences {
    nx::assets::SequenceIndex idle{};
    nx::assets::SequenceIndex walk{};
    nx::assets::SequenceIndex attack{};
    nx::assets::SequenceIndex death{};
    nx::assets::SequenceIndex crouchIdle{};
    nx::assets::SequenceIndex crouchWalk{};
};

/// A type of the catalog: its definition, its model and its sequences, validated.
struct EntityType {
    EntityDefinition definition{};
    EntityModelIndex model{};
    EntitySequences sequences{};
};

/// Models stay owned here; indices are append-only and definitions cannot change under live bodies.
/// References returned by type/model are borrowed until the next add; retain indices across additions.
class EntityCatalog {
public:
    /// Validates stats, files, hitboxes and sequences before appending. Errors: InvalidDefinition,
    /// ModelUnreadable, UnknownSequence. A failed addition leaves existing entries unchanged.
    [[nodiscard]] auto add(const nx::assets::Vfs& vfs, EntityDefinition definition, ModelTextures textures)
        -> nx::Result<EntityTypeIndex, EntityError>;
    /// Requires a type index issued by this catalog.
    [[nodiscard]] auto type(EntityTypeIndex index) const -> const EntityType&;
    /// Requires a model index issued by this catalog.
    [[nodiscard]] auto model(EntityModelIndex index) const -> const AnimatedModel&;
    /// How many models the catalog holds.
    [[nodiscard]] inline auto modelCount() const noexcept -> std::size_t { return models.size(); }
    /// How many types the catalog holds.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return types.size(); }

private:
    std::vector<AnimatedModel> models;
    std::vector<EntityType> types;
};

/// A model entity, independent of players, hostages and the kind of agent driving it.
struct EntityState {
    nx::ecs::Entity entity{};
    EntityTypeIndex type{};
    nx::Vec3 origin{};
    nx::Vec3 velocity{};
    nx::EulerAngles angles{};
    Animation animation{};
    nx::HitPoints health{};
    nx::Vec3 movedFrom{};           ///< Where its last move started: it shows going from there to `origin`.
    nx::GameTime movedAt{};         ///< When its last move ended, as a snapshot tells the time.
    std::uint32_t ticksPerMove = 1; ///< How many ticks it shows going for: its next move comes then.
    bool isDucked = false;          ///< Its box is the ducked one, its origin lower over the same feet.
    bool isOnGround = false;        ///< Its body stands on the ground.
    bool isKnockedBack = false;     ///< As of its last move, a hit still held back its walk (walkShareOf).
};

/// A step an entity's body took in a tick, as it walked a stride on the ground: the body, and where its
/// feet were.
struct EntityStep {
    nx::ecs::Entity entity{};
    nx::Vec3 position{};
};

/// An entity as a client draws it: its type, where it is, its model's sequence at a frame, and how
/// opaque it is drawn, of 255 (a corpse fading out).
struct ShownEntity {
    EntityTypeIndex type{};
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    nx::assets::SequenceIndex sequence{};
    SequenceFrame frame{};
    std::uint8_t amount = std::numeric_limits<std::uint8_t>::max();
    bool isDucked = false; ///< As it died (modelOffsetOf).
};

} // namespace cs

template <>
struct nx::meta::Description<cs::EntityModelOffset> {
    static constexpr std::string_view kName = "cs::EntityModelOffset";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::EntityModelOffset::x>("x"),
        nx::meta::field<&cs::EntityModelOffset::y>("y"),
        nx::meta::field<&cs::EntityModelOffset::z>("z"),
    };
};

template <>
struct nx::meta::Description<cs::EntityDefinition> {
    static constexpr std::string_view kName = "cs::EntityDefinition";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::EntityDefinition::id>("id"),
        nx::meta::field<&cs::EntityDefinition::model>("model"),
        nx::meta::field<&cs::EntityDefinition::modelOffset>("modelOffset"),
        nx::meta::field<&cs::EntityDefinition::health>("health"),
        nx::meta::field<&cs::EntityDefinition::speed>("speed"),
        nx::meta::field<&cs::EntityDefinition::meleeDamage>("meleeDamage"),
        nx::meta::field<&cs::EntityDefinition::meleeReach>("meleeReach"),
        nx::meta::field<&cs::EntityDefinition::meleeInterval>("meleeInterval"),
        nx::meta::field<&cs::EntityDefinition::idleSequence>("idleSequence"),
        nx::meta::field<&cs::EntityDefinition::walkSequence>("walkSequence"),
        nx::meta::field<&cs::EntityDefinition::attackSequence>("attackSequence"),
        nx::meta::field<&cs::EntityDefinition::deathSequence>("deathSequence"),
        nx::meta::field<&cs::EntityDefinition::crouchIdleSequence>("crouchIdleSequence"),
        nx::meta::field<&cs::EntityDefinition::crouchWalkSequence>("crouchWalkSequence"),
        nx::meta::field<&cs::EntityDefinition::crouchedModelOffset>("crouchedModelOffset"),
        nx::meta::field<&cs::EntityDefinition::hitGroups>("hitGroups"),
        nx::meta::field<&cs::EntityDefinition::bloodColor>("bloodColor"),
        nx::meta::field<&cs::EntityDefinition::knockback>("knockback"),
        nx::meta::field<&cs::EntityDefinition::stride>("stride"),
    };
};
