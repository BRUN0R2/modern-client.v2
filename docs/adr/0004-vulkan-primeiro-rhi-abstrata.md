# ADR-0004 — Vulkan primeiro, RHI compatível com DirectX 11, shaders em Slang

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O pedido é Vulkan agora e DirectX 11 no futuro. O renderer precisa de gravação paralela,
GPU skinning e baixa latência, e não pode ser reescrito quando o D3D11 chegar.

## Decisão

- **RHI própria**, explícita mas restrita ao que o D3D11 consegue expressar: *bind groups*
  como unidade de binding, *bindless* apenas como tier opcional, sem mesh shaders nem ray
  tracing no caminho principal ([05 §3](../05-renderizacao.md#3-rhi-render-hardware-interface)).
- **Vulkan 1.3** como primeiro backend (dynamic rendering, synchronization2, timeline
  semaphores), com volk e VMA.
- **Render graph** que deriva barreiras, aliasing e paralelismo.
- **Slang** como linguagem de shader: SPIR-V para Vulkan e HLSL → DXBC (FXC) para D3D11.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Renderer direto em Vulkan, sem RHI | Reescrita para o D3D11 |
| RHI baseada em bindless obrigatório | Incompatível com D3D11 |
| bgfx / Diligent / wgpu-native | Menos controle sobre o render graph e a latência, e camada extra para depurar. Requisitos de fidelidade GoldSrc pedem controle fino |
| HLSL puro com DXC | DXC não gera DXBC para D3D11. Slang cobre os dois alvos e tem módulos e genéricos |
| OpenGL | Sem gravação paralela real e com driver overhead |

## Consequências

- ✅ Um renderer, vários backends, com testes de conformidade da RHI.
- ⚠️ Recursos avançados só entram como tiers opcionais. Para o visual GoldSrc isso não é
  limitação.
- ⚠️ Dependência do Slang. Mitigação: versão fixada e shaders compilados no build.
