# ADR-0006 — Own plugin language (Rook) with its own compiler

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The CS 1.6 ecosystem lives on AMX Mod X plugins in Pawn: untyped cells, weak tags, no
structs, strings in buffers, natives by offsets and callbacks by string. The request is a
**new** language, easy for people coming from AMXX, with typing **stronger than
TypeScript**, keeping the plugin model and the `get*Var`/`set*Var` pattern, and with its own
modern compiler.

## Decision

Create the **Rook** language (working name) and the **`rookc`** compiler
([14](../14-rook-language.md)):

- Familiar C-like syntax, with the AMXX lifecycle names written in camelCase
  (`pluginPrecache`, `pluginInit`, `forward`), like the whole project ([ADR-0013](0013-camelcase-convention.md)).
- Nominal types, no `any` and no null, `Result` with `error enum`, exhaustive `match`, units
  of measure, ephemeral handles checked at compile time and `vars` catalogs with checked
  literal keys.
- Plugins as the loadable unit and mods as the package ([13](../13-mod-system.md)).
- Incremental, query-driven compiler, shared by the CLI and the LSP.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep Pawn (with improvements) | The untyped cell is the underlying problem. Fixing it would mean creating another language |
| Lua / Luau | Gradual, optional typing, too weak for the requirement |
| TypeScript (through a JS engine) | The type system is structural and intentionally not *sound*, and the runtime is heavy |
| C# (Mono/.NET) | Large runtime, global GC, hard sandbox for client plugins |
| AngelScript | Better typing than Lua, but no units, `Result` or exhaustiveness, and dated syntax and tools |
| C++ plugins (DLL) | No sandbox, crashes the server on any error and prevents safe client plugins |

## Consequences

- ✅ Natural migration for the community and errors caught at compile time.
- ✅ Full control over diagnostics, tools and evolution.
- ⚠️ Large project (compiler, VM, LSP, debugger). Mitigation: a parallel track (M6) from
  early on, a specification and a conformance suite.
- ⚠️ The name "Rook" is provisional, to be confirmed before the release.
