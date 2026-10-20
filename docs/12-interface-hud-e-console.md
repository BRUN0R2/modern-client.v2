# 12 — Interface, HUD, console e entrada

## 1. Objetivos

- **HUD e menus visualmente idênticos** ao CS 1.6 por padrão: sprites de `hud.txt`,
  fontes, cores, posições, crosshair dinâmico, kill feed, radar, scoreboard e menus.
- **Console e configs 100% familiares:** `~`, `bind`, `alias`, `exec`, `config.cfg`,
  `autoexec.cfg`, `userconfig.cfg`, cvars de cliente clássicas.
- **Extensível por plugins** com API tipada para HUD, menus e telas, sem as limitações das
  mensagens de HUD do original.

## 2. Componentes de UI

| Camada | Tecnologia | Uso |
|--------|------------|-----|
| HUD clássico | Renderer próprio de sprites (SPR) e fontes bitmap | Vida, colete, dinheiro, munição, timer, radar, ícones de status, crosshair, kill feed |
| Menus e telas | Toolkit de UI retido próprio sobre `nx.render`, com layout declarativo e skins | Menu principal, opções, server browser, seleção de time e personagem, compra VGUI, scoreboard, MOTD |
| Menus de texto | Renderer do HUD | Menus clássicos de teclas (rádio, compra antiga, `showMenu` de plugins) |
| Console | Toolkit próprio | Console do jogo e do servidor de listen |
| Ferramentas de dev | Dear ImGui | Inspetores, profiler, editor de navegação (somente builds de desenvolvimento) |

> A escolha do toolkit de menus está registrada como pendente de validação no
> [TODO](../TODO.md). A alternativa avaliada é o RmlUi (HTML/CSS-like). O critério é
> fidelidade visual, desempenho e facilidade para plugins.

## 3. HUD

### 3.1 Elementos clássicos

| Elemento | Detalhe |
|----------|---------|
| Vida e colete | Ícones e números com as cores e o "flash" de dano do original |
| Dinheiro | Animação de ganho e perda |
| Munição | Clip e reserva por tipo de munição |
| Timer de round | Com ícone de bomba plantada |
| Ícones de status | Zona de compra, zona de bomba, kit de defuse, C4, zona de resgate, VIP |
| Radar | Pontos de aliados, bomba e reféns |
| Crosshair | `cl_crosshair_color`, `cl_crosshair_size`, `cl_crosshair_translucent`, `cl_dynamiccrosshair` |
| Kill feed | Ícone da arma e de headshot |
| Mensagens | Texto central, hints, `titles.txt`, `game_text`, mensagens de plugins |
| Barra de progresso | Plantio, defuse |
| Indicador de dano | Direção do dano recebido |
| Efeitos de tela | Flashbang, fade, NVG, zoom de sniper (overlay de mira) |

### 3.2 Escala e resolução

- O layout original (por resolução, via `hud.txt`) é reproduzido. Em resoluções altas,
  `hud_scale` aplica escala inteira, ou fracionária em `r_fidelity enhanced`, sem borrar
  sprites.
- Proporções largas (16:9, 21:9) mantêm a posição relativa original dos elementos.

### 3.3 Paridade visual de números

Valores dentro da faixa do original (vida até 100, colete até 100, dinheiro até 16000)
são desenhados **exatamente** como no CS 1.6: mesmos sprites de dígitos, espaçamento e
posição.

### 3.4 Sem truncamento de valores

No original, várias mensagens de HUD usavam campos de 1 byte: vida acima de 255 aparecia
errada, assim como munição de reserva e clip grandes. No Modern-Client.v2:

- Todo valor exibido no HUD usa o **mesmo tipo de domínio** do jogo da simulação até a
  tela: `Health` e `Armor` em ponto flutuante (como no original, exibidos como inteiro
  pela regra original), `Money` e `AmmoCount` em inteiros de 32 bits. Não existe campo
  "de HUD" menor que o dado.
- Na rede, esses campos são codificados como **varint**, então valores pequenos continuam
  ocupando poucos bits sem impor teto.
- Os contadores numéricos do HUD **crescem em dígitos** conforme necessário. Para manter
  a composição visual com números grandes, o layout empurra os elementos vizinhos, e
  `hud_number_style compact` (opcional) abrevia (`12.5k`).
- Teste de regressão obrigatório: vida 5000, 100000 e o máximo do tipo, colete acima de
  100, munição de reserva acima de 255 e dinheiro acima de 16000 (permitido por plugins).
  Todos desenhados corretamente, sem *wrap*.

### 3.5 HUD para plugins

Sem os limites históricos de canais de texto e tamanho de mensagem:

- **Textos de HUD** (`showHudText`), com posição, cor, efeito e duração tipados e quantos
  canais forem necessários.
- **Elementos persistentes:** barras, ícones e contadores que o plugin cria e atualiza
  por handle.
