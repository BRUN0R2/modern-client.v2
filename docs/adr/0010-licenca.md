# ADR-0010 — Licença do projeto

- **Status:** Aceito (opção A, com a Rook Plugin Exception). A revisão jurídica antes do
  primeiro release continua pendente.
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

**Opção A**, escolhida pelo mantenedor, com uma permissão adicional para plugins:

| Componente | Licença | Onde |
|------------|---------|------|
| Engine, jogo, servidor dedicado, ferramentas, dados de jogo, build | **GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception** | Todo o repositório, exceto o listado abaixo |
| Toolchain Rook (`rookc`, VM, LSP, CLI) e SDK de plugins (biblioteca `std` da Rook, declarações `.rookd` geradas) | **MIT** | `rook/` e os `.rookd` gerados pelo build |
| Documentação | **CC BY 4.0** | `docs/`, `README.md`, `TODO.md` |

### Rook Plugin Exception

[`LICENSES/LicenseRef-Rook-Plugin-Exception.txt`](../../LICENSES/LicenseRef-Rook-Plugin-Exception.txt)
é uma **permissão adicional da seção 7 da GPL-3.0**, no estilo das exceções de runtime de
compiladores (GCC Runtime Library Exception, Classpath Exception):

- **Concede:** plugins e mods que interagem com a engine **apenas pela interface de
  plugins** (bytecode Rook, manifestos, declarações `.rookd`, biblioteca `std` e as mensagens
  trocadas por elas) podem usar qualquer licença. Eles não viram obra derivada da engine só
  por serem carregados e executados por ela.
- **Não concede:** nada para versões modificadas da engine, nem para extensões nativas
  (código que fala com a engine por outro meio, como a futura interface de extensões em
  C++), nem para código da engine copiado para dentro de um plugin. Tudo isso continua sob a
  GPL-3.0.
- **Modificações:** quem modifica a engine pode manter ou remover a exceção da sua versão,
  como a seção 7 permite.
- **Por que agora:** só os titulares dos direitos podem acrescentar uma exceção. Decidir
  antes das contribuições externas evita ter que pedir o consentimento de cada contribuidor
  depois.
- O identificador é `LicenseRef-` porque a especificação REUSE 3.3 exige esse prefixo para
  licenças e exceções que não estão na lista SPDX.

### Aplicação no padrão [REUSE](https://reuse.software)

- Textos em `LICENSES/`: `GPL-3.0-or-later.txt`, `LicenseRef-Rook-Plugin-Exception.txt`,
  `MIT.txt` e `CC-BY-4.0.txt`.
- `LICENSE` na raiz com o texto da GPL, para que o GitHub e outras ferramentas reconheçam a
  licença principal.
- `REUSE.toml` com a licença de cada diretório. `reuse lint` passa (REUSE 3.3) e roda no CI
  (job `checks`).
- Cabeçalho SPDX em todo arquivo de código:

```cpp
// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception
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
- ✅ Autores de plugins e mods escolhem a licença, garantido pelo SDK em MIT e pela exceção
  escrita na própria licença da engine, não só pela intenção nos docs.
- ⚠️ Código de projetos sob licenças incompatíveis com a GPL-3.0 não pode ser incorporado.
- ⚠️ O texto da exceção (versão 1.0) foi redigido pelo projeto e ainda não passou por
  revisão jurídica. Ajustes de redação podem gerar uma versão 1.1.

## Pendências

- [ ] Revisão jurídica do texto da Rook Plugin Exception, da fronteira de plugins e da
  política de clean room antes do primeiro release.
- [ ] Decidir a licença do SDK de extensões nativas quando ele existir (hoje: GPL, sem
  exceção).
