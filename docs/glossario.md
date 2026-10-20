# Glossário

| Termo | Definição |
|-------|-----------|
| **A2S** | Protocolo de consulta de servidores da Valve (`A2S_INFO`, `A2S_PLAYER`, `A2S_RULES`), usado por server browsers e ferramentas de monitoramento |
| **Arquétipo** | Conjunto exato de tipos de componente. Entidades com o mesmo conjunto são armazenadas juntas ([03](03-ecs.md)) |
| **Attachment** | Ponto nomeado de um modelo (muzzle, ejeção de cápsula) usado para efeitos e anexos |
| **Baseline** | Estado de referência para codificação delta na rede |
| **Bind group** | Conjunto de recursos de GPU ligados juntos a um pipeline, unidade de binding da RHI |
| **Body group / body part** | Parte de um modelo de estúdio com submodelos alternativos (ex.: arma com ou sem silenciador) |
| **BSP** | Formato de mapa da GoldSrc (v30): geometria, lightmaps, visibilidade, hulls e entidades |
| **Capability** | Permissão concedida pelo administrador a um mod (`storage`, `http`…) |
| **Chunk** | Bloco de memória de 16 KiB de um arquétipo, com colunas SoA |
| **Clipnode** | Nó da árvore de colisão do BSP para um hull específico |
| **Command buffer** | Fila de mudanças estruturais do ECS aplicada em pontos de sincronização |
| **Compensação de lag** | Rebobinar alvos no servidor para o instante que o atirador viu |
| **Cvar** | Variável de console configurável (`mp_roundtime`, `sensitivity`) |
| **Edict** | Entidade no modelo da GoldSrc, limitada por um teto fixo. Substituída por `Entity` |
| **Efêmero (handle)** | Handle Rook válido só durante o callback atual. Para guardar, use `Ref` |
| **Extract** | Cópia do estado de simulação necessário para o render do frame |
| **Forward** | Função de plugin chamada pela engine ou por outro plugin em um evento (`pluginInit`, `roundStart`) |
| **GoldSrc** | Engine original do Half-Life e do Counter-Strike 1.6 |
| **Hammer unit (`hu`)** | Unidade de distância dos mapas (≈ 1 polegada) |
| **Handle** | Identificador opaco, tipado e geracional para um objeto |
| **Hitbox / hitgroup** | Volume de colisão de bala ligado a um osso e a região do corpo que define o multiplicador de dano |
| **Hook** | Interceptação de uma ação do jogo, com fase pré (pode alterar ou bloquear) e pós |
| **Hull** | Caixa de colisão usada nos traces (ponto, em pé, agachado, grande) |
| **Isolate** | Ambiente de execução de um plugin Rook: heap, globais e tarefas próprias |
| **Job** | Unidade de trabalho executada pelo Job System em um worker |
| **Lightmap** | Textura de iluminação pré-calculada das superfícies do BSP |
| **Lightstyle** | Padrão animado de intensidade de luz (piscar, pulsar) aplicado a lightmaps |
| **MDL** | Formato de modelo de estúdio da GoldSrc (v10) |
| **Mod** | Pacote distribuível com plugins, assets, traduções e configs (`mod.toml`) |
| **PAS** | *Potentially Audible Set*: regiões de onde um som pode ser ouvido |
| **Plugin** | Unidade carregável compilada pelo `rookc` (`.rbc`). Equivale a um `.amxx` |
| **Plugin de API** | Plugin que exporta handles, `vars`, funções e forwards para outros plugins |
| **Precache** | No original, registro obrigatório e limitado de recursos. Aqui, registro sem limite |
| **Predição** | Simular localmente o jogador antes da confirmação do servidor |
| **PVS** | *Potentially Visible Set*: regiões visíveis a partir de uma região do BSP |
| **RHI** | *Render Hardware Interface*: abstração sobre Vulkan/D3D11 |
| **Render graph** | Descrição dos passes do frame, da qual derivam barreiras, aliasing e paralelismo |
| **Rook** | Linguagem de plugins do projeto ([14](14-linguagem-rook.md)) |
| **`rookc`** | Compilador da linguagem Rook |
| **`.rookd`** | Arquivo de declarações da API (equivalente moderno ao `.inc`), gerado do C++ |
| **Snapshot** | Estado do mundo enviado do servidor ao cliente em um instante |
| **SPR** | Formato de sprite da GoldSrc (paletado). O equivalente moderno é `.nxspr` |
| **Tick** | Passo fixo da simulação do servidor (`sv_tickrate`) |
| **Usercmd** | Comando de entrada do cliente (movimento, botões, ângulos, duração) |
| **Varint** | Inteiro de tamanho variável: valores pequenos ocupam poucos bytes, sem teto fixo |
| **`vars` (catálogo)** | Declaração das propriedades de um handle, usada pelo padrão `get*Var`/`set*Var` com chaves verificadas em compilação |
| **WAD** | Arquivo de texturas da GoldSrc (WAD3) |
| **Work-stealing** | Estratégia em que workers ociosos roubam trabalho da fila de outros |
