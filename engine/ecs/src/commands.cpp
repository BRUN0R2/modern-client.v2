// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;

namespace nx::ecs {

namespace {

[[nodiscard]] constexpr auto alignUp(std::size_t value, std::size_t alignment) noexcept -> std::size_t {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

Commands::Commands(Commands&& other) noexcept
    : m_world{other.m_world}, m_commands{std::exchange(other.m_commands, {})},
      m_pages{std::move(other.m_pages)}, m_largePayloads{std::move(other.m_largePayloads)},
      m_pageIndex{std::exchange(other.m_pageIndex, 0)}, m_pageUsed{std::exchange(other.m_pageUsed, 0)},
      m_pendingCount{std::exchange(other.m_pendingCount, 0)}, m_resolved{std::move(other.m_resolved)} {}

auto Commands::operator=(Commands&& other) noexcept -> Commands& {
    if (this != &other) {
        destroyPayloads();
        m_world = other.m_world;
        m_commands = std::exchange(other.m_commands, {});
        m_pages = std::move(other.m_pages);
        m_largePayloads = std::move(other.m_largePayloads);
        m_pageIndex = std::exchange(other.m_pageIndex, 0);
        m_pageUsed = std::exchange(other.m_pageUsed, 0);
        m_pendingCount = std::exchange(other.m_pendingCount, 0);
        m_resolved = std::move(other.m_resolved);
    }
    return *this;
}

Commands::~Commands() {
    destroyPayloads();
}

auto Commands::spawn() -> PendingEntity {
    const PendingEntity entity{m_pendingCount++};
    m_commands.push_back(
        Command{.kind = Kind::Spawn, .target = Target{.pending = entity.index(), .isPending = true}});
    return entity;
}

void Commands::destroy(Entity entity) {
    m_commands.push_back(Command{.kind = Kind::Destroy, .target = Target{.entity = entity}});
}

auto Commands::apply(World& world) -> CommandStats {
    expects(&world == m_world, "Commands::apply: the buffer belongs to another world");
    CommandStats stats;
    m_resolved.assign(m_pendingCount, Entity{});
    for (Command& command : m_commands) {
        if (command.kind == Kind::Spawn) {
            m_resolved[command.target.pending] = world.spawn();
            ++stats.applied;
            continue;
        }
        const Entity target = resolveTarget(command.target);
        if (!world.isAlive(target)) {
            ++stats.skipped;
            continue; // an Add keeps its payload, which clear() destroys
        }
        switch (command.kind) {
        case Kind::Destroy:
            world.destroy(target);
            break;
        case Kind::Add:
            world.insertRelocated(target, command.type, command.payload);
            command.payload = nullptr; // moved into the world
            break;
        case Kind::Remove:
            world.removeComponent(target, command.type);
            break;
        case Kind::Spawn:
            break;
        }
        ++stats.applied;
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

auto Commands::pendingTarget(PendingEntity entity) const noexcept -> Target {
    expects(entity.index() < m_pendingCount,
            "Commands: the pending entity comes from another buffer or apply()");
    return Target{.pending = entity.index(), .isPending = true};
}

auto Commands::allocate(std::size_t size, std::size_t alignment) -> void* {
    if (size > kCommandPageBytes) {
        const std::size_t pages =
            (size / kCommandPageBytes) + (size % kCommandPageBytes != 0 ? std::size_t{1} : std::size_t{0});
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

void Commands::recordRemove(Target target, std::optional<ComponentTypeId> type) {
    if (type) {
        m_commands.push_back(Command{.kind = Kind::Remove, .target = target, .type = *type});
    }
}

auto Commands::resolveTarget(const Target& target) const noexcept -> Entity {
    return target.isPending ? m_resolved[target.pending] : target.entity;
}

void Commands::destroyPayloads() noexcept {
    for (const Command& command : m_commands) {
        if (command.kind == Kind::Add && command.payload != nullptr) {
            if (const auto destroy = m_world->components().info(command.type).destroy; destroy != nullptr) {
                destroy(command.payload);
            }
        }
    }
}

void Commands::clear() noexcept {
    destroyPayloads();
    m_commands.clear();
    m_largePayloads.clear();
    // Pages are kept for the next run.
    m_pageIndex = 0;
    m_pageUsed = 0;
    m_pendingCount = 0;
}

} // namespace nx::ecs
