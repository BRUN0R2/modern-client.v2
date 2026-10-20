# 13 — Sistema de plugins e mods

> Decisões registradas em [ADR-0006](adr/0006-linguagem-de-plugins-propria-rook.md) e
> [ADR-0007](adr/0007-vm-propria-bytecode-tipado.md).

## 1. Princípio

O modelo que a comunidade de CS 1.6 conhece continua o mesmo: **o servidor carrega
plugins**, plugins conversam por **natives e forwards**, e conjuntos de plugins formam um
**mod**. O que muda é a tecnologia por baixo: linguagem fortemente tipada, compilador
moderno, isolamento por plugin, API nativa da engine (sem fakemeta, offsets ou Orpheu),
hot reload e plugins no cliente.

## 2. Vocabulário e equivalências

| AMX Mod X | Modern-Client.v2 | Observação |
|-----------|------------------|------------|
| `.sma` (fonte Pawn) | `.rook` (fonte Rook) | [14](14-linguagem-rook.md) |
| `amxxpc` | `rookc` | Compilador próprio, incremental, com LSP |
| `.amxx` (plugin compilado) | `.rbc` (Rook bytecode) | Verificado na carga |
| `plugins.ini` | `server/plugins.toml` | Habilita mods e plugins avulsos |
| `configs/*.ini`/`.cfg` | `configs/` do mod + cvars | Mesmo papel |
| Módulos (fakemeta, hamsandwich, ReAPI, engine) | API nativa da engine | Tipada e estável ([15](15-api-de-mods.md)) |
| `register_native` / natives de plugin | Funções `pub` exportadas por plugins | Chamadas tipadas entre plugins |
| `forward` / `CreateMultiForward` | `forward` declarado com `pub forward` | Assinatura verificada em compilação |
| `set_task` | `setTask` + coroutines (`await wait(1s)`) | Tempo de jogo determinístico |
| nVault / SQLx | `nx.storage` (KV e SQLite) | Por capability |
| Plugin de "API" (ex.: `ApiClasses.sma` do ReZombie) | **Plugin de API** | Exporta handles, `vars`, funções e forwards |
| Mod (ex.: ReZombie) | **Mod** (`mod.toml`) | Pacote com plugins, assets, locale e configs |
| Módulos C++ do AMXX / Metamod | **Extensões nativas** (futuro) | C ABI versionada, opt-in do admin ([§9](#9-extensões-nativas-futuro)) |

## 3. Estrutura de um mod

```
mods/rezombie/
├── mod.toml
├── plugins/
│   ├── api/
│   │   ├── classes.rook        # plugin de API: handles, vars, natives, forwards
│   │   ├── props.rook
│   │   └── weapons.rook
│   ├── class/
│   │   ├── human.rook
│   │   └── assassin.rook
│   ├── gamemodes/
│   │   └── nemesis.rook
│   └── dev/
│       └── runtime.rook
├── assets/                     # models/, sound/, sprites/ ... (mounted in the VFS)
├── locale/
│   ├── en.toml
│   └── pt-BR.toml
├── configs/
│   └── rezombie.cfg            # default cvars, executed on map start
└── tests/
    └── infectionTests.rook
```

### 3.1 `mod.toml`

```toml
[mod]
id       = "rezombie"
name     = "ReZombie"
version  = "0.4.0"
authors  = ["ReZombie Team"]
license  = "MIT"
api      = "^1.0"                 # Modern-Client.v2 plugin API range

[dependencies]
# "another-mod" = "^2.1"

[capabilities]                    # requested; the server admin grants them
storage = true

[[plugin]]
id     = "api.classes"
source = "plugins/api/classes.rook"
target = "server"

[[plugin]]
id     = "class.human"
source = "plugins/class/human.rook"
target = "server"

[[plugin]]
id     = "weapons.coldsteel"
source = "plugins/weapons/coldsteel.rook"
target = "shared"                 # runs on server and client (prediction)

[[plugin]]
id      = "dev.runtime"
source  = "plugins/dev/runtime.rook"
target  = "server"
profile = "debug"                 # only loaded with plugins debug profile
```

### 3.2 Ordem de carregamento automática

- A ordem **não é manual**. Ela vem do grafo de dependências: um plugin que `import`a
  algo de outro plugin (função, handle, ID de conteúdo, forward) depende dele.
- Exemplo do ReZombie: em vez de "coldsteel precisa vir antes de Class: Human" escrito à
  mão no manifesto, o `human.rook` importa o ID `COLDSTEEL` exportado por
  `coldsteel.rook`, e o compilador registra a dependência.
- Ciclos de dependência são erro de build (`R0601`) com o ciclo impresso.
- Empates são resolvidos de forma determinística (ordem alfabética do ID).

## 4. Configuração do servidor (`server/plugins.toml`)

```toml
# Equivalent of plugins.ini. Order here does not matter: dependencies decide it.

[[mod]]
id = "admin"                      # built-in admin system

[[mod]]
id      = "rezombie"
disable = ["dev.runtime"]         # turn off individual plugins
grant   = ["storage"]             # capabilities granted to this mod

[[plugin]]
path  = "plugins/welcome.rbc"     # standalone plugin outside any mod
debug = true                      # same idea as "plugin.amxx debug"
```

## 5. Ciclo de vida

| Forward | Quando | Uso típico |
|---------|--------|-----------|
| `pluginPrecache` | Antes do mapa iniciar, **em todos os plugins** antes de qualquer `pluginInit` | Registrar conteúdo (classes, armas, entidades) e recursos obrigatórios |
| `pluginInit` | Depois de todos os `pluginPrecache` | Registrar comandos, cvars, hooks dinâmicos e menus. Buscar conteúdo de outros plugins por nome |
| `pluginCfg` | Depois de `server.cfg` e configs do mod | Ler cvars já configuradas |
| `pluginPause` / `pluginUnpause` | `plugins pause/unpause` | Suspender efeitos |
| `pluginEnd` | Fim do mapa ou descarga | Salvar estado e liberar recursos externos |

- O precache **não tem mais limite**: recursos registrados em `pluginPrecache` viram
  downloads obrigatórios, e recursos registrados depois são entregues sob demanda
  ([06 §6](06-assets-e-recursos.md#6-fim-do-precache-registro-de-recursos)).
- Forwards retornam `Result`. Um `err` em `pluginPrecache`/`pluginInit` **falha a carga
  do plugin** com a mensagem tipada (equivalente moderno ao `set_fail_state`), e os
  plugins que dependem dele também não carregam, com o motivo.

## 6. Modelo de execução

### 6.1 Isolamento

- Cada plugin roda em um **isolate**: heap próprio (coleta de lixo própria), globais
  próprias e fila de tarefas própria, como cada `.amxx` tinha a própria memória.
- Uma falha em um plugin **nunca** derruba o servidor nem corrompe outro plugin.

### 6.2 Ordem e threads

- Forwards e hooks são despachados na thread de simulação, em **ordem determinística**:
  prioridade do hook e depois ordem de dependência dos plugins.
- **Sistemas ECS** de plugins diferentes, sem conflito de acesso, rodam **em paralelo**
  nos workers ([03 §13](03-ecs.md#13-integração-com-plugins-rook)).
- Funções `pure` podem rodar como jobs (`runJob`) em paralelo, sem acesso ao mundo
  ([14](14-linguagem-rook.md#17-concorrência-e-determinismo)).

### 6.3 Chamadas entre plugins

- Funções `pub` de um plugin são chamadas por outro como se fossem natives, mas com
  tipos verificados em compilação e na carga (hash de assinatura).
- Valores atravessam isolates por **cópia** (primitivos, unidades, enums, structs,
  listas) ou como **handle** (entidades, classes, armas). Strings são imutáveis e
  compartilhadas sem cópia.
- Reentrância (A → B → A) é permitida, com profundidade máxima configurável.

### 6.4 Orçamentos e falhas

| Limite | Cvar | Padrão |
|--------|------|--------|
| CPU por callback | `rook_budget_ms` | Conservador, ajustável por plugin |
| CPU por tick por plugin | `rook_tick_budget_ms` | Fração do tick |
| Memória por plugin | `rook_heap_limit_mb` | Ajustável por plugin |
| Profundidade de chamada | `rook_max_call_depth` | Limite de pilha da VM |

Ao exceder um limite, ou em `panic`, o callback é **abortado** com uma falha tipada
(`PluginFault.BudgetExceeded`, `PluginFault.HeapLimit`, `PluginFault.Panic{message}`,
`PluginFault.StackOverflow`), com *stack trace* Rook completo no log. A política vem de
`rook_fault_policy`:

| Política | Efeito |
|----------|--------|
| `pause` (padrão) | Plugin pausado e admins avisados |
| `reload` | Recarrega o plugin (limitado por janela de tempo) |
| `continue` | Só registra (desenvolvimento) |

## 7. Plugins no cliente

| Alvo | Roda em | Exemplos |
|------|---------|----------|
| `server` | Servidor | Regras, modos, admin, economia |
| `client` | Cliente | HUD customizado, menus, efeitos locais |
| `shared` | Ambos | **Armas customizadas com predição**, movimento customizado |

- Plugins `client`/`shared` são enviados pelo servidor como recurso (hash verificado,
  [06 §7](06-assets-e-recursos.md#7-download-de-recursos)). Eles **acabam com o "lag"
  de armas customizadas**, que no AMXX rodavam só no servidor.
- **Sandbox estrito no cliente:** sem sistema de arquivos (exceto armazenamento local por
  servidor com cota), sem rede (exceto mensagens tipadas com o próprio servidor), sem
  leitura de entrada fora das próprias telas e com orçamentos menores.
- Os módulos permitidos no cliente são verificados na carga. Importar um módulo só de
  servidor em plugin `client` é erro de compilação.
- O jogador controla com `cl_plugins 1/0`. O servidor pode exigir plugins de cliente e
  recusar a entrada com motivo claro.

## 8. Hot reload

```text
] plugins reload rezombie/class.assassin
[rook] compiling rezombie/class.assassin ... ok (12 ms)
[rook] migrated 3 persistent globals, reset 0
[rook] rezombie/class.assassin reloaded
```

1. Compila (se o fonte mudou) e verifica o bytecode novo.
2. Chama `pluginEnd` na versão antiga.
3. Migra as globais `persist var` cujo nome e tipo coincidem. Incompatíveis são
   reiniciadas, com aviso listando cada uma.
4. Re-registra hooks, comandos, forwards e cvars. O conteúdo registrado (classes, armas)
   é **atualizado** pelo mesmo ID.
5. Chama `pluginInit` e `pluginCfg`.

Em desenvolvimento, `rook_watch 1` recarrega automaticamente ao salvar o arquivo.

## 9. Extensões nativas (futuro)

Para casos que realmente exigem C++ (integração com sistemas externos, algoritmos
pesados), existirá uma **C ABI versionada** (`mcv2_ext_v1`):

- Somente no servidor, **desligada por padrão** e habilitada explicitamente pelo admin.
- A extensão declara suas funções em uma IDL, da qual o `rookc` gera as declarações
  tipadas.
- Fora do sandbox: o servidor avisa no boot quais extensões estão carregadas.

## 10. Capabilities

| Capability | Libera |
|------------|--------|
| `storage` | `nx.storage` (KV e SQLite por mod) |
| `http` | `nx.http` (saída para hosts permitidos em `plugins.toml`) |
| `files` | Leitura e escrita em `data/<mod>/` |
| `serverCommand` | `serverCmd` (executar comandos de console do servidor) |
| `clientPlugins` | Enviar plugins `client`/`shared` |
| `nativeExtensions` | Chamar extensões nativas |

Um plugin que importa um módulo sem a capability concedida **não carrega**
(`CapabilityError.NotGranted{capability, mod}`), em vez de falhar no meio da partida.

## 11. Ferramentas

| Comando | Função |
|---------|--------|
| `rook new <tipo>` | Gera plugin, mod ou plugin de API a partir de template |
| `rook build` | Compila o mod (incremental) |
| `rook check` | Só verificação de tipos, rápida (para o editor) |
| `rook test` | Roda testes `@test` com um servidor simulado *headless* |
| `rook fmt` / `rook lint` | Formatação canônica e lints |
| `rook doc` | Documentação HTML a partir de `///` |
| `rook pack` | Gera `.nxpak` reproduzível do mod |
| `rook migrate <arquivo.sma>` | Conversão assistida de Pawn para Rook ([16](16-migracao-amxx.md)) |
| `rookLsp` | Language server: autocompletar (inclusive chaves de `vars`), ir para definição, renomear, diagnósticos ao digitar |
| Depurador (DAP) | Breakpoints, passo a passo e inspeção no VS Code, conectado a um servidor local |

## 12. Mods embutidos (substitutos dos plugins padrão do AMXX)

Distribuídos com o jogo, escritos em Rook e servindo como exemplo de referência:

| Mod/plugin | Equivalente AMXX |
|------------|------------------|
| `admin` | `admin`, `admincmd`, `adminhelp`, `adminslots`, `adminchat` |
| `admin.menus` | `menufront`, `cmdmenu`, `plmenu`, `telemenu`, `mapsmenu` |
| `votes` | `adminvote`, `mapchooser` |
| `maps` | `nextmap`, `timeleft` |
| `chat` | `antiflood`, `scrollmsg`, `imessage` |
| `stats` | `statsx`, `statscfg` |
| `pausecfg` | `pausecfg` |

O sistema de admin usa `server/admins.toml` com permissões **tipadas** (`Permission.Kick`,
`Permission.Ban`, `Permission.Map`…) em vez das letras de flag, e com importação
automática de `users.ini`.

## 13. Erros tipados

| Enum | Exemplos |
|------|----------|
| `PluginLoadError` | `CompileFailed`, `VerifyFailed`, `DependencyMissing{plugin}`, `DependencyFailed`, `ApiVersionMismatch{required, available}` |
| `PluginFault` | `BudgetExceeded`, `HeapLimit`, `Panic{message}`, `StackOverflow`, `StaleHandle` |
| `CapabilityError` | `NotGranted{capability, mod}`, `NotAllowedOnClient` |
| `ManifestError` | `InvalidField{path, line}`, `DuplicatePluginId`, `UnknownTarget` |
| `ReloadError` | `IncompatibleContent{id}`, `ReloadLoopDetected` |
