# ADR-0004 — Vulkan first, DirectX 11-compatible RHI, shaders in Slang

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The request is Vulkan now and DirectX 11 in the future. The renderer needs parallel
recording, GPU skinning and low latency, and it cannot be rewritten when D3D11 arrives.

## Decision

- **Own RHI**, explicit but restricted to what D3D11 can express: *bind groups* as the unit
  of binding, *bindless* only as an optional tier, no mesh shaders or ray tracing on the main
  path ([05 §3](../05-rendering.md#3-rhi-render-hardware-interface)).
- **Vulkan 1.3** as the first backend (dynamic rendering, synchronization2, timeline
  semaphores), with volk and VMA.
- A **render graph** that derives barriers, aliasing and parallelism.
- **Slang** as the shader language: SPIR-V for Vulkan and HLSL → DXBC (FXC) for D3D11.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Renderer directly on Vulkan, without an RHI | Rewrite for D3D11 |
| RHI based on mandatory bindless | Incompatible with D3D11 |
| bgfx / Diligent / wgpu-native | Less control over the render graph and latency, and an extra layer to debug. GoldSrc fidelity requirements call for fine control |
| Plain HLSL with DXC | DXC does not generate DXBC for D3D11. Slang covers both targets and has modules and generics |
| OpenGL | No real parallel recording, and driver overhead |

## Consequences

- ✅ One renderer, several backends, with RHI conformance tests.
- ⚠️ Advanced features only come in as optional tiers. For the GoldSrc look this is not a
  limitation.
- ⚠️ Dependency on Slang. Mitigation: pinned version and shaders compiled at build time.
