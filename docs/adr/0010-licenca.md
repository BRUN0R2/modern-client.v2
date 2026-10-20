# ADR-0010 — Licença do projeto

- **Status:** Aceito (opção A). A revisão jurídica antes do primeiro release continua pendente.
- **Data:** 2026-10-20

## Contexto

O projeto é de código aberto e público. Ele precisa de uma licença que proteja a engine, mas
que permita plugins e mods com qualquer licença (inclusive fechada) e seja compatível com as
dependências ([17 §5](../17-build-e-ferramentas.md#5-dependências)).

## Opções avaliadas

| Componente | Opção A (copyleft forte) | Opção B (copyleft fraco) |
|------------|--------------------------|--------------------------|
| Engine, jogo, servidor | GPL-3.0-or-later | MPL-2.0 |
| Toolchain Rook e SDK de plugins | MIT | MIT |
| Documentação | CC BY 4.0 | CC BY 4.0 |

## Decisão

**Opção A**, escolhida pelo mantenedor:

| Componente | Licença | Onde |
|------------|---------|------|
| Engine, jogo, servidor dedicado, ferramentas, dados de jogo, build | **GPL-3.0-or-later** | Todo o repositório, exceto o listado abaixo |
| Toolchain Rook (`rookc`, VM, LSP, CLI) e SDK de plugins (biblioteca `std` da Rook, declarações `.rookd` geradas) | **MIT** | `rook/` e os `.rookd` gerados pelo build |
| Documentação | **CC BY 4.0** | `docs/`, `README.md`, `TODO.md` |

Aplicação, no padrão [REUSE](https://reuse.software):

- Textos das licenças em `LICENSES/` (`GPL-3.0-or-later.txt`, `MIT.txt`, `CC-BY-4.0.txt`).
- `LICENSE` na raiz com o texto da GPL, para que o GitHub e outras ferramentas reconheçam a
  licença principal.
- `REUSE.toml` com a licença de cada diretório.
- Cabeçalho SPDX em todo arquivo de código:

```cpp
// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
```

- Contribuições entram pela licença do arquivo alterado, com *Signed-off-by* (DCO,
  [20 §3](../20-legal-e-licenciamento.md#3-clean-room)). O titular é "Modern-Client.v2
  contributors": cada contribuidor mantém o direito autoral sobre o que escreveu.

## Compatibilidade das dependências

Todas as dependências previstas em [17 §5](../17-build-e-ferramentas.md#5-dependências)
usam licenças permissivas compatíveis com a GPL-3.0: Zlib, MIT, BSD-2/3-Clause, ISC,
Apache-2.0 (com ou sem exceção LLVM), CC0 e domínio público. Uma dependência nova precisa
passar por essa verificação antes de entrar ([17 §5](../17-build-e-ferramentas.md#5-dependências)).

## Consequências

- ✅ Forks e redistribuições da engine precisam manter o código aberto sob a GPL.
- ✅ O SDK em MIT deixa autores de plugins e mods livres para escolher a licença.
- ⚠️ A fronteira entre a engine (GPL) e plugins Rook precisa ficar clara. Plugins usam só a
  API pública do SDK (MIT) e rodam como bytecode na VM. A revisão jurídica deve confirmar se
  essa fronteira basta ou se convém declarar uma **permissão adicional** para plugins
  (seção 7 da GPL-3.0), no estilo das exceções de runtime de compiladores.
- ⚠️ Código de projetos sob licenças incompatíveis com a GPL-3.0 não pode ser incorporado.

## Pendências

- [ ] Revisão jurídica da fronteira de plugins e da política de clean room antes do primeiro
  release.
- [ ] Verificação automática no CI (`reuse lint`).
