# ADR-0001 — C++26 com módulos nomeados

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O projeto recria uma engine inteira e quer código moderno, seguro e fácil de manter. O
C++26 traz recursos que eliminam código repetitivo e frágil: reflexão estática (metadados
de componentes, serialização, replicação, bindings de plugins), contratos (invariantes
explícitas) e `std::execution`. Módulos reduzem tempo de build e isolam detalhes de
implementação. O suporte dos compiladores a esses recursos ainda varia.

## Decisão

- Padrão **C++26**, sem extensões de compilador.
- Código próprio em **módulos nomeados**, com `import std;`. Headers de terceiros só no
  *global module fragment*.
- Recursos sem suporte universal (reflexão, anotações, contratos, `std::execution`) ficam
  **atrás de fachadas** em `nx.core`/`nx.meta`, com fallback. O CI garante que os dois
  caminhos produzem o mesmo resultado.
- Matriz de toolchain validada no M0 e revisada a cada release de compilador
  ([17 §2](../17-build-e-ferramentas.md#2-toolchains)).

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| C++20/23 com headers | Perde reflexão e contratos. Exigiria geradores de código externos para metadados |
| Rust | Ecossistema de engine e integração com Vulkan/D3D maduros em C++, além do pedido explícito por C++26 |
| C++26 sem módulos | Builds mais lentos e vazamento de detalhes por headers |

## Consequências

- ✅ Metadados gerados do próprio código (componentes, esquemas TOML, rede, API Rook).
- ✅ Interfaces de módulo limpas e builds incrementais rápidos.
- ⚠️ Risco de bugs de compilador com módulos + reflexão. Mitigação: fallback testado,
  CI com dois compiladores e conversão mecânica para headers possível em último caso.
- ⚠️ Exige CMake recente e Ninja.
