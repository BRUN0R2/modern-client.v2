# ADR-0034 — Rook keeps nothing for old plugins: one modern spelling, typed to the maximum

- **Status:** Proposed
- **Date:** 2026-11-04

## Context

The maintainer decided that the language owes no compatibility to old plugins: it must be modern
and typed to the maximum, and ReZombie will be rewritten from scratch in Rook, not ported. Familiarity
was the reason behind several decisions, and it no longer holds:

- [ADR-0021](0021-rook-amxx-names.md) gave Rook a second spelling, the `amxx` layer: `#include`,
  `new`, AMX Mod X's natives and `get_player_var`, with an exception to
  [ADR-0013](0013-camelcase-convention.md), a softer `naming` lint and a `compatibilityName` lint.
- [ADR-0012](0012-typed-get-set-vars-pattern.md) chose `getPlayerVar(player, "health")` over fields
  because fields are "further from what the community knows", and recorded that "the type checking
  would be equivalent".
- [16](../16-amxx-migration.md) and `rook migrate` exist to carry plugins over.
- The API inherited names that are cut (`registerClcmd`, `pluginCfg`), engine jargon
  (`clientPutinserver`) or misspelled (`Supercede`).
- A unit's suffix on a field that already has that type repeats it: `player.health = 100hp`.

Asked directly, the maintainer chose one modern spelling, fields over accessors, and no suffix where
the type already says it.

## Decision

Proposed: Rook has one spelling, chosen because it is modern and true, with nothing kept for plugins
that came before it. Decisions 1 to 3 and 6 are the maintainer's; 4 and 5 follow from them and are
proposed for review.

```rook
import cs.prelude.*;

plugin { name: "Hello", version: "1.0.0", author: "Modern-Client.v2" }

on pluginInit() -> Result<void, PluginError> {
    registerClientCommand("say /hello", cmdHello)?;
    return ok();
}

fn cmdHello(player: Player) -> PluginResult {
    if !player.isAlive {
        return PluginResult.Handled;
    }
    player.health = 100;
    printToPlayer(player, PrintTarget.Chat, "Hello, {player.name}!");
    return PluginResult.Handled;
}
```

### 1. One spelling

This supersedes ADR-0021. These go away: the `amxx.*` modules, `#include`, `new`, the underscored
accessors, the `compatibilityName` lint, the softer `naming` in some files, `rook fmt --canonical`,
`rook migrate` with `--keep-amxx-names`, and [16](../16-amxx-migration.md). Declarations are `let`
and `var`, and ADR-0013 holds without an exception.

