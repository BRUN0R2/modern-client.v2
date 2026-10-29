# ADR-0025 — The enhanced look: physically based materials, the map's reflections, HDR and bloom

- **Status:** Proposed
- **Date:** 2026-10-23

## Context

`r_fidelity legacy`, the default, draws what Counter-Strike 1.6 draws, to the bit where it can be
measured ([05 §11](../05-rendering.md#11-visual-fidelity)): lightmaps, the GoldSrc gamma curves, the
studio models' shade and their chrome, whose texture coordinates follow the normal against the
viewer (`R_StudioSetupChrome`). `r_fidelity enhanced` is planned for M7 with little more than a
better filter and optional high-resolution packs.

The maintainer asks for an enhanced look that is modern, optimized, better than the legacy ways and
running on many graphics cards: weapons whose metal and gold shine as metal does, parts that glow,
the newer models mods bring. The engines of Counter-Strike's successors (Counter-Strike Online among
them) do it with the techniques of their time: one fixed cubemap reflected by the view, a specular
strength tuned by hand per texture, and a glow pass apart from a threshold bloom. Each part is
tuned for one scene and looks wrong in the next.

What bounds the answer:

- **The legacy look stays the default and stays exact.** Enhanced is optional and never changes
  gameplay: what a player sees through smoke, in the dark or through a flashbang is the same in both
  modes ([05 §11](../05-rendering.md#11-visual-fidelity)).
- **The RHI's baseline** ([ADR-0004](0004-vulkan-first-abstract-rhi.md), [05 §4, §5](../05-rendering.md#4-vulkan-backend)):
  Vulkan 1.3, a Direct3D 11 backend to come, so no mandatory bindless, mesh shaders, ray tracing,
  nor compute beyond Shader Model 5.0 on the main path.
- **Almost any PC, no card left out:** Counter-Strike 1.6 and Counter-Strike Online run on almost
  any PC and still look good; the maintainer asks the same of enhanced. It must run well on every
  GPU the RHI runs on, down to the integrated ones and the older cards of the Direct3D 11 backend,
  with the same look on all: no part of it kept for strong cards. What those GPUs lack is
  bandwidth and memory, not arithmetic, so the look is chosen to be cheap in both.
- **Data, not code:** a mod brings its look as files, as it brings its models and sounds, with no
  limit on a texture's size beyond the GPU's own.
- **The frame is one render pass today** ([05 §7.1](../05-rendering.md#71-frame-passes-legacy-mode)):
  anything drawn after the scene (a bloom, a tone curve) needs the render graph of
  [05 §7](../05-rendering.md#7-render-graph) first.

## Decision

Proposed: `r_fidelity enhanced` is a physically based pipeline in five parts, forward rendered on
the baseline of the RHI, each part chosen for its cost on the weakest GPU it runs on, so that every
card draws the same look.

### 1. A linear HDR scene, a fixed exposure, a tone curve

- In enhanced, textures are read as sRGB and lit in linear light; the scene is drawn to a
  floating-point target of 4 bytes a pixel, as many as an 8-bit one: `B10G11R11_UFLOAT`, which every
  Direct3D 11 card renders and blends to and Vulkan's desktop GPUs too; `R16G16B16A16_SFLOAT`, twice
  the bandwidth, only where a GPU cannot.
- The map's light stays the map's: lightmaps and the studio models' GoldSrc shade are the diffuse
  light, converted to linear radiance, so that de_dust2 keeps the light its author baked.
- **Exposure is fixed per map**, calibrated so that a mid-grey surface is as bright as in legacy.
  No eye adaptation: a dark corner never brightens to reveal a player, nor dims one.
- A tone curve (AgX) maps the scene to the screen; HDR10 output on HDR displays is a later option.
- The additive effects made for the original's addition (a weapon's muzzle flashes; glows and
  explosions as they come) are not added as light: they are drawn to a layer of display colors over
  the scene's depths, which the last pass adds once the scene is mapped, each channel stopping at
  white as the original adds them. A flame authored that way turns yellow, then white, as it was
  made, where a curve that keeps a color's hue would keep it orange; its own light feeds the bloom.
- The decals made for the original's blend of display colors (a bullet hole, blood on a wall) keep
  it: blended as light with their alpha, a mark half opaque would keep about twice the light of the
  wall under it that the original keeps, and the dark ones fade. Each texel gives the blend two
  colors (dual-source blending), the light it adds and the share it keeps of the light under it,
  channel by channel, so that a mark is the original's exactly over a black wall, a white one, and
  when it is black over any; a little darker between them (the game's blood, by at most 16 of 255
  on its red channel).

### 2. Bloom, energy-conserving, without a threshold

- The HDR scene is reduced in a chain of mips (a 13-tap filter, the first level averaged by
  brightness to keep single bright texels from flickering) and enlarged back with a tent filter,
  then added at a small strength (Call of Duty: Advanced Warfare's method). Only what is brighter
  than white blooms visibly; there is no threshold to tune per map.
- Fragment shaders, not compute: the same passes run on Direct3D 11 and on tile-based GPUs.
- Its strength is capped: bloom never hides a player standing before a light.

### 3. Physically based materials, from data

- A surface's **material** follows glTF 2.0's metallic-roughness model: its base color, how
  metallic, how rough, a normal map, ambient occlusion and an emissive color and strength. Gold is
  `metallic = 1`, `roughness = 0.25`, base color `(1.00, 0.71, 0.29)` in linear light; its shine
  follows from the light around it, in every map, with nothing tuned per scene.
- A model's materials are a file beside it, read by `nx.data`: `models/v_drakar.material.toml`
  names, for each texture of the `.mdl`, its maps and values. The model keeps its skeleton, its
  animations and its own textures as the base color unless the file gives others; without the
  file, a material is derived from the GoldSrc flags (a chrome texture metallic, a fullbright one
  emissive, additive and masked ones blended as they are), so every classic model has an enhanced
  look with no work. Counter-Strike Online's texture conventions (`#WWWHHH` external textures,
  `$xx` render modes) become materials the same way, once read from the files themselves
  ([20 §3](../20-legal-and-licensing.md#3-clean-room)).
- **Emissive is per pixel**: an emissive map lights exactly the pixels that glow, where GoldSrc's
  fullbright lights a whole texture; what is emissive feeds the bloom, so a glow needs no pass of
  its own.
- Textures are KTX2: BC7 for colors, BC5 for normals, occlusion, roughness and metallic packed in
  one texture, with mips; Basis Universal transcoded to what the GPU reads; PNG accepted for
  authoring. Three texture reads a pixel.
- Shading: GGX with height-correlated Smith visibility and Schlick's Fresnel, Lambert diffuse, and
  specular anti-aliasing (roughness widened by the normal's variance across a pixel), so that thin
  metal edges do not shimmer at low resolutions.

### 4. Reflections from the map itself: probes, baked as it loads

- When a map loads, the engine renders it into **reflection probes**, cube captures at its spawn
  points, bomb sites, hostage rescue zones and buy zones, and at a sparse grid over its open leaves,
  each with the box of the leaves around it. A probe is prefiltered for roughness (GGX importance
  sampling, one mip a roughness, split-sum with one shared lookup table) and its diffuse light kept
  as nine spherical harmonics.
- Box projection corrects a reflection for the probe's place: the gun reflects the wall beside it
  and the sky above, not a generic cubemap.
- The probes are a cubemap array; the CPU picks the probe (or two to blend) of each model, the view
  model's at the eye: no search per pixel.
- Baked asynchronously at load on the GPU, cached on disk by the map's hash: a map seen once loads
  its probes from the cache.

### 5. Highlights from the map's lights

- The light entities a map keeps (`light`, `light_spot`, `light_environment`) and the dynamic lights
  (a muzzle flash, a flashlight) give specular highlights with the same GGX: the gun shines as it
  passes a lamp. The lightmaps still give the diffuse light, so nothing is lit twice.
- First, light lists per object, the four that matter most chosen by the CPU; clustered lists by
  screen tile and depth when the world's surfaces get normal maps, if they stay within the budget.

### Every card, the same look

- Nothing in enhanced needs more than the RHI's baseline: Vulkan 1.3's core and what Direct3D 11's
  feature level 11_0 has (cubemap arrays, BC7, BC5, floating-point targets, dual-source blending,
  Shader Model 5.0), so
  the Direct3D 11 backend draws it as the Vulkan one does.
- The cost is bounded by design, not by tiers: a material reads three textures a pixel, a
  reflection one probe and one lookup table; the bloom starts at half resolution and keeps five
  mips; a model lights itself by four lights at most; the probes are 128 pixels a face. Each is the
  same on every card.
- Budget, measured by the benchmarks of [18](../18-testing-and-quality.md): the whole of enhanced
  within 2 ms a frame at 1920×1080 on an Intel UHD 620, a common integrated GPU, and within 0.5 ms
  on a GTX 1060 or an RX 580; the Direct3D 11 backend's older cards are measured when it comes. A
  part that exceeds it is made cheaper, not reserved.
- A player may still turn a part off (`r_bloom 0`, `r_reflections 0`), as Counter-Strike 1.6's
  video options turn things off; the defaults are the same on every card.
- Correctness is tested on lavapipe in the CI, as the renderer is today: each pass against reference
  images, with a tolerance.

### Order of work

1. The render graph's first passes: the HDR scene, the tone curve, the legacy frame unchanged to the
   bit (its reference screenshots).
2. Bloom.
3. Materials: the files, KTX2 and PNG, the shading of studio models, emissive.
4. Reflection probes.
5. Highlights from the map's lights.
6. Later: the world's materials (WAD textures with normal maps), HDR10 output.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep only the legacy look | The maintainer asks for a modern one; legacy stays the default, exact |
| The older engines' way: one fixed cubemap, specular tuned by hand, a glow pass | Right for one scene, wrong in the next; values per texture to keep in step by hand |
| Quality tiers that give weaker cards a lesser look | The maintainer rules it out: as Counter-Strike 1.6 and Counter-Strike Online, the look must be the same on almost any PC; the parts are made cheap enough instead |
| Deferred shading (a G-buffer) | Several full-screen targets of bandwidth integrated GPUs lack, MSAA and translucent surfaces harder; the scenes have few lights, which forward handles |
| Screen-space reflections | Costly, and they break exactly on the view model and at the screen's edges |
| Ray-traced reflections | Not on the RHI's baseline (no Direct3D 11, few integrated GPUs) |
| Eye adaptation, depth of field, motion blur | They change what a player sees; enhanced may not |
| Temporal anti-aliasing | Smears motion and adds latency in a competitive game; MSAA or FXAA as [05 §11](../05-rendering.md#11-visual-fidelity) plans, and specular anti-aliasing |
| Re-baking the maps' light | The authors' light is part of the maps; enhanced reads it, it does not replace it |

## Consequences

- ✅ Metal, gold and glowing parts look as they should in every map, from data a mod brings, with no
  tuning per scene; every classic model gains an enhanced look from its flags alone.
- ✅ One pipeline and one look for the game's content and mods', on every card the RHI runs on.
- ✅ The legacy look is untouched and stays the default.
- ⚠️ The render graph comes first: a larger first part. Mitigation: it starts with the passes
  enhanced needs and the legacy frame drawn through it unchanged, proven by its screenshots.
- ⚠️ New dependencies: KTX-Software (Apache-2.0) and a PNG reader, both already planned
  ([17 §5](../17-build-and-tools.md#5-dependencies)).
- ⚠️ Probes cost memory (a cubemap array: at 128 pixels a face with its mips, about 1 MB a probe in
  half-float, half that in `B10G11R11_UFLOAT`) and time at load. Mitigation: a budget of probes a
  map, the cache.
- ⚠️ A fixed cost on every card leaves strong cards headroom unused. Mitigation: that headroom goes to
  frame rate and latency, which a competitive game values more.
- ⚠️ A material derived from GoldSrc flags is a guess. Mitigation: a material file overrides it,
  and enhanced is optional.
