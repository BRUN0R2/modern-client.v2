/// nx.core — foundation of the Next Engine (docs/01-arquitetura.md, layer 0).
///
/// Typed failures, contract checks, strong types and units, explicit conversions and
/// enum-indexed containers. Depends only on the C++ standard library.
export module nx.core;

export import :introspection;
export import :contracts;
export import :failure;
export import :conversion;
export import :quantity;
export import :units;
export import :enumArray;
export import :flags;
