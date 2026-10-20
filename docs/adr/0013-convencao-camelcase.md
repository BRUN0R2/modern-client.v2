# ADR-0013 — Convenção camelCase em todo o projeto (C++ e Rook)

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O C++ do projeto já usava camelCase para funções e variáveis, mas a primeira versão da
especificação Rook seguia o `snake_case` dos natives do AMXX (`get_weapon_var`,
`plugin_precache`). Duas convenções no mesmo projeto dificultam a leitura, as ferramentas e
a documentação. O mantenedor decidiu usar camelCase em todo o projeto.

## Decisão

- Funções, métodos, variáveis, parâmetros e campos usam **camelCase** no C++ e na Rook,
  incluindo a API de plugins e as chaves dos catálogos `vars`: `getWeaponVar`,
  `setEntityVar`, `precacheModel`, `pluginPrecache`, `"maxClip"`.
- **Arquivos e diretórios** também: `contractsTests.cpp`, `enumArray.cppm`, `rhiVulkan/`,
  `devShell.ps1`, `infectionTests.rook`. O mesmo vale para alvos de build, executáveis e
  funções CMake do projeto (`nxCoreTests`, `rookLsp`, `nxAddTest`).
- Tipos e valores de enum continuam em PascalCase. Constantes seguem o idioma de cada
  linguagem (`kPascalCase` no C++, `SCREAMING_SNAKE_CASE` na Rook).
- Exceções explícitas: cvars e comandos de console (`mp_roundtime`), classnames de mapa
  (`func_buyzone`), nomes exigidos por ferramentas (`CMakeLists.txt`, triplets do vcpkg),
  arquivos convencionais de repositório, os documentos de `docs/`, arquivos do jogo
  original, chaves de tradução e nomes impostos por terceiros
  ([02 §3.1](../02-padroes-de-codigo.md#31-regra-do-projeto-camelcase)).

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| `snake_case` na Rook, camelCase no C++ | Duas convenções no mesmo projeto |
| `snake_case` em tudo | Contraria a escolha do mantenedor e o C++ já escrito |
| Renomear cvars para camelCase | Quebraria `server.cfg`, binds e ferramentas existentes (paridade de servidor) |

## Consequências

- ✅ Uma única convenção, verificada por `clang-tidy` e pelo lint `naming` do `rookc`.
- ✅ A migração continua mecânica: `set_props_var` → `setPropsVar`, feita pelo `rook migrate`.
- ⚠️ Plugins migrados mudam de grafia em relação ao AMXX. A tabela de equivalências de
  [16](../16-migracao-amxx.md) cobre os nomes.
