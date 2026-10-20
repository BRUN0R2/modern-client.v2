// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;

namespace nx::ecs {

Commands::Commands(Commands&& other) noexcept : m_world{other.m_world} {
    swap(other); // `other` takes this empty buffer
}

auto Commands::operator=(Commands&& other) noexcept -> Commands& {
    Commands taken{std::move(other)};
    swap(taken); // `taken` leaves with this buffer's commands and destroys them
    return *this;
}

Commands::~Commands() {
    destroyPayloads();
}

void Commands::destroy(Entity entity) {
    DestroyCommand* run = lastCommandAs<DestroyCommand>();
    if (run == nullptr) {
        run = &std::get<DestroyCommand>(m_commands.emplace_back(
            DestroyCommand{.first = narrow<std::uint32_t>(m_destroyed.size()), .count = 0}));
    }
    ++run->count;
    m_destroyed.push_back(entity);
}

auto Commands::apply(World& world) -> CommandStats {
    expects(&world == m_world, "Commands::apply: the buffer belongs to another world");
    CommandStats stats;
    m_resolved.assign(m_pendingCount, Entity{});
    for (Command& command : m_commands) {
        std::visit([this, &world, &stats](auto& recorded) { applyCommand(world, recorded, stats); }, command);
    }
    clear();
    return stats;
}

auto Commands::resolve(PendingEntity entity) const noexcept -> std::optional<Entity> {
    if (entity.index() >= m_resolved.size()) {
        return std::nullopt;
    }
    return m_resolved[entity.index()];
}

auto Commands::recordSpawn(std::span<const RecordedComponent> components) -> PendingEntity {
    const PendingEntity entity{m_pendingCount++};
    // Spawns get consecutive pending entities, so one with the previous spawn's types joins its run.
    SpawnCommand* run = lastCommandAs<SpawnCommand>();
    if (run == nullptr || !std::ranges::equal(typesOf(*run), components, std::equal_to{}, std::identity{},
                                              &RecordedComponent::type)) {
        run = &std::get<SpawnCommand>(m_commands.emplace_back(SpawnCommand{
            .first = entity,
            .count = 0,
            .firstType = narrow<std::uint32_t>(m_spawnTypes.size()),
            .typeCount = narrow<std::uint32_t>(components.size()),
            .firstSource = narrow<std::uint32_t>(m_spawnSources.size()),
        }));
        for (const RecordedComponent& component : components) {
            m_spawnTypes.push_back(component.type);
        }
    }
    ++run->count;
    for (const RecordedComponent& component : components) {
        m_spawnSources.push_back(component.source);
    }
    return entity;
}

void Commands::recordRemove(Target target, std::optional<ComponentTypeId> type) {
    if (type) {
        m_commands.emplace_back(RemoveCommand{.target = target, .type = *type});
    }
}

auto Commands::pendingTarget(PendingEntity entity) const noexcept -> Target {
    expects(entity.index() < m_pendingCount,
            "Commands: the pending entity comes from another buffer or apply()");
    return Target{entity};
}

auto Commands::allocateBeyondPage(std::size_t size, std::size_t alignment) -> void* {
    if (size > kCommandPageBytes) {
        const std::size_t pages = divideRoundingUp(size, kCommandPageBytes);
        m_largePayloads.push_back(std::make_unique_for_overwrite<Page[]>(pages));
        return m_largePayloads.back()[0].bytes.data();
    }
    if (m_pages.empty()) {
        m_pages.push_back(std::make_unique_for_overwrite<Page>());
    }
    std::size_t offset = alignUp(m_pageUsed, alignment);
    if (offset + size > kCommandPageBytes) {
        ++m_pageIndex;
        if (m_pageIndex == m_pages.size()) {
            m_pages.push_back(std::make_unique_for_overwrite<Page>());
        }
        offset = 0;
    }
    m_pageUsed = offset + size;
    return m_pages[m_pageIndex]->bytes.data() + offset;
}

auto Commands::typesOf(const SpawnCommand& command) const noexcept -> std::span<const ComponentTypeId> {
    return std::span{m_spawnTypes}.subspan(command.firstType, command.typeCount);
}

auto Commands::sourcesOf(const SpawnCommand& command) noexcept -> std::span<void*> {
    return std::span{m_spawnSources}.subspan(command.firstSource,
                                             std::size_t{command.count} * command.typeCount);
}

auto Commands::entitiesOf(const DestroyCommand& command) const noexcept -> std::span<const Entity> {
    return std::span{m_destroyed}.subspan(command.first, command.count);
}

auto Commands::resolveTarget(const Target& target) const noexcept -> Entity {
    if (const PendingEntity* const pending = std::get_if<PendingEntity>(&target)) {
        return m_resolved[pending->index()];
    }
    return std::get<Entity>(target);
}

void Commands::applyCommand(World& world, const SpawnCommand& command, CommandStats& stats) {
    world.spawnRelocated(typesOf(command), sourcesOf(command),
                         std::span{m_resolved}.subspan(command.first.index(), command.count));
    stats.applied += command.count;
}

void Commands::applyCommand(World& world, const DestroyCommand& command, CommandStats& stats) const {
    for (const Entity entity : entitiesOf(command)) {
        if (!world.isAlive(entity)) {
            ++stats.skipped;
            continue;
        }
        world.destroy(entity);
        ++stats.applied;
    }
}

void Commands::applyCommand(World& world, AddCommand& command, CommandStats& stats) const {
    const Entity target = resolveTarget(command.target);
    if (!world.isAlive(target)) {
        destroyComponent(command.type, std::exchange(command.payload, nullptr));
        ++stats.skipped;
        return;
    }
    world.insertRelocated(target, command.type, std::exchange(command.payload, nullptr));
    ++stats.applied;
}

void Commands::applyCommand(World& world, const RemoveCommand& command, CommandStats& stats) const {
    const Entity target = resolveTarget(command.target);
    if (!world.isAlive(target)) {
        ++stats.skipped;
        return;
    }
    world.removeComponent(target, command.type);
    ++stats.applied;
}

void Commands::destroyComponent(ComponentTypeId type, void* component) const noexcept {
    if (component == nullptr) {
        return;
    }
    if (const auto destroy = m_world->components().info(type).destroy; destroy != nullptr) {
        destroy(component);
    }
}

void Commands::destroyPayloads() noexcept {
    for (const Command& command : m_commands) {
        if (const AddCommand* const add = std::get_if<AddCommand>(&command)) {
            destroyComponent(add->type, add->payload);
        } else if (const SpawnCommand* const spawn = std::get_if<SpawnCommand>(&command)) {
            const auto types = typesOf(*spawn);
            const auto sources = sourcesOf(*spawn);
            for (std::size_t index = 0; index < sources.size(); ++index) {
                destroyComponent(types[index % types.size()], sources[index]);
            }
        }
    }
}

void Commands::clear() noexcept {
    m_commands.clear();
    m_spawnTypes.clear();
    m_spawnSources.clear();
    m_destroyed.clear();
    m_largePayloads.clear();
    // Pages are kept for the next run.
    m_pageIndex = 0;
    m_pageUsed = 0;
    m_pendingCount = 0;
}

void Commands::swap(Commands& other) noexcept {
    std::ranges::swap(m_world, other.m_world);
    std::ranges::swap(m_commands, other.m_commands);
    std::ranges::swap(m_spawnTypes, other.m_spawnTypes);
    std::ranges::swap(m_spawnSources, other.m_spawnSources);
    std::ranges::swap(m_destroyed, other.m_destroyed);
    std::ranges::swap(m_pages, other.m_pages);
    std::ranges::swap(m_largePayloads, other.m_largePayloads);
    std::ranges::swap(m_pageIndex, other.m_pageIndex);
    std::ranges::swap(m_pageUsed, other.m_pageUsed);
    std::ranges::swap(m_pendingCount, other.m_pendingCount);
    std::ranges::swap(m_resolved, other.m_resolved);
}

} // namespace nx::ecs