What stays is what works, not what is familiar: a C-like syntax with semicolons, plugins and mods,
a lifecycle, commands, cvars and tasks. The vocabulary table of
[13 §2](../13-mod-system.md#2-vocabulary-and-equivalents) remains a reading guide for people who know
AMX Mod X, and promises nothing.

### 2. Properties are fields

This supersedes ADR-0012.

```rook
let health = player.health;       // Health
player.health = 100;
player.helth = 100;               // error R0312: no field `helth` on `Player`; did you mean `health`?
player.health = player.armor;     // error R0201: expected `Health`, found `Armor`
player.isAlive = true;            // error R0313: `isAlive` is readonly
```

- A `vars` catalog stays the declaration of a handle's fields: type, default, `readonly` and
  documentation. No accessor is generated, no key is a string, and a field compiles to an index.
- A handle has the fields of those above it ([15 §4](../15-mod-api.md#4-handles-and-var-catalogs)).
  Its own field hides one of the same name: `weapon.owner` is the `Weapon`'s `Player?`, and through
  `let entity: Entity = weapon;`, `entity.owner` is the `Entity`'s `EntityRef?`.
- **A field write only stores.** Whatever applies rules or raises an event is a named action and the
  field is `readonly`, as `team` and `changeTeam` are today: `player.money` becomes `readonly`, changed
  by `setMoney(player, 800, reason)?`. No write hides a flow ([RULES §2](../RULES.md#2-philosophy)),
  and none can end a checked state ([ADR-0033](0033-rook-compiler-and-types.md)).
- `getVarDynamic` and `setVarDynamic` stay, for admin and debugging tools.

### 3. A literal takes the type its place asks for

```rook
player.health = 100;            // ok: `Health`
player.health = 99.5;           // ok
player.maxSpeed = 250;          // ok: `Speed`
setMoney(player, 800, reason)?; // ok: `Money`, the same rule at a parameter
weapon.accuracy = 1;            // ok: `f32`
player.frags = 1.5;             // error R0204: expected `i32`, found a fraction

setTask(3, onTimer);            // error R0205: `Duration` has several units; write `3s` or `3ms`
setTask(3s, onTimer);           // ok
player.fieldOfView = 90deg;     // ok

let amount = 100;               // i32
player.health = amount;         // error R0201: expected `Health`, found `i32`
let bonus = Health(25);         // nothing asks for a type here, so it is named
let limit: Health = 5000;       // ok
```

- **Only a literal adapts, and only while compiling.** A value that has a type keeps it, so
  conversions stay never implicit ([14 §5](../14-rook-language.md#5-primitive-types-and-conversions)).
  In an operation, the literal takes the type the operation asks of it (`player.health + 50`).
- **A suffix exists only where a number alone cannot say which unit it means:** `Duration` (`ms`, `s`,
  `min`) and `Angle` (`deg`, `rad`). `hp`, `ap`, `dmg`, `ups`, `hu` and `x` go away.
- A literal in a place typed as a unit counts as having one for the `magicNumber` lint, which is what
  `25hp` was: nothing gets looser. A plain number in a place typed `i32` or `f32` stays under the lint.

### 4. Stricter defaults

No carried plugin has to compile any more, so these go from warnings to errors:
`expectInGameplay` and `wildcardHidesVariants`; `naming` is an error everywhere.

A newtype may declare a range. A literal is checked while compiling, a value known only at run time
goes through `tryFrom`, and a cvar takes its limits from its type.

```rook
newtype Volume(f32) range(0..=1);

sound.volume = 1.5;                    // error R0206: `Volume` goes from 0 to 1
let volume = Volume.tryFrom(value)?;   // Result<Volume, ConversionError>
```

### 5. Names

A name stays if it is true today, written in full, and consistent with the rest of the API, however
old it is. Being familiar is no longer a reason. These stay: `plugin`, `pluginInit`, `pluginEnd`,
`pluginPause`, `pluginUnpause`, `setTask`, `removeTask`, `createCvar`, `playerSpawned`, `playerKilled`,
`roundStart`. Keywords and primitive types (`fn`, `pub`, `i32`) are not names in this sense.

| Today | Proposed | Why |
|-------|----------|-----|
| `forward pluginInit()` | `on pluginInit()` | Everywhere else `forward` declares something defined later; this handles an event |
| `pub forward roundStart(...)`, `executeForward(roundStart, ...)` | `pub event roundStart(...)`, `emit roundStart(...)` | The same, and a typed statement in place of a function that takes the event as a value |
| `pluginPrecache` | `pluginLoad` | Precache is the old mechanism's word; this loads resources and registers content |
| `precacheModel`, `precacheSound`, `precacheSprite`, `precacheGeneric` | `loadModel`, `loadSound`, `loadSprite`, `loadFile` | What they do, and the naming example of [02 §3.2](../02-coding-standards.md#32-other-naming-rules) |
| `pluginCfg` | `pluginConfigured` | Cut |
| `clientConnect`, `clientAuthorized`, `clientDisconnected` | `playerConnecting`, `playerAuthorized`, `playerDisconnected` | They receive a `Player`, and the rest of the API says `player` |
| `clientPutinserver`, `clientInfochanged` | `playerEntered`, `playerInformationChanged` | Engine jargon |
| `registerClcmd`, `registerConcmd` | `registerClientCommand`, `registerConsoleCommand` | Cut |
| `serverCmd` | `runServerCommand` | Cut |
| `clientPrint`, `clientPrintColor` | `printToPlayer`, `printColorToPlayer` | They take a `Player` |
| `HookResult.Supercede`, `HookResult.Break` | `HookResult.Block`, `HookResult.BlockAndStop` | A misspelling of *supersede*, and what each one does |
| `tr` | `translate` | Cut |
| `kvOpen`, `kvGet`, `kvSet` | `openStore`, `readStore`, `writeStore` | Cut |
| `logWarn` | `logWarning` | Cut |
| `hasNvg`, `scoreCt`, `renderFx`, `fov`, `mins`, `maxs` | `hasNightVision`, `scoreCounterTerrorist`, `renderEffect`, `fieldOfView`, `minimumBounds`, `maximumBounds` | Cut |
| `rook fmt`, `rook doc` | `rook format`, `rook document` | Cut |

### 6. ReZombie is written from scratch

ReZombie is the reference mod, written in Rook on this API and designed anew. The items that port it
become items that write it, and the converted examples of 16 go with that document. The proof of
ADR-0033's order is unchanged: the native `mod.zombies` rewritten in Rook and measured first.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| The `amxx` layer as an option | Two spellings of the API to keep, document and check for good, for plugins that are not coming over |
| Keeping `new Type:name` | Pawn's most recognizable trait, but mutable by default and with the type where `let x: T` does not put it. The maintainer chose `let` and `var` |
| Keeping `get*Var`/`set*Var` | As safe as fields, but strings in the code and a special rule in the compiler. The maintainer chose fields |
| Fields whose writes run rules (`player.money = 800` raising `moneyChanged`) | Shorter, but a hidden flow that looks like storing a value |
| The suffix always required (`100hp`) | One rule for every unit and the unit in plain sight, but it repeats what the field's type says. The maintainer ruled it out |
| The suffix optional | Two ways to write the same value |
| A default unit for `Duration` and `Angle` (`setTask(3, ...)` in seconds) | Shorter, but a reader has to know the default, and `3` would read differently from `3ms` |
| Keeping `rook migrate` for the community | Most of a converter's work is guessing types from Pawn's untyped cells, its output needs rewriting by hand, and it would tie the language to Pawn's shapes |
| Keeping `pluginPrecache` and `precacheModel` | True today and known to modders; but with familiarity gone, `load` says the same in plain words |
| Keeping `forward` | Short and known to modders; but it means something else to everyone else |

## Consequences

- ✅ One way to write each thing: a smaller grammar, compiler, API surface and documentation.
- ✅ No property is reached through a string, and no special rule for literal keys remains.
- ✅ No hidden flow behind a field write.
- ✅ ADR-0013 holds without exceptions, and less of AMX Mod X's interface is borrowed, which narrows
  the pending legal review ([20](../20-legal-and-licensing.md)).
- ⚠️ No existing plugin comes over, so the ecosystem starts empty. Mitigation: the built-in mods
  ([13 §12](../13-mod-system.md#12-built-in-mods-replacements-for-the-default-amxx-plugins)) and
  ReZombie come first, and serve as examples.
- ⚠️ People who know AMX Mod X have more to learn. Mitigation: the concepts are the same, 13 §2 maps
  them, and the diagnostics give the fix.
- ⚠️ `player.maxSpeed = 250` does not show its unit on the line. Mitigation: the field's type is one
  hover away, and mixing up units stays an error.
- ⚠️ A literal's type depends on its place, which is one more rule for the checker. Mitigation: the
  conformance suite covers each place a literal can stand.
- ⚠️ Properties with rules lose the short form. Mitigation: their action names what happens.

If accepted, the same change marks ADR-0012 and ADR-0021 as superseded by this ADR; amends
[ADR-0006](0006-own-plugin-language-rook.md), ADR-0013, [ADR-0022](0022-plugin-api-primitives.md),
[ADR-0024](0024-agents.md), [ADR-0028](0028-native-agent-test-mod.md) and ADR-0033; removes
[16](../16-amxx-migration.md); and amends [00](../00-overview.md),
[02 §3.1](../02-coding-standards.md#31-project-rule-camelcase), [13](../13-mod-system.md),
[14](../14-rook-language.md), [15](../15-mod-api.md), [17](../17-build-and-tools.md),
[19](../19-roadmap.md), [20](../20-legal-and-licensing.md), [docs/README](../README.md) and the M6
items of `TODO.md`.