- **Menus de texto** (`showMenu`), com itens tipados e callback com o item escolhido como
  enum ou valor, nunca como número de tecla cru.
- **Telas completas** (layouts declarativos) para plugins de cliente
  ([13 §7](13-sistema-de-mods.md#7-plugins-no-cliente)).

## 4. Menus e telas do jogo

| Tela | Requisitos |
|------|------------|
| Assistente de primeiro start | Detecta a instalação do CS 1.6 e pergunta uma vez se o jogador quer usar os assets originais (perfil `classic`). Também permite escolher a pasta manualmente ou seguir sem eles ([06 §2.5](06-assets-e-recursos.md#25-configuração-local)) |
| Menu principal | Novo jogo (listen server), procurar servidores, opções (inclusive **Conteúdo**: perfil e fontes), sair. Música de fundo do conteúdo ativo (`media/gamestartup.mp3` no perfil `classic`) |
| Server browser | Internet (master servers), favoritos, histórico, LAN. Filtros por mapa, jogadores, ping, senha e plugins |
| Opções | Teclado (binds), mouse, áudio, vídeo, voz, multiplayer (nome, spray, crosshair) |
| Seleção de time e personagem | Teclas e layout originais |
| Compra (VGUI e por teclas) | [10 §8](10-gameplay-counter-strike.md#8-compra) |
| Scoreboard | Nome, frags, mortes, ping, status (morto, bomba, VIP), *mute* |
| MOTD | Texto ou HTML **restrito e seguro** (subconjunto renderizado pela UI, sem scripts). Links externos abrem no navegador do sistema após confirmação |

## 5. Console

- Abre com `~` (`toggleconsole`), com histórico, autocompletar de comandos e cvars (com
  descrição e faixa), cores por nível de log e busca.
- Mesma linguagem de comandos do original: separador `;`, aspas, `alias`, `+comando`/
  `-comando`, `wait`, `exec`, `echo`, `toggle`, `incrementvar`.
- **Erros explícitos:** comando ou cvar desconhecida, ou valor inválido, gera mensagem com
  sugestão ("did you mean...").
- **Conteúdo:** `content_list`, `content_scan`, `content_add <caminho>` e
  `content_profile <perfil>` ([06 §2.5](06-assets-e-recursos.md#25-configuração-local)).

### 5.1 Cvars de cliente preservadas (seleção)

| Grupo | Cvars |
|-------|-------|
| Rede | `rate`, `cl_updaterate`, `cl_cmdrate`, `cl_cmdbackup`, `ex_interp`/`cl_interp`, `cl_lw`, `cl_lc` |
| Vídeo | `fps_max`, `gamma`, `brightness`, `gl_vsync` (alias de `vid_vsync`), `fov` (com regras do original), `hud_draw`, `r_drawviewmodel` |
| HUD | `hud_fastswitch`, `hud_centerid`, `hud_saytext_time`, `cl_righthand`, `cl_crosshair_*`, `cl_dynamiccrosshair`, `net_graph` |
| Mouse | `sensitivity`, `m_rawinput`, `m_filter`, `m_pitch`, `m_yaw`, `zoom_sensitivity_ratio`, `m_customaccel` (desligado) |
| Áudio | `volume`, `bgmvolume`, `voice_enable`, `voice_scale` |
| Jogador | `name`, `_cl_autowepswitch`, `cl_autobuy`, `cl_rebuy`, `cl_minmodels` (respeitando o servidor) |

### 5.2 Arquivos de configuração do cliente

| Arquivo | Função |
|---------|--------|
| `config.cfg` | Gravado automaticamente (binds e cvars arquivadas) |
| `autoexec.cfg` | Executado no início |
| `userconfig.cfg` | Configuração pessoal do usuário |
| `video.toml`, `audio.toml` | Dispositivo e modo de vídeo e áudio (novo, estruturado) |

### 5.3 Módulo `nx.console`

A linguagem do console é a mesma no cliente e no servidor e vive no `nx.console` (camada 1,
só depende do `nx.core`). O console da UI (`nx.ui`) e o terminal do servidor são visões sobre
um `nx::console::Console`, que escreve respostas e falhas num `ConsoleOutput` e lê os arquivos
do `exec` por um `ConfigFiles` (o VFS, a partir do M2).

- **Um só espaço de nomes**, sem diferenciar maiúsculas, para cvars, comandos e aliases
  (`SV_GRAVITY 600` altera `sv_gravity`). Registrar um nome já usado falha com `NameTaken`.
- **Sintaxe dos `.cfg`:** um comando termina numa quebra de linha ou num `;` fora de aspas e
  fora de comentário. Aspas agrupam palavras, sem sequências de escape, e aspas sem fechamento
  vão até o fim do comando. `//` no início de uma palavra comenta o resto da linha, então
  `http://...` sem aspas continua uma palavra só. `
` e tabulação são espaço, e o BOM UTF-8 no
  início de um arquivo é ignorado.
- **Buffer de comandos:** `addText` enfileira, `insertText` passa à frente (aliases e `exec`
  rodam no ponto em que são chamados) e `execute()` roda até esvaziar ou até um `wait`, que
  deixa o resto para o próximo frame. Mais de 65 536 expansões de alias e `exec` numa mesma
  execução são um laço: o buffer é descartado e a falha `ExpansionLimit` é reportada.
- **Aliases:** o corpo é o resto das palavras, unidas por espaço. Argumentos depois do nome de
  um alias são ignorados, como no original. `alias nome` sozinho define um alias vazio, e
  `alias` lista todos.
- **Botões `+`/`-`:** `registerButton` cria `+nome` e `-nome`. Uma tecla (o número que o bind
  passa, `+jump 32`) segura o botão até soltar. Sem tecla, quem segura é o console, e `-nome`
  sem tecla solta tudo. As bordas (pressionado e solto desde a última leitura) não se perdem
  quando `+` e `-` chegam no mesmo frame, como num bind na roda do mouse.
- **Cvars tipadas:** `bool` (`0` ou `1`), inteiro de 32 bits (aceita `10.0` e `1e3`, recusa
  `10.5`), `float` finito, texto (sem aspas duplas nem quebras de linha, para que o
  `config.cfg` sempre releia o valor) e `enum` com `Count` (aceita o número ou o nome do
  enumerador e grava o número). Números podem ter faixa. Texto recusado nunca vira 0:
  `mp_freezetime: expected a whole number in [0, 60], got "abc"`.

| Flag | Efeito |
|------|--------|
| `Archive` | Entra no `config.cfg`: `Console::archivedCvars()` gera as linhas `nome "valor"` |
| `Notify` | Mudança anunciada aos jogadores e cvar listada no `A2S_RULES`. O console só marca; quem anuncia compara `revision()` |
| `Cheat` | O console só altera com cheats liberados (`Console::setCheats`, que segue o `sv_cheats` do servidor). Fora disso, `CheatProtected` |
| `ServerProtected` | O valor vem do servidor: o console nunca altera (`ServerProtected`), só o código. No cliente, é o caso das cvars de movimento replicadas para a predição |

O código lê e escreve pela alça tipada (`Cvar<float>`), que valida tipo e faixa, mas não as
flags. O `Console` pertence a uma thread (a principal, entre frames), e a simulação copia os
valores de que precisa no início do tick (`MovementSettings`).

Comandos embutidos: `alias`, `wait`, `exec`, `echo`, `toggle` (alterna entre 0 e 1 ou percorre
os valores dados) e `incrementvar` (soma o passo e dá a volta na faixa).

## 6. Entrada

- **SDL3** para teclado, mouse e gamepad.
- **Mouse raw** por padrão (`m_rawinput 1`), sem aceleração do SO. A sensibilidade é
  calibrada para que `sensitivity` tenha **o mesmo efeito em graus por contagem** que no
  original (validado por teste de paridade).
- Amostragem de entrada o mais tarde possível no frame ([05 §12](05-renderizacao.md#12-latência-e-ritmo-de-frame)).
- **Binds:** mesmos nomes de tecla do original (`MOUSE1`, `MWHEELUP`, `KP_END`, `SPACE`…),
  inclusive *bunny hop* com `MWHEELDOWN` em `+jump` (com o comportamento de comando
  instantâneo do original).
- Layouts de teclado não-QWERTY: binds por **posição física** opcional (`in_keymap scancode`).

## 7. Localização

- Strings da UI em arquivos por idioma (`locale/pt-BR.toml`, `locale/en.toml`), com chaves
  tipadas geradas para C++ e Rook. Uma chave inexistente é erro de build, não texto vazio.
- `titles.txt` e textos originais continuam sendo lidos e podem ser sobrepostos por
  tradução.
- Plugins têm os próprios arquivos de locale ([14](14-linguagem-rook.md#12-strings-formatação-e-tradução)).

## 8. Acessibilidade

- Escala de UI e de fonte do console independentes.
- Paletas de crosshair e de destaque para daltonismo (opcional, desligado por padrão para
  não alterar a paridade).
- Legendas opcionais para falas de rádio.

## 9. Erros tipados

| Enum | Exemplos |
|------|----------|
| `ConsoleError` | `UnknownCommand` (com sugestão), `UnknownCvar`, `InvalidValue`, `CheatProtected`, `ServerProtected`, `InvalidArguments`, `InvalidName`, `NameTaken`, `ExecFailed`, `ExpansionLimit`, `CommandFailed` |
| `CvarError` | `InvalidName`, `NameTaken`, `InvalidDefinition`, `InvalidValue`, `OutOfRange` |
| `ConfigFileError` | `NotFound`, `InvalidPath`, `Unreadable` |
| `UiError` | `LayoutInvalid`, `MissingSprite`, `UnknownLocaleKey` |
| `InputError` | `UnknownKeyName`, `DeviceUnavailable` |
