# 06 — Assets e recursos

> Decisão sobre assets originais em [ADR-0009](adr/0009-assets-originais-do-usuario.md).

## 1. Objetivos

- Carregar **diretamente** os arquivos originais do CS 1.6 da instalação do usuário (BSP,
  MDL, WAD, SPR, WAV, configs), sem conversão manual.
- **Acabar com o precache:** recursos ilimitados, registrados a qualquer momento, com
  download incremental para os clientes.
- Carregamento **assíncrono** em jobs, sem travar o frame.
- Formatos modernos para mods (glTF, KTX2, PNG, OGG/Opus, FLAC).
- Segurança: nenhum caminho arbitrário e nenhum arquivo não verificado vindo da rede.

## 2. Sistema de arquivos virtual (VFS)

### 2.1 Pontos de montagem

Ordem de busca (maior prioridade primeiro), equivalente ao *search path* da GoldSrc:

| Prioridade | Montagem | Conteúdo |
|------------|----------|----------|
| 1 | `mods/<mod>/` (na ordem de dependência) | Assets de mods habilitados |
| 2 | `downloads/` | Cache de recursos baixados (endereçado por conteúdo) |
| 3 | `addons/cstrike/` | Conteúdo customizado local do usuário (`cstrike_addon`) |
| 4 | `<instalação>/cstrike/` | Arquivos originais do Counter-Strike |
| 5 | `<instalação>/valve/` | Arquivos originais do Half-Life base |
| 6 | `engine/` (embutido) | Mínimo da engine: fonte do console, shaders de fallback, tela de erro |

### 2.2 Regras de caminho

- Caminhos lógicos são relativos, com `/` como separador e **busca sem diferenciar
  maiúsculas** (como no Windows, onde os mapas foram criados), mas com grafia canônica
  preservada para log.
- **Proibido:** `..`, caminhos absolutos, letras de unidade, `\\`, bytes de controle e
  extensões fora da lista permitida. Violação gera `VfsError::InvalidPath`.
- O índice de cada montagem é construído uma vez (com *watch* em desenvolvimento) e a
  busca é O(1) por hash do caminho normalizado.

### 2.3 Localização da instalação original

1. Opção explícita: `-basedir <caminho>` ou `game.toml`.
2. Steam no Windows: registro (`SteamPath`) mais `steamapps/libraryfolders.vdf`.
3. Steam no Linux: `~/.steam/steam` e `~/.local/share/Steam` mais `libraryfolders.vdf`.
4. Não encontrada: o primeiro start mostra um assistente para escolher a pasta.

A validação confirma a presença dos arquivos essenciais (`cstrike/liblist.gam`, WADs base,
modelos de jogador). Falta de arquivo gera `InstallError::MissingFile` com a lista exata.

## 3. Formatos suportados

### 3.1 GoldSrc (leitura direta)

