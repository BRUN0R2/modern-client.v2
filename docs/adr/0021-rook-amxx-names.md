# ADR-0021 — Rook accepts AMX Mod X's names and `get_*_var` through a compatibility layer

- **Status:** Accepted
- **Date:** 2026-10-23

## Context

Rook has to be as easy to pick up as Pawn for the people who write AMX Mod X plugins today
([ADR-0006](0006-own-plugin-language-rook.md)). Their hands know AMXX's spelling: `#include
<amxmodx>`, `new` for a variable, and natives such as `register_clcmd`, `client_print`,
`set_task` and `get_user_health`. The maintainer asks that Rook accept those literal names, so that
a plugin reads almost as it reads in Pawn, while keeping everything Rook adds: nominal types,
typed errors, no null, exhaustive `match`, units, structs and enums with data.

The same goes for the mods' own pattern: a plugin may write the variable accessors with
underscores, as ReAPI's and ReZombie's (`get_entvar`, `set_class_var`, `set_props_var`) are written:
`get_player_var`/`set_player_var`, `get_weapon_var`/`set_weapon_var`,
`get_entity_var`/`set_entity_var`.

Spelling is free to borrow: AMXX's names, its keywords and its plugin model are reused as an
interface, and nothing of its code is copied. AMX Mod X is GPL-3.0-or-later, Metamod-R GPL-3.0
and Pawn Apache 2.0; the Rook toolchain and SDK are MIT ([ADR-0010](0010-license.md)), so their code could
not be copied into it anyway. The pending legal review ([20](../20-legal-and-licensing.md))
covers the names too.

### What the four forms mean in Pawn and in Rook today

| Form | Pawn | Rook today |
|------|------|------------|
| `#include <amxmodx>` | The preprocessor pastes the header's text in | `import cs.prelude.*;`: modules, no preprocessor ([14 §19](../14-rook-language.md#19-modules-imports-and-visibility)); the incremental compiler and the LSP depend on that |
| `new x = 10;` | A mutable variable, an untyped cell unless tagged (`new Float:x`) | `var x = 10;` (mutable) or `let x = 10;` (immutable, the default); `new` is the conventional name of a constructor (`PlayerMap.new()`, `pub fn new(...) -> Self`, [14 §14](../14-rook-language.md#14-classes-and-interfaces)) |
| `register_clcmd("say /menu", "cmd_menu")` | The handler named by a string, looked up at run time | `registerClcmd("say /menu", cmdMenu)?`: the handler by reference, the failure a `Result` |
| `get_user_health(id)` | An integer from a reusable player index | `getPlayerVar(player, "health")`: a `Health` from a generational `Player` handle, the key checked at compile time ([ADR-0012](0012-typed-get-set-vars-pattern.md)), `health` an `Entity` key that a `Player`, an entity too, has ([15 §4](../15-mod-api.md#4-handles-and-var-catalogs)) |

### Conflicts with accepted decisions

The literal names contradicted, as they were written before this ADR:

- [ADR-0013](0013-camelcase-convention.md), [02 §3.1](../02-coding-standards.md#31-project-rule-camelcase)
  and [RULES §6.2](../RULES.md#62-names): camelCase for every function, variable and API name,
  `snake_case` only where imposed from outside. ADR-0013 itself records that the first Rook
  specification used AMXX's `snake_case` and that the maintainer replaced it with camelCase, to
  keep one convention in the project.
- [ADR-0006](0006-own-plugin-language-rook.md): "the AMXX lifecycle names written in camelCase".
- [ADR-0012](0012-typed-get-set-vars-pattern.md): `get*Var`/`set*Var` is **the main way** to reach
  a property, its names in camelCase (`getEntityVar`): `get_entity_var` is a second spelling of
  it, and `get_user_health` and its kind would be a second way to the same property.
- [14 §4](../14-rook-language.md#4-lexical-structure) and
  [§22](../14-rook-language.md#22-lints-and-magic-numbers): the reserved words do not include `new`
  and the `naming` lint is an error; [§19](../14-rook-language.md#19-modules-imports-and-visibility):
  imports, no preprocessor.
- [16](../16-amxx-migration.md): migration renames every AMXX name to its camelCase form.

A contradiction between documents is a defect that an ADR resolves before any implementation
([RULES §1](../RULES.md#1-supreme-rule)). This ADR states it, and the decision below amends those
documents in the same change.

## Decision

Rook accepts AMX Mod X's spelling **as a compatibility layer**, `amxx`, over the one canonical,
camelCase API, with every Rook guarantee kept.

1. **`#include <header>`** is accepted for the AMXX headers the layer provides (`amxmodx` first,
   then `amxmisc`, `fun`, `cstrike`, `fakemeta`, `hamsandwich` and `reapi` as their natives are
   covered), on a line of its own before the first declaration, and means
   `import amxx.<header>.*;`. There is no preprocessor and no textual inclusion: `#include
   "file"`, an unknown header and `#define` are errors whose fix rewrites them as an `import` or a
   `const`.
2. **`new`** is a contextual keyword at the start of a declaration: `new x = 10;` is exactly
   `var x = 10;`, mutable as in Pawn, with Rook's type syntax (`new speed: Speed = 250ups;`). A
   Pawn tag (`new Float:speed`) is an error whose fix writes the type. Everywhere else `new` stays
   a name, so constructors (`PlayerMap.new()`) are untouched.
3. **AMXX's natives that do what no catalog does, by their literal names,** live in the `amxx.*`
   modules of the SDK, written in Rook, each a one-line function over the canonical API:
   `register_clcmd` calls `registerClcmd`, `client_print` calls `clientPrint`, `set_task` calls
   `setTask`. They take and return Rook's types: a `Player` handle and not an index, units, a
   function reference and not a callback's name, a `Result` where the canonical function returns
   one. There is no second implementation, and the layer's names are listed in
   [16 §3](../16-amxx-migration.md#3-api) beside their canonical forms.

   A native that only reads or writes a property a `vars` catalog has is **not** in the layer: the
   property is reached through its handle's accessor, so that the API keeps one way to each
   property ([ADR-0012](0012-typed-get-set-vars-pattern.md)). A handle's accessor reaches the keys of
   the handles above it, a `Player`'s those of `Entity`
   ([15 §4](../15-mod-api.md#4-handles-and-var-catalogs)): `get_user_health(id)` is
   `get_player_var(player, "health")`, `set_user_health(id, 100)` is
   `set_player_var(player, "health", 100hp)`, `is_user_alive(id)` is
   `get_player_var(player, "isAlive")`, `cs_get_user_money(id)` is
   `get_player_var(player, "money")`. Calling one of those natives is an error whose fix writes the
   accessor, and `rook migrate` writes it too.
4. **The `vars` pattern with underscores.** For each handle with a `vars` catalog, the engine's
   (`Entity`, `Player`, `Weapon`, `WeaponClass`, the game rules) and those plugins declare
   (`Class`, `Props`...), the compiler generates `get_<handle>_var` and `set_<handle>_var` beside
   `get<Handle>Var` and `set<Handle>Var`, the handle's name in `snake_case`: `get_player_var` and
   `set_player_var`, `get_weapon_var` and `set_weapon_var`, `get_entity_var` and
   `set_entity_var`, `get_weapon_class_var`, `get_class_var`. Each is the same function, its
   literal key checked and its type inferred from the key in the same way
   ([ADR-0012](0012-typed-get-set-vars-pattern.md)), and every plugin may use them, whether or not
   it uses the rest of the layer.
5. **The exception to ADR-0013** is this layer: AMXX's API and the mods' accessor pattern are
   contracts imposed from outside, like cvar names and map classnames. Their names are allowed in
   the `amxx.*` modules, as the generated accessors, and in the plugins that use them. The
   project's own Rook code (built-in mods, examples, documentation, the API's tests) stays
   canonical; only the layer's conformance tests use it.
6. **Lints.** A new `compatName` lint (`info` by default) marks each AMXX name and each
   `get_<handle>_var`/`set_<handle>_var` with its canonical form, with an LSP quick fix and
   `rook fmt --canonical` to rewrite a file at once; the LSP offers the canonical form first. In a
   file that includes an AMXX header, `naming` is a warning instead of an error, so that a plugin
   carried over with its own `g_data`-style names compiles; everywhere else it stays an error.
7. **Diagnostics speak both:** a literal name used with Pawn's types is explained in AMXX's terms
   (`register_clcmd expects a function: the handler AMXX names by a string is the function itself
   in Rook`; `client_print expects a Player: AMXX's player index is a Player handle in Rook`).
8. **`rook migrate`** gains `--keep-amxx-names`: the converted plugin keeps the literal names,
   `new` and the `get_<handle>_var` spelling, changing only what typing requires, for the smallest
   diff; without it, it writes the canonical forms as today.

The `vars` keys stay as the catalogs declare them (`"health"`, `"maxClip"`), whichever spelling of
the accessor reaches them; accepting `"max_clip"` too would be a decision of its own. AMXX/ReAPI's
variable names (`var_health`, `m_iAccount`) are outside this ADR.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep ADR-0013 as it is: camelCase only, `rook migrate` and the table of [16](../16-amxx-migration.md) | One convention, nothing more to maintain; but the familiarity the maintainer asks for stops at the migration report, and a plugin written from memory does not compile |
| AMXX's spelling as Rook's only spelling (supersede ADR-0013 for Rook) | Two conventions in the project again, the case ADR-0013 was written against; `new` and `#include` with Pawn's meanings would bring the preprocessor and untyped declarations back |
| A Pawn front end: `rookc` compiles `.sma` sources | Pawn's untyped cells give the compiler nothing to check: most plugins would be refused, or run in a mode without Rook's guarantees, against ADR-0006 and [ADR-0007](0007-own-vm-typed-bytecode.md) |
| Tools only: the LSP completes `register_clcmd` as `registerClcmd`, and "did you mean" diagnostics | No change to the language and the most predictable; but the code is still written in the canonical spelling, which is not what is asked |
| Literal names mixed freely, without a layer or lints | No single place for them, no canonical form to converge on, and two names for every function in every listing |
| `get_player_var` and its kind as the `vars` pattern's only spelling (ADR-0012 and ADR-0013 amended for it) | The accessors would be the one part of the API in `snake_case` beside `registerClcmd` and `precacheModel`, unless the whole API changes with them, which is the "AMXX's spelling only" alternative |

## Consequences

- ✅ A plugin carried over from AMX Mod X keeps its spelling and compiles with the changes typing
  requires, and an author writes from memory.
- ✅ One implementation of each function: the layer is Rook code over the canonical API, with its
  types and errors; nothing of AMXX's code is used.
- ✅ Each literal name has a canonical form one quick fix away, so code can converge on it.
- ✅ One way to each property: no native of the layer duplicates a catalog's key, and the
  accessor that reaches it is the one a mod's author already writes (`get_player_var`).
- ⚠️ Two spellings of the same API, the accessors of every catalog included. Mitigation: the
  documentation, the examples and the project's own code use the canonical one, the LSP offers it
  first, and the layer is listed in one generated table.
- ⚠️ False familiarity: the names are AMXX's, the types are not (`register_clcmd` takes the
  handler and not its name, `client_print` a `Player` and not an index). Mitigation: diagnostics in
  AMXX's terms (decision 7), the fix that turns a property's native into its accessor, and the
  differences of [16 §5](../16-amxx-migration.md#5-important-behavior-differences).
- ⚠️ ADR-0013's single convention gets a scoped exception, and `naming` a softer default in
  files that include AMXX headers. Mitigation: the exception is the layer alone, and `compatName`
  points at every use.
- ⚠️ A contextual keyword and a restricted `#include` in the grammar. Mitigation: both covered by
  the conformance suite, and `new` never reserved, so no existing name breaks.
- ⚠️ The layer is API surface to keep: the CI's API compatibility check
  ([15 §10](../15-mod-api.md#10-versioning-and-stability)) covers the `amxx.*` modules as it covers
  the rest.

The same change amended ADR-0013 (the exception), ADR-0006, ADR-0012,
[02 §3.1](../02-coding-standards.md#31-project-rule-camelcase),
[14 §1, §4, §13.2, §19 and §22](../14-rook-language.md), [15 §1, §2, §4 and §10](../15-mod-api.md),
[16 §1, §2, §3, §6 and §7](../16-amxx-migration.md) and the M6 items of `TODO.md`.
