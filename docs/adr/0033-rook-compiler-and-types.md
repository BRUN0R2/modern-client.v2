# ADR-0033 — A separate Rook compiler, and types that rule out what Pawn left to run time

- **Status:** Proposed
- **Date:** 2026-11-04

## Context

The maintainer asks for the new language to read like AMX Pawn, with typing that is exaggeratedly
strong, and for its compiler to be separate and decoupled: more robust, safer, modern and dynamic.
The VM stays the project's own ([ADR-0007](0007-own-vm-typed-bytecode.md)). `rook/` holds only a
README, so each point below costs a document today and would cost a rewrite later.

The documents leave four things open:

- **The compiler's boundary is not written down.** The roadmap says the compiler does not depend on
  the engine ([19](../19-roadmap.md)) and the toolchain is MIT
  ([ADR-0010](0010-license.md)), but a hot reload has the server compile
  ([13 §8](../13-mod-system.md#8-hot-reload)), `PluginLoadError` has a `CompileFailed`
  ([13 §13](../13-mod-system.md#13-typed-errors)), and the MIT tool `rook test` starts a server
  without saying how ([14 §21](../14-rook-language.md#21-tests)).
- **Whole classes of bug stay at run time.** [14](../14-rook-language.md) is already stronger than
  TypeScript, yet registering a class outside precache is an error the plugin returns as it runs
  ([14 §8.2](../14-rook-language.md#82-typed-errors)), `giveItem` takes any `Player`, a player's
  name is a plain `string` that can reach `serverCmd`, and a mistyped model path fails only at load.
  Each is a bug Pawn plugins have today.
- **The verifier's reach is not stated.** It checks "types, register bounds and jump targets"
  ([14 §24](../14-rook-language.md#24-vm-and-bytecode)). Code a client downloads never went through
  that player's compiler, so a rule the verifier does not check holds only for honest servers.
- **The dispatch cannot be built everywhere.** ADR-0007 and 14 §24 ask for *computed goto*, which
  MSVC, a validated toolchain ([17 §2.1](../17-build-and-tools.md#21-compiler-matrix)), does not
  have.

## Decision

Proposed: the compiler is a program of its own, tied to the engine by two versioned formats; the
bytecode keeps the types, so the verifier enforces them too; and new kinds of type turn those bugs
into compile errors, in Pawn's own spelling.

### 1. The compiler stands alone

1. **Two contracts, nothing else.** `rookc` reads `.rook` sources and `.rookd` declarations and
   writes `.rbc` bytecode. The engine generates the `.rookd` from its annotated C++
   ([15 §1](../15-mod-api.md#1-overview)) and loads the `.rbc`. Both formats carry a version. A
   plugin builds with the SDK alone: no engine sources, no game installation.
2. **No compiler in the server or the client.** They carry the verifier and the VM and parse no
   Rook source. `rook build --watch` writes the `.rbc`; `plugins reload` and `rook_watch 1` load and
   verify what is there, and `PluginLoadError.CompileFailed` goes away. A mod in development is built
   by `rook build`, a released one comes as a `.nxpak`, and the built-in mods of `mods/` are built
   with the game, `rookc` being a host tool as `slangc` is for shaders.
3. **`rook/` imports nothing of `engine/` or `game/`.** It is MIT, and linking GPL code would end
   that: it uses `std::expected` ([ADR-0005](0005-typed-errors-without-exceptions.md)) and not
   `nx::Result`. The dependency runs one way, `nx.script` importing the VM. `rook/` configures and
   builds on its own, and the layer check
   ([01 §1.1](../01-architecture.md#11-module-catalog)) refuses an edge into the engine. `rook test`
   and the debugger reach the engine as another program: the dedicated server, started or connected
   to.
4. **The targets:**

| Directory | Contents | Depends on |
|-----------|----------|------------|
| `rook/format` | The layout of `.rbc` and `.rookd`, their versions, readers and writers | `std` |
| `rook/compiler` | Library: syntax, semantics, RIR, bytecode ([14 §23](../14-rook-language.md#23-the-rookc-compiler)) | `format` |
| `rook/verifier` | The load-time check of §2 | `format` |
| `rook/vm` | Interpreter, collector, isolates, budgets | `format`, `verifier` |
| `rook/cli`, `rook/lsp` | `rook`, `rookc` and `rookLsp`: thin programs over the library | `compiler` |

5. **What the maintainer's four words mean here:**
   - **Robust:** malformed source always ends in a diagnostic, never a crash. The parser tolerates
     errors, `rookc` is fuzzed, and every diagnostic has a conformance test
     ([18](../18-testing-and-quality.md)).
   - **Safer:** the compiler treats its input as untrusted, a mod downloaded from anyone: no network,
     reads only under the mod and the declarations, `const fn` evaluated under a budget. Its output
     is not trusted either (§2).
   - **Modern:** the query-driven compiler of 14 §23, one base for the CLI and the LSP. The same
     inputs and compiler version give the same bytes, which `rook pack`'s reproducible `.nxpak`
     needs.
   - **Dynamic:** edit, save and see it in the running game, without a restart, `persist var` kept.
     The word describes the way of working: types stay static, and the only access by name at run
     time stays `getVarDynamic` ([14 §13.2](../14-rook-language.md#132-variable-catalogs-vars)).

### 2. The bytecode keeps the types

This amends ADR-0007.

- `.rbc` carries a table of its nominal types (units and other newtypes, structs, enums, handles
  with their hierarchy and whether they are ephemeral) and, for each function, its signature and its
  marks (`pure` and those of §3).
- The verifier checks every instruction against them at load: a `Health` is not used as an `Armor`;
  an ephemeral handle does not reach a global, a field, a component, a closure or the far side of an
  `await` ([14 §13.1](../14-rook-language.md#131-handles)); a `pure` function calls only `pure` ones;
  a function bound to a lifecycle stage is called only from it. A failure is
  `PluginLoadError.VerifyFailed`. Nothing of this runs per instruction: the interpreter still makes
  no dynamic type check.
- Some rules stay the compiler's alone, because their source is gone by then: exhaustive `match`
  (lowered with a branch that faults), `vars` keys (resolved to indices the engine bounds-checks),
  resource paths and translation keys.
- The verifier shares only `rook/format` with the compiler, so that a bug in the checker is not also
  a hole in the verifier, and it is fuzzed from its first commit. The interpreter relies on it.
- The engine still validates every handle on every call, whatever the bytecode proved.
- Server plugins come first. Bytecode from the network runs on a client only after the audit ADR-0007
  requires.
- Dispatch is one file's detail: *computed goto* on GCC and Clang, `switch` on MSVC. The bytecode
  and its results are the same on each, and a benchmark compares them
  ([RULES §8](../RULES.md#8-performance)).

### 3. Five kinds of type

Each is a compile error with a code and a fix. There is no `unsafe`, no cast, no pragma and no lint
level that turns one off. The explicit exits that already exist stay as they are: `.value()` for a
unit and `getVarDynamic` for admin tools.

**A lifecycle stage in the signature.** A function that only makes sense in one stage says so, and
`ClassError.RegistrationClosed` and `isPrecachePhase()` go away.

```rook
@onlyIn(Lifecycle.Precache)
pub fn createClass(id: ClassId, team: Team) -> Result<Class, ClassError> { ... }

forward pluginInit() -> Result<void, PluginError> {
    createClass(ASSASSIN, Team.Zombie)?;   // error R0701: `createClass` runs only in `pluginPrecache`
    return ok();
}
```

The mark is inferred inside a plugin and declared on a `pub` function. A function reference carries
it, so `setTask(2s, createLater)` does not compile either.

**A state proved by the check Pawn authors already write.** `AlivePlayer` is a handle under `Player`,
accepted wherever a `Player` is. The catalog declares what narrows to it
(`readonly isAlive: bool narrows AlivePlayer`), so no new spelling is needed.

```rook
fn onMenuChoice(player: Player) {
    giveItem(player, ItemId.Ak47);     // error R0702: `giveItem` needs a living player; check "isAlive" first
}

fn onBuy(player: Player) -> PluginResult {
    if !getPlayerVar(player, "isAlive") {
        return PluginResult.Handled;   // Pawn's `if (!is_user_alive(id)) return PLUGIN_HANDLED;`
    }
    giveItem(player, ItemId.Ak47);     // ok: from here on `player` is an `AlivePlayer`
    return PluginResult.Handled;
}
```

- A parameter or a `let` narrows; a `var` does not. Like any ephemeral handle it ends with the
  callback and at an `await`, and it is never stored: `refOf` gives a `PlayerRef`. Only the
  instruction that asks the engine makes one, which the verifier checks.
- It proves that the check was made and that nothing the plugin did since could undo it. A call that
  can end the state says so, `@ends(AlivePlayer)`, inferred inside a plugin and declared on a `pub`
  function; after it the variable is a `Player` again.
- The engine checks again on every call, because another plugin's hook can end the state during an
  action. The callback is then aborted with `PluginFault.StateEnded` and logged; being an order of
  events and not a bug, it does not count toward `rook_fault_policy`
  ([13 §6.4](../13-mod-system.md#64-budgets-and-failures)).

**Text from outside is its own type.** A player's name pasted into `server_cmd` can carry a second
command. A player's `name`, the text of `say`, a command's `string` argument, a client's message and
an HTTP response are `UntrustedText`, and an interpolation with an untrusted piece is untrusted.

```rook
let name = getPlayerVar(player, "name");            // UntrustedText
clientPrint(player, .Chat, "Hello, {name}!");       // ok: the engine draws it as text
serverCmd("kick {name}");                           // error R0703: text from a player cannot reach `serverCmd`
serverCmd(consoleCommand("kick", name)?);           // ok: quoted as one argument, or an error
```

Printing, comparing and parsing accept it. `serverCmd` takes a `ConsoleCommand`, and a SQLite
statement is a literal with parameters. It becomes a `string` only through a check that returns a
`Result` (`validateText(name, PATTERN)?`). Command arguments typed as `Player` or `Health` are parsed
values, not untrusted text.

**Resource paths are checked against the mod.** A literal takes the type the parameter asks for, as
a number literal does, and `rookc` looks for it in the mod's `assets/` and those of its dependencies,
with the kind its extension says.

```rook
let model = precacheModel("models/rezombie/zombie.mdl")?;   // ok
let other = precacheModel("models/rezombie/zombi.mdl")?;    // error R0704: no such file; did you mean "zombie.mdl"?
```

The compiler never reads the game's content ([ADR-0015](0015-free-content-and-optional-original-assets.md)),
so a mod names what it expects of it in `mod.toml` (`[resources] external = [...]`). The call still
returns a `Result`, because files can change after a build. A path built at run time is checked only
then.

**Units derive their operations.** One declaration replaces the entries listed by hand in
[14 §6](../14-rook-language.md#6-units-and-newtypes), and a mod's own units get them too.

```rook
newtype Speed(f32) suffix ups dimension(Distance / Duration);
```

This gives `Distance / Duration → Speed`, `Speed * Duration → Distance` and
`Distance / Speed → Duration`, the conversion from `Duration`'s microseconds included. Relations that
are not dimensions (`Health − Damage → Health`) stay declared one by one.

### Keeping Pawn's look

- The types weigh on signatures and on the API; function bodies infer. A plugin written with Pawn's
  habits names no `AlivePlayer`, `UntrustedText` or `@onlyIn` unless it exports a `pub` function.
- Diagnostics speak AMX Mod X's terms and carry the fix
  ([ADR-0021](0021-rook-amxx-names.md), decision 7): R0702 names `is_user_alive`, R0703 `server_cmd`.
- `rook migrate` writes the checks and `consoleCommand` where a converted plugin needs them.

### 4. The order

Infrastructure arrives with its first consumer ([RULES §5.3](../RULES.md#53-evolution)), so the five
kinds of type do not come at once: each enters with the API that first needs it.

1. `rook/format`, the parser, the conformance suite and the checker's core of
   [ADR-0006](0006-own-plugin-language-rook.md), with `rook check` and `rookLsp`: useful before
   anything runs.
2. The verifier, then the interpreter and a precise collector per isolate, made incremental as
   ADR-0007 asks once its pauses can be measured.
3. The first plugin on a server: `#include <amxmodx>`, `register_clcmd`, `client_print`.
4. The rest of the language and of §3: `vars`, ephemeral handles, hooks, tasks, coroutines, hot
   reload.
5. The proof: the native `mod.zombies` ([ADR-0028](0028-native-agent-test-mod.md)) rewritten in Rook
   and measured against itself before the ReZombie port.
6. Plugins on the client, after the audit.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| WebAssembly (Wasmtime) as the execution target | Native code generation and a runtime maintained by others; proposed on 2026-11-04 with a measured comparison. The maintainer keeps ADR-0007's own VM, and a Wasm module's validation would not check Rook's nominal types at load |
| The compiler inside the server, as 13 §8 writes it | A parser and a checker in every server, a compile failure as a run-time state, and a blurred line between MIT and GPL code |
| A repository of its own for the toolchain | `.rookd` changes with the API, so one change would span two pull requests and two versions. The build's layer check gives the same boundary |
| Types erased from the bytecode, the verifier checking only primitives and bounds | A simpler verifier, and still memory safe; but a hand-made `.rbc` could mix units or store an ephemeral handle, and the rules would hold only for honest compilers |
| The lifecycle stage as a token parameter (`precacheModel(precache, path)`) | No new rule, since an ephemeral handle does it; but the call no longer reads like Pawn's |
| Explicit narrowing only (`if let alive = asAlive(player)`) | Simpler for the checker; but `if (is_user_alive(id))`, the check authors already write, would prove nothing |
| These rules as lints | A lint can be turned off, and the request is for types |
| Range and refinement types, linear types, teams in a handle's type | Each is heavier for authors and for diagnostics, and none answers a bug named here. A concrete bug can bring one through its own ADR |
| Dynamic typing, for the word "dynamic" | Contradicts ADR-0006. What was asked of the compiler is served by incremental builds and hot reload |

## Consequences

- ✅ The engine and the compiler change apart, and a mod builds on any machine with the SDK alone.
- ✅ Servers and clients carry no parser or checker: less to attack, less to ship.
- ✅ The type rules hold for bytecode from a server too, not only for honest compilers.
- ✅ Forgotten `is_user_alive` checks, command injection through a name and mistyped paths stop
  compiling, in code that still reads like Pawn.
- ⚠️ A checker with flow-sensitive narrowing and marks over the call graph. Mitigation: the rules
  enter one at a time, each diagnostic with its conformance test.
- ⚠️ A larger verifier is a larger surface on the client. Mitigation: a target of its own, fuzzing
  from the first commit, nothing shared with the checker but the format, and the audit before any
  bytecode from the network runs.
- ⚠️ `.rookd`, `.rbc` and the marks become public contracts: adding `@ends` to a `pub` function
  breaks its callers. Mitigation: they enter the signature hash
  ([13 §6.3](../13-mod-system.md#63-calls-between-plugins)) and the CI's API check
  ([15 §10](../15-mod-api.md#10-versioning-and-stability)).
- ⚠️ `AlivePlayer` can go stale through another plugin's hook, and that callback is cut short.
  Mitigation: it is typed, logged and not held against the plugin; nothing unsafe can follow, since
  the engine checks every call.
- ⚠️ Development takes two programs, `rook build --watch` and the server. Mitigation: `rook_watch 1`
  makes the reload automatic.
- ⚠️ `rook/` keeps a small base of its own beside `nx.core`, which the license requires.
- ⚠️ ADR-0007's interpreter risk stands, and [ADR-0024](0024-agents.md) puts a mod's zombies and
  bosses in Rook. Mitigation: per-frame work stays in C++, and step 5 of the order measures it before
  anything is built on it.

If accepted, the same change amends ADR-0006, ADR-0007,
[13 §7, §8, §11 and §13](../13-mod-system.md), [14 §1, §2, §6, §8.2, §13, §15.3, §21, §23, §24 and
§25](../14-rook-language.md), [15 §1 and §4.2](../15-mod-api.md),
[16](../16-amxx-migration.md), [17 §1](../17-build-and-tools.md#1-repository-layout),
[19](../19-roadmap.md), `rook/README.md` and the M6 items of `TODO.md`.
