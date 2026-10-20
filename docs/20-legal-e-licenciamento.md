# 20 — Legal e licenciamento

> Este documento registra a **política do projeto**. Não é aconselhamento jurídico. Antes do
> primeiro release público, o conteúdo deve ser revisado por alguém com formação jurídica
> (item do [TODO](../TODO.md)).

## 1. Marcas

- Counter-Strike, Half-Life, Valve, Steam e logotipos associados são marcas da Valve
  Corporation. O projeto **não é afiliado** à Valve.
- O nome do projeto, **Modern-Client.v2**, não usa marcas da Valve. "Counter-Strike" e
  "CS 1.6" aparecem só de forma descritiva (o jogo cuja jogabilidade é recriada).
- O projeto não usa logotipos, ícones nem identidade visual da Valve.

## 2. Assets originais

- O repositório e os binários distribuídos **não contêm** nenhum asset da Valve: mapas,
  modelos, texturas, sons, sprites, textos ou configs originais.
- O jogo exige uma **cópia legítima** do Counter-Strike 1.6 instalada (Steam) e lê os
  arquivos diretamente dela ([06](06-assets-e-recursos.md#23-localização-da-instalação-original)).
- Testes automatizados usam assets **sintéticos** criados pelo projeto. A suíte de
  paridade roda em *runner* próprio com cópia licenciada, e o que é versionado são apenas
  **dados numéricos** de comportamento ([18](18-testes-e-qualidade.md#testes-de-paridade)).
- No futuro, um pacote de assets livres feito pela comunidade poderá permitir jogar sem o
  original. Esse pacote precisa de licença própria e não pode derivar de assets da Valve.

## 3. Clean room

O código é **escrito do zero**. Regras para contribuições:

| Fonte | Política |
|-------|----------|
| Código-fonte vazado da engine GoldSrc | **Proibido** consultar ou copiar. Contribuições suspeitas são rejeitadas |
| Half-Life SDK | Licença própria e restritiva. **Não copiar** código. Comportamento é reimplementado a partir de observação e testes |
| ReHLDS, ReGameDLL_CS, Xash3D, Quake/QuakeWorld (GPL) | **Não copiar** código. Consulta para entender comportamento é permitida, mas a implementação é própria e validada por testes de paridade contra o original |
| Documentação pública, especificações de formato e engenharia reversa por observação | Permitido, com referência no código |
| Bibliotecas de terceiros | Só via dependência declarada e com licença compatível ([17 §5](17-build-e-ferramentas.md#5-dependências)) |

- Todos os commits têm *Signed-off-by* (DCO), em que o contribuidor declara ter o direito
  de enviar o código.
- Formatos de arquivo (BSP, MDL, WAD, SPR) são implementados a partir de especificações
  públicas e de análise dos próprios arquivos.

## 4. Licença do projeto

> **Decidido** em [ADR-0010](adr/0010-licenca.md) (opção A). Licenças aplicadas no padrão
> [REUSE](https://reuse.software): textos em `LICENSES/`, mapa por diretório em `REUSE.toml`.

| Componente | Licença | Motivo |
|------------|---------|--------|
| Engine, jogo, servidor, ferramentas e build | **GPL-3.0-or-later** | Garantir que melhorias na engine voltem para a comunidade |
| Toolchain Rook (`rookc`, VM, LSP) e SDK de plugins (`.rookd`, `std`) | **MIT** | Permitir plugins e mods com **qualquer licença**, inclusive fechada |
| Documentação (`docs/`, `README.md`, `TODO.md`) | **CC BY 4.0** | Reuso com atribuição |
| Dados de jogo (`data/`) | GPL-3.0-or-later, como a engine | Os valores numéricos de comportamento são fatos medidos |

Todo arquivo de código começa com o cabeçalho SPDX da sua licença
([02 §2](02-padroes-de-codigo.md#2-organização-de-arquivos-e-módulos)).

A fronteira da API de plugins (bytecode + `.rookd`) é desenhada para que mods **não sejam
obra derivada** da engine. A revisão jurídica deve confirmar isso e avaliar uma permissão
adicional explícita para plugins (seção 7 da GPL-3.0).

## 5. Mods

- Autores de mods detêm os direitos dos próprios mods e escolhem a licença.
- Mods distribuídos pelo portal oficial (futuro) precisam declarar licença no `mod.toml`
  e garantir que têm direito sobre os assets incluídos.

## 6. Privacidade

- O cliente não coleta telemetria por padrão. Crash reports são opcionais, sem dados
  pessoais.
- Servidores registram IP e `PlayerId` em logs, como o original registrava SteamID. A
  documentação do `nextds` orienta operadores sobre retenção de logs e legislação
  aplicável (LGPD/GDPR).
- A identidade do jogador é uma chave local. Nenhum serviço central é necessário para
  jogar.
