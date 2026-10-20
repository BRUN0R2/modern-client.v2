# 13 — Plugin and mod system

> Decisions recorded in [ADR-0006](adr/0006-own-plugin-language-rook.md) and
> [ADR-0007](adr/0007-own-vm-typed-bytecode.md).

## 1. Principle

The model the CS 1.6 community knows stays the same: **the server loads
plugins**, plugins talk through **natives and forwards**, and sets of plugins form a
**mod**. What changes is the technology underneath: a strongly typed language, a modern
compiler, per-plugin isolation, a native engine API (no fakemeta, offsets or Orpheu),
hot reload and plugins on the client.

### 1.1 Plugin licensing

Plugins and mods that use only the plugin interface (Rook bytecode, manifests,
`.rookd` declarations and the `std` library) may use **any license**, including
closed-source ones. The engine is GPL-3.0-or-later with the
[Rook Plugin Exception](../LICENSES/LicenseRef-Rook-Plugin-Exception.txt), and the SDK is MIT
([ADR-0010](adr/0010-license.md)). Native C++ extensions ([§9](#9-native-extensions-future))
are not covered by the exception.

## 2. Vocabulary and equivalents

| AMX Mod X | Modern-Client.v2 | Notes |
|-----------|------------------|-------|
| `.sma` (Pawn source) | `.rook` (Rook source) | [14](14-rook-language.md) |
| `amxxpc` | `rookc` | Own compiler, incremental, with LSP |
| `.amxx` (compiled plugin) | `.rbc` (Rook bytecode) | Verified at load |
| `plugins.ini` | `server/plugins.toml` | Enables mods and standalone plugins |
| `configs/*.ini`/`.cfg` | The mod's `configs/` + cvars | Same role |
| Modules (fakemeta, hamsandwich, ReAPI, engine) | Native engine API | Typed and stable ([15](15-mod-api.md)) |
| `register_native` / plugin natives | `pub` functions exported by plugins | Typed calls between plugins |
| `forward` / `CreateMultiForward` | `forward` declared with `pub forward` | Signature checked at compile time |
| `set_task` | `setTask` + coroutines (`await wait(1s)`) | Deterministic game time |
| nVault / SQLx | `nx.storage` (KV and SQLite) | Per capability |
| "API" plugin (e.g. ReZombie's `ApiClasses.sma`) | **API plugin** | Exports handles, `vars`, functions and forwards |
| Mod (e.g. ReZombie) | **Mod** (`mod.toml`) | Package with plugins, assets, locale and configs |
| AMXX / Metamod C++ modules | **Native extensions** (future) | Versioned C ABI, admin opt-in ([§9](#9-native-extensions-future)) |

## 3. Structure of a mod

```
mods/rezombie/
├── mod.toml
├── plugins/
│   ├── api/
│   │   ├── classes.rook        # API plugin: handles, vars, natives, forwards
│   │   ├── props.rook
│   │   └── weapons.rook
│   ├── class/
│   │   ├── human.rook
│   │   └── assassin.rook
│   ├── gamemodes/
│   │   └── nemesis.rook
│   └── dev/
│       └── runtime.rook
├── assets/                     # models/, sound/, sprites/ ... (mounted in the VFS)
├── locale/
│   ├── en.toml
│   └── pt-BR.toml
├── configs/
│   └── rezombie.cfg            # default cvars, executed on map start
└── tests/
    └── infectionTests.rook
```

### 3.1 `mod.toml`

```toml
[mod]
id       = "rezombie"
name     = "ReZombie"
version  = "0.4.0"
authors  = ["ReZombie Team"]
license  = "MIT"
api      = "^1.0"                 # Modern-Client.v2 plugin API range

[dependencies]
# "another-mod" = "^2.1"

[capabilities]                    # requested; the server admin grants them
storage = true

[[plugin]]
id     = "api.classes"
source = "plugins/api/classes.rook"
target = "server"

[[plugin]]
id     = "class.human"
source = "plugins/class/human.rook"
target = "server"

[[plugin]]
id     = "weapons.coldsteel"
source = "plugins/weapons/coldsteel.rook"
target = "shared"                 # runs on server and client (prediction)

[[plugin]]
id      = "dev.runtime"
source  = "plugins/dev/runtime.rook"
target  = "server"
profile = "debug"                 # only loaded with plugins debug profile
```

### 3.2 Automatic load order

- The order **is not manual**. It comes from the dependency graph: a plugin that `import`s
  something from another plugin (function, handle, content ID, forward) depends on it.
- ReZombie example: instead of "coldsteel must come before Class: Human" written by
  hand in the manifest, `human.rook` imports the `COLDSTEEL` ID exported by
  `coldsteel.rook`, and the compiler records the dependency.
- Dependency cycles are a build error (`R0601`), with the cycle printed.
- Ties are broken deterministically (alphabetical order of the ID).

## 4. Server configuration (`server/plugins.toml`)

```toml
# Equivalent of plugins.ini. Order here does not matter: dependencies decide it.

[[mod]]
id = "admin"                      # built-in admin system

[[mod]]
id      = "rezombie"
disable = ["dev.runtime"]         # turn off individual plugins
grant   = ["storage"]             # capabilities granted to this mod

[[plugin]]
path  = "plugins/welcome.rbc"     # standalone plugin outside any mod
debug = true                      # same idea as "plugin.amxx debug"
```

## 5. Lifecycle

| Forward | When | Typical use |
|---------|------|-------------|
| `pluginPrecache` | Before the map starts, **in every plugin** before any `pluginInit` | Register content (classes, weapons, entities) and mandatory resources |
| `pluginInit` | After every `pluginPrecache` | Register commands, cvars, dynamic hooks and menus. Look up other plugins' content by name |
| `pluginCfg` | After `server.cfg` and the mod's configs | Read cvars that are already configured |
| `pluginPause` / `pluginUnpause` | `plugins pause/unpause` | Suspend effects |
| `pluginEnd` | End of the map or unload | Save state and release external resources |

- Precache **no longer has a limit**: resources registered in `pluginPrecache` become
  mandatory downloads, and resources registered later are delivered on demand
  ([06 §6](06-assets-and-resources.md#6-end-of-precache-resource-registry)).
- Forwards return `Result`. An `err` in `pluginPrecache`/`pluginInit` **fails the plugin
  load** with the typed message (the modern equivalent of `set_fail_state`), and the
  plugins that depend on it do not load either, with the reason.

## 6. Execution model

### 6.1 Isolation

- Each plugin runs in an **isolate**: its own heap (its own garbage collection), its own
  globals and its own task queue, just as each `.amxx` had its own memory.
- A failure in one plugin **never** brings down the server or corrupts another plugin.

### 6.2 Order and threads

- Forwards and hooks are dispatched on the simulation thread, in **deterministic order**:
  hook priority, then plugin dependency order.
- **ECS systems** from different plugins, without access conflicts, run **in parallel**
  on the workers ([03 §13](03-ecs.md#13-rook-plugin-integration)).
- `pure` functions can run as jobs (`runJob`) in parallel, without access to the world
  ([14](14-rook-language.md#17-concurrency-and-determinism)).

### 6.3 Calls between plugins

- A plugin's `pub` functions are called by another plugin as if they were natives, but with
  types checked at compile time and at load (signature hash).
- Values cross isolates by **copy** (primitives, units, enums, structs,
  lists) or as a **handle** (entities, classes, weapons). Strings are immutable and
  shared without copying.
- Reentrancy (A → B → A) is allowed, with a configurable maximum depth.

### 6.4 Budgets and failures

| Limit | Cvar | Default |
|-------|------|---------|
| CPU per callback | `rook_budget_ms` | Conservative, adjustable per plugin |
| CPU per tick per plugin | `rook_tick_budget_ms` | Fraction of the tick |
| Memory per plugin | `rook_heap_limit_mb` | Adjustable per plugin |
| Call depth | `rook_max_call_depth` | VM stack limit |

When a limit is exceeded, or on `panic`, the callback is **aborted** with a typed failure
(`PluginFault.BudgetExceeded`, `PluginFault.HeapLimit`, `PluginFault.Panic{message}`,
`PluginFault.StackOverflow`), with a full Rook *stack trace* in the log. The policy comes from
`rook_fault_policy`:

| Policy | Effect |
|--------|--------|
| `pause` (default) | Plugin paused and admins notified |
| `reload` | Reloads the plugin (limited by a time window) |
| `continue` | Only logs (development) |

## 7. Plugins on the client

| Target | Runs on | Examples |
|--------|---------|----------|
| `server` | Server | Rules, modes, admin, economy |
| `client` | Client | Custom HUD, menus, local effects |
| `shared` | Both | **Custom weapons with prediction**, custom movement |

- `client`/`shared` plugins are sent by the server as a resource (hash verified,
  [06 §7](06-assets-and-resources.md#7-resource-downloads)). They **end the "lag"
  of custom weapons**, which in AMXX ran only on the server.
- **Strict sandbox on the client:** no file system (except per-server local storage with
  a quota), no network (except typed messages with its own server), no input reading
  outside its own screens, and smaller budgets.
- The modules allowed on the client are verified at load. Importing a server-only module
  in a `client` plugin is a compile error.
- The player controls this with `cl_plugins 1/0`. The server can require client plugins
  and refuse the connection with a clear reason.

## 8. Hot reload

```text
] plugins reload rezombie/class.assassin
[rook] compiling rezombie/class.assassin ... ok (12 ms)
[rook] migrated 3 persistent globals, reset 0
[rook] rezombie/class.assassin reloaded
```

1. Compiles (if the source changed) and verifies the new bytecode.
2. Calls `pluginEnd` on the old version.
3. Migrates the `persist var` globals whose name and type match. Incompatible ones are
   reset, with a warning listing each one.
4. Re-registers hooks, commands, forwards and cvars. Registered content (classes, weapons)
   is **updated** under the same ID.
5. Calls `pluginInit` and `pluginCfg`.

In development, `rook_watch 1` reloads automatically when the file is saved.

## 9. Native extensions (future)

For cases that really require C++ (integration with external systems, heavy
algorithms), there will be a **versioned C ABI** (`mcv2_ext_v1`):

- Server only, **off by default** and explicitly enabled by the admin.
- The extension declares its functions in an IDL, from which `rookc` generates the typed
  declarations.
- Outside the sandbox: the server reports at boot which extensions are loaded.

## 10. Capabilities

| Capability | Unlocks |
|------------|---------|
| `storage` | `nx.storage` (KV and SQLite per mod) |
| `http` | `nx.http` (outbound to hosts allowed in `plugins.toml`) |
| `files` | Reading and writing in `data/<mod>/` |
| `serverCommand` | `serverCmd` (run server console commands) |
| `clientPlugins` | Send `client`/`shared` plugins |
| `nativeExtensions` | Call native extensions |

A plugin that imports a module without the capability being granted **does not load**
(`CapabilityError.NotGranted{capability, mod}`), instead of failing in the middle of the match.

## 11. Tools

| Command | Purpose |
|---------|---------|
| `rook new <type>` | Generates a plugin, mod or API plugin from a template |
| `rook build` | Compiles the mod (incremental) |
| `rook check` | Type checking only, fast (for the editor) |
| `rook test` | Runs `@test` tests with a simulated *headless* server |
| `rook fmt` / `rook lint` | Canonical formatting and lints |
| `rook doc` | HTML documentation from `///` |
| `rook pack` | Generates a reproducible `.nxpak` of the mod |
| `rook migrate <file.sma>` | Assisted conversion from Pawn to Rook ([16](16-amxx-migration.md)) |
| `rookLsp` | Language server: autocompletion (including `vars` keys), go to definition, rename, diagnostics as you type |
| Debugger (DAP) | Breakpoints, stepping and inspection in VS Code, connected to a local server |

## 12. Built-in mods (replacements for the default AMXX plugins)

Shipped with the game, written in Rook and serving as reference examples:

| Mod/plugin | AMXX equivalent |
|------------|-----------------|
| `admin` | `admin`, `admincmd`, `adminhelp`, `adminslots`, `adminchat` |
| `admin.menus` | `menufront`, `cmdmenu`, `plmenu`, `telemenu`, `mapsmenu` |
| `votes` | `adminvote`, `mapchooser` |
| `maps` | `nextmap`, `timeleft` |
| `chat` | `antiflood`, `scrollmsg`, `imessage` |
| `stats` | `statsx`, `statscfg` |
| `pausecfg` | `pausecfg` |

The admin system uses `server/admins.toml` with **typed** permissions (`Permission.Kick`,
`Permission.Ban`, `Permission.Map`…) instead of flag letters, and with automatic import
of `users.ini`.

## 13. Typed errors

| Enum | Examples |
|------|----------|
| `PluginLoadError` | `CompileFailed`, `VerifyFailed`, `DependencyMissing{plugin}`, `DependencyFailed`, `ApiVersionMismatch{required, available}` |
| `PluginFault` | `BudgetExceeded`, `HeapLimit`, `Panic{message}`, `StackOverflow`, `StaleHandle` |
| `CapabilityError` | `NotGranted{capability, mod}`, `NotAllowedOnClient` |
| `ManifestError` | `InvalidField{path, line}`, `DuplicatePluginId`, `UnknownTarget` |
| `ReloadError` | `IncompatibleContent{id}`, `ReloadLoopDetected` |
