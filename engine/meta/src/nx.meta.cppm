// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.meta — static reflection facade (docs/01-architecture.md, layer 1; docs/02-coding-standards.md,
/// section 1.2).
///
/// Types that the engine reflects (components, data files, network messages, plugin bindings) carry
/// an explicit Description: their declared name, their fields and the annotations of both. Callers
/// read it through qualifiedName, typeAnnotation and forEachField. Where the compiler implements
/// C++26 reflection (P2996), every description is checked against the type itself at compile time,
/// so a description cannot drift from the struct it describes.
export module nx.meta;

export import :description;
export import :native;
export import :reflection;
