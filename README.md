# Modern-Client.v2

Recriação de código aberto do Counter-Strike 1.6, escrita do zero em **C++26**, com
jogabilidade e servidores idênticos ao original e **sem os limites históricos** da GoldSrc.

- **Engine moderna:** ECS por arquétipos, Job System multithread, Vulkan (DirectX 11 no
  futuro) e GPU skinning.
- **Sem limites:** entidades, precache, jogadores (até 1024), mensagens, attachments,
  body groups, sprites e valores de HUD.
- **Plugins e mods nativos:** linguagem **Rook**, fortemente tipada e familiar para quem
  vem do AMX Mod X, com compilador próprio, hot reload e LSP.
- **Servidores como sempre:** mesmas cvars, configs, comandos, RCON e consultas A2S.

> Funciona sem nenhum asset da Valve. Quem tem uma cópia legítima do Counter-Strike 1.6
> (Steam) pode usar os assets originais: o jogo detecta a instalação e configura o perfil
> `classic` localmente ([ADR-0015](docs/adr/0015-conteudo-livre-e-assets-originais-opcionais.md)).
> Nenhum asset da Valve é distribuído. Projeto sem afiliação com a Valve Corporation.

## Status

Marco **M0 (Fundação)** em andamento: build com módulos C++ e `import std`, presets, vcpkg
e o módulo `nx.core` (erros tipados, contratos, tipos fortes e unidades) com testes. A
documentação de arquitetura está em [`docs/`](docs/README.md) e o backlog em
[`TODO.md`](TODO.md).

## Como compilar

Requisitos no Windows: Visual Studio 2026 com a carga "Desenvolvimento para desktop com
C++" (traz MSVC, Ninja, CMake e vcpkg). Detalhes e a matriz de toolchains estão em
[docs/17-build-e-ferramentas.md](docs/17-build-e-ferramentas.md).

```powershell
. .\scripts\devShell.ps1
cmake --preset dev-windows-msvc
cmake --build --preset dev-windows-msvc
ctest --preset dev-windows-msvc
```

No Linux, use os presets `dev-linux-gcc` ou `dev-linux-clang`.

## Licença

| Parte | Licença |
|-------|---------|
| Engine, jogo, servidor, ferramentas e dados | [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt) |
| Toolchain Rook e SDK de plugins (`rook/`) | [MIT](LICENSES/MIT.txt). Plugins e mods podem usar qualquer licença |
| Documentação (`docs/`, `README.md`, `TODO.md`) | [CC BY 4.0](LICENSES/CC-BY-4.0.txt) |

A licença de cada arquivo está no cabeçalho SPDX e em [`REUSE.toml`](REUSE.toml)
([REUSE](https://reuse.software)). Decisão e motivos: [ADR-0010](docs/adr/0010-licenca.md).
Contribuições usam *Signed-off-by* (DCO).
