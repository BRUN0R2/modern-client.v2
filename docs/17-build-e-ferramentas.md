# 17 — Build, dependências e ferramentas

## 1. Layout do repositório

```
modern-client.v2/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json                  # dependency manifest (pinned baseline)
├── .clang-format / .clang-tidy / .editorconfig
├── cmake/                      # toolchain helpers, warnings, sanitizers, module support
├── engine/
│   ├── core/                   # nx.core
│   ├── math/                   # nx.math
│   ├── meta/                   # nx.meta (reflection facade + fallback)
│   ├── jobs/                   # nx.jobs
│   ├── platform/               # nx.platform (SDL3)
│   ├── ecs/                    # nx.ecs
│   ├── rhi/                    # nx.rhi (interface)
│   ├── rhiVulkan/             # nx.rhi.vulkan
│   ├── rhiD3d11/              # nx.rhi.d3d11 (future)
│   ├── render/                 # nx.render
│   ├── assets/                 # nx.assets (+ goldsrc importers)
│   ├── audio/                  # nx.audio
│   ├── net/                    # nx.net
│   ├── collision/              # nx.collision
│   ├── ui/                     # nx.ui
│   ├── game/                   # nx.game
│   └── script/                 # nx.script (Rook VM host + bindings)
├── game/
│   ├── shared/                 # cs.shared (prediction-safe code)
│   ├── server/                 # cs.server
│   └── client/                 # cs.client
├── apps/
│   ├── client/                 # next
│   ├── server/                 # nextds
│   └── master/                 # nextmaster
├── rook/
│   ├── spec/                   # grammar, conformance tests
│   ├── compiler/               # rookc (frontend, checker, RIR, codegen)
│   ├── vm/                     # interpreter, GC, verifier
│   ├── stdlib/                 # std.* in Rook + native parts
│   ├── lsp/                    # rookLsp
│   ├── cli/                    # rook (build, test, fmt, lint, doc, pack, migrate)
│   └── vscode/                 # VS Code extension (syntax, LSP client, DAP)
├── shaders/                    # Slang sources
├── data/
│   └── cstrike/                # gameplay data (TOML)
├── mods/                       # built-in mods written in Rook (admin, votes, maps, ...)
├── tools/
│   ├── nxbake/                 # asset import/cook/pack
│   └── parityRecorder/        # reference data capture from the original game
├── tests/
│   ├── parity/                 # parity fixtures and tests
│   ├── integration/
│   ├── fuzz/
│   └── bench/
├── scripts/
│   └── devShell.ps1           # loads the Visual Studio x64 environment for CLI builds
├── docs/
└── TODO.md
```

