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

> Requer uma cópia legítima do Counter-Strike 1.6 (Steam). Nenhum asset da Valve é
> distribuído. Projeto sem afiliação com a Valve Corporation.

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
