// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.core — foundation of the Next Engine (docs/01-arquitetura.md, layer 0).
///
/// Typed failures, contract checks, strong types and units, explicit conversions, enum-indexed
/// containers, stable hashing, frame allocation and structured logging. Depends only on the C++
/// standard library.
export module nx.core;

export import :introspection;
export import :source;
export import :contracts;
export import :failure;
export import :conversion;
export import :quantity;
export import :units;
export import :enumArray;
export import :flags;
export import :hash;
export import :memory;
export import :log;
