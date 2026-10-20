# Glossary

| Term | Definition |
|-------|-----------|
| **A2S** | Valve's server query protocol (`A2S_INFO`, `A2S_PLAYER`, `A2S_RULES`), used by server browsers and monitoring tools |
| **API plugin** | Plugin that exports handles, `vars`, functions and forwards to other plugins |
| **Archetype** | Exact set of component types. Entities with the same set are stored together ([03](03-ecs.md)) |
| **Attachment** | Named point of a model (muzzle, shell ejection) used for effects and attached items |
| **Baseline** | Reference state for delta encoding on the network |
| **Bind group** | Set of GPU resources bound together to a pipeline; the binding unit of the RHI |
| **Body group / body part** | Part of a studio model with alternative submodels (e.g. a weapon with or without a silencer) |
| **BSP** | GoldSrc map format (v30): geometry, lightmaps, visibility, hulls and entities |
| **Capability** | Permission granted by the administrator to a mod (`storage`, `http`…) |
| **Chunk** | 16 KiB memory block of an archetype, with SoA columns |
| **Clipnode** | Node of the BSP collision tree for a specific hull |
| **Command buffer** | Queue of ECS structural changes applied at synchronization points |
| **Cvar** | Configurable console variable (`mp_roundtime`, `sensitivity`) |
| **Edict** | Entity in the GoldSrc model, limited by a fixed cap. Replaced by `Entity` |
| **Ephemeral (handle)** | Rook handle valid only during the current callback. To keep it, use `Ref` |
| **Extract** | Copy of the simulation state needed to render the frame |
| **Forward** | Plugin function called by the engine or by another plugin on an event (`pluginInit`, `roundStart`) |
| **GoldSrc** | Original engine of Half-Life and Counter-Strike 1.6 |
| **Launcher** | `nextLauncher`, the program players open: it updates itself and the client, then starts the game ([21](21-launcher-and-updates.md)) |
| **Hammer unit (`hu`)** | Distance unit of maps (≈ 1 inch) |
| **Handle** | Opaque, typed, generational identifier of an object |
| **Hitbox / hitgroup** | Bullet collision volume tied to a bone, and the body region that sets the damage multiplier |
| **Hook** | Interception of a game action, with a pre phase (may change or block it) and a post phase |
| **Hull** | Collision box used in traces (point, standing, crouching, large) |
| **Isolate** | Execution environment of a Rook plugin: its own heap, globals and tasks |
| **Job** | Unit of work run by the Job System on a worker |
| **Lag compensation** | Rewinding targets on the server to the instant the shooter saw |
| **Lightmap** | Precomputed lighting texture of BSP surfaces |
| **Lightstyle** | Animated light intensity pattern (flicker, pulse) applied to lightmaps |
| **MDL** | GoldSrc studio model format (v10) |
| **Mod** | Distributable package with plugins, assets, translations and configs (`mod.toml`) |
| **PAS** | *Potentially Audible Set*: regions from which a sound can be heard |
| **Plugin** | Loadable unit compiled by `rookc` (`.rbc`). Equivalent to an `.amxx` |
| **Precache** | In the original, the mandatory, limited registration of resources. Here, registration without a limit |
| **Prediction** | Simulating the local player before the server confirms |
| **PVS** | *Potentially Visible Set*: regions visible from a BSP region |
| **RHI** | *Render Hardware Interface*: abstraction over Vulkan/D3D11 |
| **Render graph** | Description of the passes of the frame, from which barriers, aliasing and parallelism are derived |
| **Rook** | Plugin language of the project ([14](14-rook-language.md)) |
| **`rookc`** | Compiler of the Rook language |
| **`.rookd`** | API declaration file (the modern equivalent of `.inc`), generated from C++ |
| **Snapshot** | World state sent from the server to the client at a given instant |
| **SPR** | GoldSrc sprite format (paletted). The modern equivalent is `.nxspr` |
| **Tick** | Fixed step of the server simulation (`sv_tickrate`) |
| **Usercmd** | Client input command (movement, buttons, angles, duration) |
| **Varint** | Variable-length integer: small values take few bytes, with no fixed cap |
| **`vars` (catalog)** | Declaration of the properties of a handle, used by the `get*Var`/`set*Var` pattern with keys checked at compile time |
| **WAD** | GoldSrc texture file (WAD3) |
| **Work-stealing** | Strategy in which idle workers steal work from the queues of other workers |
