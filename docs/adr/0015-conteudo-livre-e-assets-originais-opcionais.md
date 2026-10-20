# ADR-0015 — Conteúdo livre por padrão; assets originais opcionais e configurados localmente

- **Status:** Aceito. Substitui o [ADR-0009](0009-assets-originais-do-usuario.md).
- **Data:** 2026-10-20

## Contexto

O ADR-0009 tornava a instalação do Counter-Strike 1.6 **obrigatória**: sem ela o jogo não
abria. Isso impede testar a engine no CI sem uma cópia do jogo, afasta contribuidores sem a
instalação e acopla o projeto a conteúdo de terceiros. Ao mesmo tempo, quem tem o jogo quer
jogar com os assets originais sem configurar nada à mão.

## Decisão

### 1. Fontes de conteúdo, nenhuma obrigatória

O jogo monta o conteúdo a partir de **fontes**, em camadas por prioridade, no VFS
([06 §2](../06-assets-e-recursos.md#2-sistema-de-arquivos-virtual-vfs)):

| Fonte (`kind`) | Origem | Redistribuível |
|----------------|--------|----------------|
| `engine` | Embutida no binário (fonte do console, shaders, tela de erro) | Sim |
| `devContent` | Gerada por código: texturas em grade, jogador em cápsula, armas simples, sons sintetizados | Sim |
| `openContent` | Pacote livre da comunidade, em repositório próprio | Sim |
| `goldsrcInstall` | Instalação local do CS 1.6 do usuário | **Não** |
| `mod` | Pacotes de mods | Conforme a licença do mod |

O jogo funciona com `engine` + `devContent` (ou `openContent`). Os assets originais são uma
fonte **opcional**, configurada na máquina do usuário.

### 2. Perfis prontos

| Perfil | Fontes | Uso |
|--------|--------|-----|
| `classic` | `goldsrcInstall` (`cstrike` e `valve`) | Experiência original: visual, sons, HUD e mapas do CS 1.6 |
| `open` | `openContent` | Jogo sem nenhum asset da Valve |
| `dev` | `devContent` | Desenvolvimento da engine e CI |
| `custom` | Lista explícita | Combinações próprias |

No perfil `classic`, a falta de um arquivo essencial é um **erro tipado** com a lista do que
falta. Não há preenchimento silencioso com outro conteúdo.

### 3. Detecção automática e configuração local

- No primeiro start (e com `content_scan`) a engine procura a instalação nas bibliotecas
  Steam do sistema (Windows e Linux) e a valida contra a lista de arquivos essenciais em
  `data/content/goldsrcCstrike.toml`.
- Se encontrar, o assistente pergunta uma vez se o usuário quer usar os assets originais e
  grava a escolha (perfil `classic`).
- A configuração mora **no perfil do usuário, fora do repositório**:
  `%APPDATA%\Modern-Client.v2\content.toml` (Windows) e
  `~/.config/modern-client.v2/content.toml` (Linux). O repositório traz só o
  `content.example.toml`.
- Também é possível configurar pela linha de comando (`--content classic`,
  `-basedir <caminho>`) e pelo console (`content_list`, `content_scan`,
  `content_add <caminho>`, `content_profile <perfil>`).

### 4. Conteúdo não redistribuível nunca sai da máquina

- Todo recurso servido carrega a fonte de onde veio. O serviço de download do servidor
  **recusa** enviar arquivos de fontes não redistribuíveis (`goldsrcInstall`).
- Um servidor que depende de uma fonte não redistribuível a declara (`requires`) nas regras
  A2S e no handshake. O cliente precisa tê-la localmente, verificada por hash. Se não tiver,
  a conexão é recusada com motivo tipado e instrução de como configurar.
- Assim, nem o projeto nem os servidores distribuem conteúdo da Valve, nem indiretamente.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Manter a instalação original obrigatória (ADR-0009) | Impede CI e contribuições sem o jogo e acopla o projeto a conteúdo de terceiros |
| Só conteúdo livre | Perderia a experiência original para quem tem o jogo |
| Distribuir os assets originais | Violação de direitos autorais |
| Completar o perfil `classic` com conteúdo livre quando faltar arquivo | Fallback silencioso: mistura visuais e esconde instalação quebrada |

## Consequências

- ✅ A engine roda e é testada de ponta a ponta sem nenhum asset externo.
- ✅ Quem tem o CS 1.6 joga com os assets originais quase sem configuração.
- ✅ A jogabilidade é a mesma em todos os perfis: a paridade está no código e nos dados.
  Muda só a apresentação.
- ⚠️ O `openContent` é um projeto de arte de longo prazo. Os assets precisam ser originais
  ou CC0/CC-BY, sem imitar a identidade visual da Valve (mapas próprios, sem réplicas).
- ⚠️ Mapas livres precisam de uma cadeia de ferramentas livre (editor TrenchBroom +
  compilador `.nxmap` do projeto), porque os compiladores clássicos de BSP30 derivam do SDK
  da Valve.
