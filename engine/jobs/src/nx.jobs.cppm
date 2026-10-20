// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.jobs — the Job System (docs/04-job-system.md, layer 1 in docs/01-architecture.md).
///
/// Workers with work-stealing deques, priorities, dependencies through atomic counters, wait() that
/// helps instead of blocking, and parallelFor. The lock-free building blocks it is made of are
/// exported too, so they can be tested and reused on their own.
export module nx.jobs;

export import :workStealingDeque;
export import :concurrentPool;
export import :intrusiveQueue;
export import :job;
export import :jobSystem;
