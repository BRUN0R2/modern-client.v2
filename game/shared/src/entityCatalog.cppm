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

export namespace cs {

enum class EntityError : std::uint8_t {
    InvalidDefinition,
    ModelUnreadable,
    UnknownSequence,
    InvalidPlacement,
    UnknownEntity
};

struct EntityTypeIndex {
    std::uint32_t value = 0;
    [[nodiscard]] inline auto operator==(const EntityTypeIndex&) const noexcept -> bool = default;
};

struct EntityModelIndex {
    std::uint32_t value = 0;
    [[nodiscard]] inline auto operator==(const EntityModelIndex&) const noexcept -> bool = default;
};

struct EntityModelOffset {
    nx::HammerUnits x{};
    nx::HammerUnits y{};
    nx::HammerUnits z{};
    [[nodiscard]] inline auto vector() const noexcept -> nx::Vec3 {
        return {.x = x.value(), .y = y.value(), .z = z.value()};
    }
};

/// The first movement adapter uses the standing player hull. Models may have a different origin.
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
    HitGroupShares hitGroups{};
};

struct EntitySequences {
    nx::assets::SequenceIndex idle{};
    nx::assets::SequenceIndex walk{};
    nx::assets::SequenceIndex attack{};
    nx::assets::SequenceIndex death{};
};

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
    [[nodiscard]] inline auto modelCount() const noexcept -> std::size_t { return models.size(); }
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
    nx::EulerAngles angles{};
    Animation animation{};
    nx::HitPoints health{};
};

struct ShownEntity {
    EntityTypeIndex type{};
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    nx::assets::SequenceIndex sequence{};
    SequenceFrame frame{};
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
        nx::meta::field<&cs::EntityDefinition::hitGroups>("hitGroups"),
    };
};
