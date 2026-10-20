# ADR-0012 — `get*Var`/`set*Var` pattern with compile-time checked keys

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The community knows and likes the variable get/set pattern of ReAPI and ReZombie
(`get_entvar`, `set_member`, `set_class_var(class, "name", ...)`). In Pawn, however, the key is a free string: a typo or a wrong type only
shows up at runtime, if at all.

## Decision

- The `get<Handle>Var` / `set<Handle>Var` pattern (`getEntityVar`, `setWeaponVar`,
  `setClassVar`) is the **main way** to access properties in the Rook API. The names, and the
  keys too (`"maxClip"`), follow the project's camelCase rule
  ([ADR-0013](0013-camelcase-convention.md)).
- Each handle has a declared **`vars` catalog** (by the engine or by API plugins). The
  compiler generates the functions and **checks at compile time** that the literal key
  exists, that the value has the right type (and unit) and that the variable is not
  `readonly` ([14 §13.2](../14-rook-language.md#132-variable-catalogs-vars)).
- The return type of `get*Var` is inferred from the key.
- Dynamic access by string exists only in separate functions, which return
  `Result<VarValue, VarError>`.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Fields/methods (`props.health = ...`) | Further from what the community knows. The type checking would be equivalent |
| Key as an enum (`setPropsVar(props, PropsVar.Health, ...)`) | More verbose and less familiar. The checked literal key gives the same safety |
| Unchecked strings (as in Pawn) | Violates the strong typing requirement |

## Consequences

- ✅ Code practically identical to today's plugins, with errors caught at compile time.
- ✅ Key autocompletion in the LSP.
- ⚠️ The compiler has to treat literal keys as part of the signature. This is covered by
  conformance tests.