Cada módulo de `engine/`, `game/` e `rook/` segue a organização de
[02 §2](02-padroes-de-codigo.md#2-organização-de-arquivos-e-módulos).

## 2. Toolchains

| Ferramenta | Versão | Observação |
|------------|--------|------------|
| CMake | 4.3+ (validado com 4.3.1 e 4.4.4) | Módulos C++ estáveis. O módulo `std` é compilado pelo projeto ([ADR-0014](adr/0014-modulo-std-compilado-pelo-projeto.md)) |
| Ninja | 1.12+ (o do Visual Studio serve) | Obrigatório (scanning de módulos) |
| vcpkg | Modo manifesto com *baseline* fixa (o do Visual Studio serve) | Builds reproduzíveis, triplets próprios sem exceções |
| Slang | Versão fixada | Compilação de shaders |
| Python | 3.12+ | Scripts de build e CI apenas (não é dependência de runtime) |

### 2.1 Matriz de compiladores

O projeto exige o subconjunto de C++26 de [02 §1.1](02-padroes-de-codigo.md#11-recursos-adotados),
com fallbacks atrás de fachadas.

| Compilador | Plataformas | Papel | Status (2026-10-20) |
|------------|-------------|-------|---------------------|
| MSVC 14.51 (Visual Studio 2026 18.x) | Windows | Toolchain de desenvolvimento no Windows | **Validado**: CMake 4.3.1 e 4.4.4, Ninja, Debug e Release, 84 testes passando |
| GCC 16+ | Linux, Windows (MinGW-w64 UCRT) | Referência para reflexão estática e contratos | A validar (presets prontos) |
| Clang (versão estável mais recente) | Linux, Windows | Sanitizers (ASan, TSan, MSan, UBSan), `clang-tidy`, `clangd` | A validar (presets prontos) |

Recursos de C++26 disponíveis no MSVC 14.51 (por *feature-test macros*): **nenhum** dos
listados em [02 §1.1](02-padroes-de-codigo.md#11-recursos-adotados). Todos usam o caminho de
fallback (introspecção por assinatura, contratos por funções, aritmética saturada própria).
O CMake ainda não conhece um switch `/std:c++26` para o MSVC, então o dialeto é
`/std:c++latest`.

### 2.2 Particularidades conhecidas

| Problema | Contorno no projeto |
|----------|---------------------|
| O suporte a `import std` do CMake ainda é experimental (*gate* com UUID que muda a cada versão) e, no MSVC, gera `@cmake_cxx_std.lib`, que o `link.exe` lê como arquivo de resposta | **Resolvido na causa:** o projeto não usa esse suporte. `cmake/nxStdModule.cmake` lê o manifesto `modules.json` da biblioteca padrão e compila o módulo `std` como o alvo comum `nx::std` ([ADR-0014](adr/0014-modulo-std-compilado-pelo-projeto.md)) |
| Fontes fora da árvore ganham diretórios de objeto com *hash* longo e podiam passar de 260 caracteres | **Resolvido na causa:** o fonte do módulo `std` é copiado para `build/<preset>/nxStd/` e compilado de lá, com caminhos curtos |
| Flags que mudam o ABI (exceções, RTTI) precisam ser iguais em todas as unidades, inclusive no módulo `std` | Ficam em `CMAKE_CXX_FLAGS` globais. Warnings ficam por alvo e não se aplicam ao `nx::std` |
| O MSVC desativa `std::println(std::ostream&, ...)` sem exceções (comportamento documentado da STL) | Saída via `std::println(stderr, ...)` ou `std::fputs` (`FILE*`) |

## 3. Presets

| Preset | Uso | Status |
|--------|-----|--------|
| `dev-windows-msvc` / `release-windows-msvc` | Desenvolvimento e release no Windows (Ninja + MSVC) | Validado |
| `dev-linux-gcc` / `release-linux-gcc` | Desenvolvimento e release no Linux com GCC | A validar |
| `dev-linux-clang` | Desenvolvimento com `clangd`/`clang-tidy` | A validar |
| `asan-linux-clang`, `tsan-linux-clang` | Sanitizers (ASan + UBSan, TSan) | A validar |
| `server-headless`, `tools` | Só `nextds` ou só ferramentas | Quando esses alvos existirem |

Cada preset de configuração tem um preset de build e um de teste com o mesmo nome. O
diretório de build é `build/<preset>`.

**Windows (linha de comando):** o build usa Ninja + MSVC, que precisam do ambiente do
Visual Studio. O script `scripts/devShell.ps1` carrega esse ambiente e define
`VCPKG_ROOT` com o vcpkg do Visual Studio. Visual Studio, VS Code (CMake Tools) e CLion
fazem isso sozinhos.

```powershell
. .\scripts\devShell.ps1
cmake --preset dev-windows-msvc
cmake --build --preset dev-windows-msvc
ctest --preset dev-windows-msvc
```

**Linux:**

```bash
cmake --preset dev-linux-gcc
cmake --build --preset dev-linux-gcc
ctest --preset dev-linux-gcc
```

## 4. Opções de build

| Opção | Padrão | Função |
|-------|--------|--------|
| `NX_BUILD_TESTS` | `ON` | Compila os testes e instala a feature `tests` do vcpkg (GoogleTest) |
| `NX_WARNINGS_AS_ERRORS` | `ON` | Warnings como erro (`/WX`, `-Werror`) |
| `NX_BUILD_CLIENT` | `ON` | Compila `next` |
| `NX_BUILD_SERVER` | `ON` | Compila `nextds` |
| `NX_BUILD_TOOLS` | `ON` | Compila ferramentas |
| `NX_RHI_VULKAN` | `ON` | Backend Vulkan |
| `NX_RHI_D3D11` | `OFF` | Backend DirectX 11 (futuro) |
| `NX_ENABLE_TRACY` | `ON` em dev | Profiler |
| `NX_REFLECTION_BACKEND` | `auto` | `native` (P2996) ou `fallback` |
| `NX_CONTRACT_SEMANTIC` | por módulo | `enforce` ou `observe` ([02 §6.5](02-padroes-de-codigo.md#65-contratos)) |

As opções a partir de `NX_BUILD_CLIENT` entram junto com os módulos correspondentes. Hoje
existem só `NX_BUILD_TESTS` e `NX_WARNINGS_AS_ERRORS`.

## 5. Dependências

| Biblioteca | Uso | Licença |
|------------|-----|---------|
| SDL3 | Janela, entrada, áudio de dispositivo, captura de voz | Zlib |
| Vulkan Headers + volk | API e loader Vulkan | Apache-2.0 / MIT |
| Vulkan Memory Allocator | Memória de GPU | MIT |
| Slang | Compilador de shaders | Apache-2.0 com exceção LLVM |
| stdexec | `std::execution` até o suporte da biblioteca padrão | Apache-2.0 com exceção LLVM |
| libsodium | Criptografia de rede, assinaturas | ISC |
| zstd | Compressão de pacotes e rede | BSD-3-Clause |
| BLAKE3 | Hash de conteúdo | CC0 / Apache-2.0 |
| xxHash | Implementação de referência usada nos testes do `nx::hash64` (o hash do projeto é um XXH64 `constexpr` próprio) | BSD-2-Clause |
| Opus | Voz e áudio | BSD-3-Clause |
| minimp3, stb_vorbis, dr_flac | Decodificação de áudio | CC0 / domínio público / MIT-0 |
| fastgltf | Importação glTF | MIT |
| KTX-Software | KTX2/Basis | Apache-2.0 |
| libspng ou stb_image | PNG/TGA | BSD-2 / domínio público |
| toml++ | Configuração e dados (modo sem exceções) | MIT |
| SQLite | `nx.storage` | Domínio público |
| Tracy | Profiler | BSD-3-Clause |
| Dear ImGui | Ferramentas de desenvolvimento | MIT |
| GoogleTest | Testes unitários, incluindo testes de morte, com suporte oficial a builds sem exceções | BSD-3-Clause |

Regras:

- Toda dependência nova passa por revisão de licença (compatível com a licença do projeto,
  [20](20-legal-e-licenciamento.md)), manutenção ativa e superfície de segurança.
- Dependências são compiladas pelos triplets do projeto (`cmake/triplets/x64-windows-nx`,
  `x64-linux-nx`): bibliotecas estáticas, **sem exceções e sem RTTI**, como o nosso código.
  Bibliotecas que exigem exceções ficam isoladas atrás de uma fronteira que converte para
  `nx::Result`, ou não são usadas.
- `THIRD_PARTY_NOTICES.md` é gerado a partir do `vcpkg.json` no build de release.

## 6. Alvos

| Alvo | Descrição |
|------|-----------|
| `next` | Cliente (inclui listen server) |
| `nextds` | Servidor dedicado *headless* |
| `nextmaster` | Master server |
| `rookc`, `rook`, `rookLsp` | Toolchain Rook |
| `nxbake` | Ferramenta de assets |
| `<módulo>Tests` (ex.: `nxCoreTests`) | Testes por módulo, registrados no CTest um a um (`gtest_discover_tests`) |
| `nxBench*` | Benchmarks |
| `fuzz*` | Alvos de fuzzing (libFuzzer) |

## 7. Integração contínua

| Job | Gatilho | Conteúdo |
|-----|---------|----------|
| `build-test` | Todo PR | Windows + Linux, GCC + Clang, testes unitários e de integração |
| `format-lint` | Todo PR | `clang-format --dry-run`, `clang-tidy`, `rook fmt --check`, `rook lint` |
| `layers` | Todo PR | Verificação do grafo de dependências entre módulos ([01 §1.1](01-arquitetura.md#11-catálogo-de-módulos)) |
| `sanitizers` | Todo PR | ASan + UBSan e TSan nos testes |
| `api-compat` | Todo PR | Diff dos `.rookd` contra a última release ([15 §10](15-api-de-mods.md#10-versionamento-e-estabilidade)) |
| `determinism` | Todo PR | Mesma demo com 0, 1, 3 e 15 workers e hash idêntico |
| `render-golden` | Todo PR | Imagens de referência com renderer de software (lavapipe) |
| `parity` | Noturno | Suíte de paridade em *runner* próprio com cópia legítima do jogo ([18](18-testes-e-qualidade.md#testes-de-paridade)) |
| `bench` | Noturno | Benchmarks em máquina dedicada. Regressão acima do limiar falha |
| `fuzz` | Noturno | Parsers (BSP, MDL, WAD, SPR, WAV), decodificador de rede e `rookc` |
| `stress` | Semanal | Servidor com 1024 clientes simulados |
| `package` | Tag de release | Binários, `THIRD_PARTY_NOTICES.md`, símbolos, imagem Docker do `nextds` |

## 8. Fluxo de trabalho

- **Trunk-based:** `main` protegida. Mudanças entram por PR com revisão e CI verde.
- **Conventional Commits** (`feat(ecs): ...`, `fix(net): ...`) e changelog gerado.
- **SemVer** para o projeto e, separadamente, para a API de plugins.
- Commits com *Signed-off-by* (DCO), por causa da política de clean room
  ([20](20-legal-e-licenciamento.md#3-clean-room)).

## 9. Ambiente de desenvolvimento

- **VS Code** com `clangd`, CMake Tools e a extensão Rook (sintaxe, LSP, depurador). O
  repositório traz `.vscode/` com configurações recomendadas.
- **Visual Studio** e **CLion** suportados via CMake presets.
- **RenderDoc** para captura de frames e **Tracy** para profiling.
- Hooks de *pre-commit* opcionais: formatação e lint dos arquivos alterados.
