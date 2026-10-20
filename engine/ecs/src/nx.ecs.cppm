// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.ecs — the Entity Component System (docs/03-ecs.md, ADR-0002; layer 2 in
/// docs/01-architecture.md).
///
/// Generational entity handles, components stored by archetype in 16 KiB SoA chunks, change
/// versions per chunk and column, typed queries, resources, events, command buffers, and a
/// scheduler that runs systems in parallel when their declared access allows it.
export module nx.ecs;

export import :entity;
export import :component;
export import :archetype;
export import :event;
export import :world;
export import :access;
export import :query;
export import :commands;
export import :system;
export import :schedule;