| Formato | Uso | Observações |
|---------|-----|-------------|
| BSP v30 | Mapas | Lumps de geometria, lightmaps coloridas, PVS, hulls de clipnodes, lump de entidades, texturas embutidas |
| WAD3 | Texturas | `halflife.wad`, `cstrike.wad`, `decals.wad`, `tempdecal.wad` (sprays), WADs referenciados pela chave `wad` do `worldspawn` |
| MDL v10 | Modelos de estúdio | Ossos, sequências, *blends*, hitboxes, anexos, eventos, texturas (incluindo `*T.mdl`) e grupos de sequência (`*01.mdl`) |
| SPR v2 | Sprites | HUD, efeitos, fumaça, crosshair |
| WAV (PCM 8/16 bits) | Sons | Pontos de *cue* para loop. Taxas originais (11/22/44 kHz) |
| `sentences.txt` | Frases de rádio e falas | Composição de palavras com parâmetros de pitch/volume |
| `materials.txt` | Tipo de material por textura | Sons de passos e penetração de bala |
| `titles.txt` | Mensagens de texto do jogo | Localizável |
| `hud.txt` e `sprites/*.txt` | Layout de sprites de HUD | Por resolução |
| `*.res` | Lista de recursos por mapa | Viram dependências do manifesto ([§6](#6-fim-do-precache-registro-de-recursos)) |
| `*_detail.txt` | Detail textures por mapa | Suporte opcional, como no CS 1.6 |
| `overviews/*.txt` + imagem | Mapa de visão geral do espectador | |
| `liblist.gam`, `*.cfg`, `mapcycle.txt`, `motd.txt` | Configuração | Ver [09](09-servidor-dedicado.md) e [12](12-interface-hud-e-console.md) |

### 3.2 Modernos (para mods)

| Formato | Uso |
|---------|-----|
| glTF 2.0 / GLB | Modelos com skinning de até 4 influências, animações, materiais |
| KTX2 (Basis Universal) | Texturas comprimidas, transcodificadas para o formato da GPU |
| PNG, TGA | Texturas e HUD |
| `.nxspr` | **Sprite moderno**: descritor TOML + quadros PNG/KTX2 em RGBA, qualquer resolução, mipmaps, tempo por quadro |
| OGG Vorbis, Opus, FLAC | Áudio (música e efeitos longos em streaming) |
| TTF/OTF | Fontes da UI |
| TOML | Dados, configs, manifestos |

### 3.3 Mapas sem limites de BSP (futuro)

O BSP v30 tem limites de formato (índices de 16 bits em clipnodes e marksurfaces, entre
outros). O suporte a mapas grandes vem de um formato estendido próprio (`.nxmap`), com
importação a partir das ferramentas de compilação existentes. Ver [TODO](../TODO.md).

### 3.4 Limites de conteúdo removidos

Os arquivos originais continuam sendo lidos como são, mas a **representação interna** de
modelos, sprites e texturas não tem tabelas de tamanho fixo. Assim, conteúdo novo (glTF,
`.nxspr`, KTX2) e mods não herdam os tetos da GoldSrc.

| Limite no original | Problema para mods | No Modern-Client.v2 |
|--------------------|-------------------|---------------------|
| **Attachments**: o cliente só usa 4 por entidade | Armas com muzzle, cápsula, mira, lanterna e efeitos extras não cabem | Quantidade ilimitada, acesso por **nome** ou índice (`getAttachment(entity, "muzzle")`) |
| **Body groups**: todas as combinações de submodelos empacotadas em um único `body`, transmitido com poucos bits | *Weapon packs* e modelos com muitas partes quebram no multiplayer | Seleção **por body part**, replicada separadamente (varint por parte). `body` legado em 64 bits só para compatibilidade |
| **Sprites**: SPR paletado (256 cores), alpha binário ou aditivo, tamanho limitado | HUD e efeitos sem qualidade, serrilhados | `.nxspr` com RGBA real, alpha suave, qualquer resolução suportada pela GPU, mipmaps, compressão e variantes por DPI |
| **Texturas de modelo e WAD** paletadas de 8 bits | Texturas pobres, sem alpha real | Texturas RGBA e compressão GPU (KTX2) em modelos glTF e pacotes de texturas de alta resolução |
| **Ossos por modelo** (128) e **vértices por submodelo** limitados | Modelos detalhados impossíveis | Paleta de ossos em storage buffer (sem teto de formato) e índices de vértice de 32 bits |
| **Bone controllers** (4 + boca) e **blends** (2) por entidade | Animação procedural limitada | Listas dinâmicas por entidade |
| **Skins** e **sequências** com tetos de formato | Variações limitadas | Listas dinâmicas |
| **Nome de textura de WAD** com até 15 caracteres | Nomes crípticos | Sem limite nos formatos novos (o WAD original continua com o limite dele) |
| **Caminhos de recurso** com até 64 caracteres | Estruturas de pasta rasas | Sem limite de formato. Teto de segurança nomeado e alto (`kMaxResourcePathBytes`) |

Regras:

- Quando um limite técnico real existe (tamanho máximo de textura da GPU, memória), ele é
  **consultado em runtime**. Conteúdo acima dele gera erro tipado (`AssetError::ExceedsDeviceLimit`)
  com o valor e o limite, nunca corte silencioso.
- A rede replica attachments, body parts, controllers e blends com **listas tipadas**
  (varint), nunca campos de largura fixa herdados do original
  ([08 §6.1](08-rede.md#61-snapshots)).
- O `nxbake` gera relatórios do conteúdo importado (ossos, vértices, tamanho de textura),
  para que autores vejam o custo do que estão criando.

## 4. Sistema de assets

### 4.1 Handles e estados

```cpp
auto model = assets.load<nx::assets::StudioModel>("models/player/gign/gign.mdl");
// model : nx::Result<AssetHandle<StudioModel>, AssetError>; the path is validated synchronously.

switch (assets.state(*model)) {
    case AssetState::Loading: /* ... */ break;
    case AssetState::Ready:   /* ... */ break;
    case AssetState::Failed:  /* assets.failure(*model) yields the typed cause */ break;
}
```

- `AssetHandle<T>` é tipado e geracional. Não existe handle "genérico" sem tipo na API
  pública.
- `load` **valida o caminho e a existência** de forma síncrona (consulta ao índice do VFS)
  e agenda o carregamento. Arquivo inexistente gera erro imediato, não um handle quebrado.
- Erros de decodificação (arquivo corrompido) levam o asset ao estado `Failed` com
  `nx::Failure<AssetError>` consultável e um evento de diagnóstico.

### 4.2 Pipeline de carregamento

```mermaid
flowchart LR
    A[Requisição] --> B[E/S assíncrona<br/>thread de E/S]
    B --> C[Decodificação<br/>job Low/Normal]
    C --> D[Dependências<br/>texturas, sequências]
    D --> E[Upload GPU<br/>anel de staging]
    E --> F[Ready]
```

- Assets com dependências (modelo → texturas → grupos de sequência) ficam `Ready` só
  quando todas as dependências estão prontas.
- Prioridade sobe automaticamente quando um asset é necessário no frame atual (ex.: um
  modelo de jogador que acabou de entrar no PVS).
- Contagem de referências com descarte adiado. Orçamentos de memória por categoria
  (`asset_budget_textures`, `asset_budget_sounds`) com despejo LRU de assets não
  referenciados.

### 4.3 Cozimento (*bake*)

- `nxbake` converte formatos de autoria em formatos de runtime otimizados (texturas com
  mipmaps e compressão GPU, malhas reordenadas, áudio reamostrado quando necessário).
- O cache fica em `cache/baked/<hash-do-conteúdo>-<versão-do-importador>`. Se o original
  ou o importador mudar, o cache é invalidado.
- Assets GoldSrc podem ser lidos sem *bake* (o caminho padrão para o conteúdo original).
  O *bake* é uma otimização.

### 4.4 Hot reload (desenvolvimento)

- O VFS observa as montagens. Arquivo alterado faz o asset ser recarregado e os
  dependentes serem notificados.
- Funciona para texturas, modelos, sons, shaders, dados de jogo (TOML) e plugins Rook.

## 5. Mapas

Carregar um mapa envolve:

1. Ler o BSP e validar os lumps.
2. Construir os **hulls de colisão** (clipnodes) e a árvore BSP de render.
3. Resolver texturas: embutidas no BSP ou nos WADs listados no `worldspawn`.
   Textura ausente gera erro de carga com o nome da textura e a lista de WADs tentados.
4. Empacotar lightmaps em atlas.
5. Ler o lump de entidades e instanciar prefabs ([03 §11](03-ecs.md#11-prefabs-e-entidades-de-mapa)).
6. Ler o `.res` do mapa e montar o manifesto de recursos.
7. Carregar os opcionais: detail textures, overview e malha de navegação (bots).

O carregamento roda como um grafo de jobs. A meta é carregar `de_dust2` em menos de
1 segundo a partir de cache quente.

## 6. Fim do precache: registro de recursos

### 6.1 O problema original

Na GoldSrc todo modelo, som e recurso genérico precisava ser declarado no *precache*,
dentro de tabelas de tamanho fixo (em torno de 512 entradas por tipo) e apenas na
inicialização do mapa. Isso obrigava os mods a cortar conteúdo.

### 6.2 Modelo novo

| Aspecto | GoldSrc | Modern-Client.v2 |
|---------|---------|------------------|
| Quantidade | Teto fixo por tipo | Sem teto (IDs de 32 bits por sessão) |
| Momento do registro | Só no precache do mapa | Qualquer momento: na carga do plugin, na troca de mapa ou durante a partida |
| Identificação na rede | Índice na tabela | `ResourceId` (varint) mais hash de conteúdo |
| Entrega ao cliente | Download antes de entrar | **Obrigatórios** antes de entrar. **Sob demanda** durante a partida |
| Integridade | Opcional (`mp_consistency`) | Hash de conteúdo sempre verificado |

### 6.3 Funcionamento

- O servidor mantém um **manifesto** `ResourceId → {caminho lógico, tipo, hash, tamanho,
  política}`.
- Ao conectar, o cliente recebe o manifesto e baixa os recursos com política
  `Required` (declarados na carga dos plugins e no `.res` do mapa).
- Recursos registrados durante a partida são anunciados por mensagem confiável
  `ResourceAdded`. O cliente os resolve em segundo plano: usa o cache local se o hash
  bater ou baixa se não houver.
- A lógica de jogo no servidor **nunca** depende de o cliente já ter carregado um recurso.

### 6.4 Recurso ainda não disponível no cliente

| Tipo | Comportamento explícito (sempre registrado em diagnóstico) |
|------|----------------------------------------------------------|
| Modelo de jogador | Usa o modelo **padrão do time** até carregar. Nunca fica invisível, por justiça competitiva |
| Outros modelos | Fica oculto até carregar, com contador no `asset_stats` |
| Som | Não toca até carregar. Sons marcados `Critical` (passos, tiros) são `Required` por regra |
| Sprite de HUD | Elemento não é desenhado e o plugin recebe evento `ResourceUnavailable` |

### 6.5 Consistência de arquivos

- Recursos podem exigir **consistência exata** (`consistency = "exact"`): o cliente prova
  que o arquivo local tem o hash esperado, como o `force_unmodified` do original. É útil
  para modelos de jogador e sprites de mira.
- Inconsistência gera desconexão com motivo tipado (`DisconnectReason::InconsistentFile`)
  e o nome do arquivo.

## 7. Download de recursos

| Canal | Quando | Detalhes |
|-------|--------|----------|
| Embutido (UDP, no canal confiável) | Padrão | Fragmentado, com taxa limitada por `sv_download_rate`, sem prejudicar o tráfego de jogo |
| HTTP(S) rápido | Se `sv_downloadurl` estiver configurado | URL endereçada por conteúdo (`<url>/<hash>`), compatível com CDN e cache |

Regras de segurança do cliente:

- O hash (BLAKE3) é verificado **antes** de o arquivo entrar no VFS.
- Lista de extensões permitidas por tipo de recurso. Executáveis e scripts fora do
  sandbox são recusados.
- Teto de tamanho por arquivo e por servidor (`cl_download_max_size`,
  `cl_download_max_total`).
- `cl_allowdownload 0/1`. Plugins de cliente seguem regras próprias
  ([13](13-sistema-de-mods.md#7-plugins-no-cliente)).
- O cache em `downloads/` é endereçado por hash, compartilhado entre servidores e limpo
  por LRU (`cl_download_cache_size`).

## 8. Pacotes

- Mods e conteúdos são distribuídos como **`.nxpak`**: contêiner com índice, entradas
  comprimidas com zstd, hash por entrada e manifesto assinado opcional (Ed25519).
- O VFS monta `.nxpak` sem extrair.
- `nxbake pack` gera pacotes reproduzíveis (mesma entrada, mesmos bytes).

## 9. Erros tipados (resumo)

| Enum | Exemplos |
|------|----------|
| `VfsError` | `InvalidPath`, `NotFound`, `MountFailed`, `ReadFailed` |
| `InstallError` | `NotFound`, `MissingFile`, `UnsupportedVersion` |
| `AssetError` | `DecodeFailed`, `DependencyFailed`, `BudgetExceeded`, `UnsupportedFormat` |
| `BspError`, `MdlError`, `WadError`, `SprError`, `WavError` | Erros de formato com lump/offset |
| `ResourceError` | `UnknownResource`, `HashMismatch`, `DownloadRejected`, `TooLarge`, `ExtensionNotAllowed` |

## 10. Diagnóstico

- `asset_stats`: assets por estado, memória por categoria, fila de carregamento e downloads.
- `asset_list [filtro]`, `asset_reload <caminho>`.
- `res_manifest`: manifesto atual do servidor com políticas e tamanhos.
