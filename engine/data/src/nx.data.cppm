// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.data — data and configuration files with schemas (docs/01-architecture.md, section 8).
///
/// A TOML file is read into a described struct (nx.meta): its keys are the struct's fields, and
/// an unknown key, a missing required key, a value of the wrong type or out of range is a typed
/// failure that names the file, line, column and key path. The TOML library stays behind the
/// Document views. A field of such a struct is set and written by its key path, as the console and
/// plugins change values made from data (:fields).
export module nx.data;

export import :document;
export import :schema;
export import :fields;
