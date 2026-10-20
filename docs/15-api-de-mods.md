# 15 — API de plugins

## 1. Visão geral

A API é a **fachada estável** entre os plugins Rook e a engine/jogo. Princípios:

- **Desacoplada:** plugins nunca veem estruturas internas. A camada de bindings
  (`nx.script`) traduz nomes e tipos estáveis para o C++ interno, então refatorar a engine
  não quebra plugins.
- **Gerada, não escrita à mão:** funções e tipos C++ anotados
  (`[[=nx::script::Native{"precacheModel"}]]`) geram por reflexão os *thunks* de chamada
  e os arquivos de declaração **`.rookd`** (equivalentes aos `.inc`) usados pelo `rookc`
  e pelo LSP.
- **Familiar:** padrão `get*Var`/`set*Var`, forwards e hooks no estilo AMXX/ReAPI, com
  nomes em camelCase como todo o projeto (`getWeaponVar`, `precacheModel`).
- **Tipada de ponta a ponta:** sem índices crus, sem flags em string (`"ae"`), sem
  `message_begin`/`write_byte`.

## 2. Organização dos módulos

| Módulo | Conteúdo |
|--------|----------|
| `std.*` | Biblioteca padrão da linguagem: `Option`, `Result`, coleções, strings, matemática, unidades |
| `nx.plugin` | Metadados, pausa, informações de plugins |
| `nx.entity` | Criação, remoção, busca de entidades, `Ref`, componentes, `EntityMap` |
| `nx.resource` | `precacheModel`, `precacheSound`, `precacheSprite`, `precacheGeneric` |
| `nx.trace` | Traces de linha e de hull |
| `nx.fx` | Efeitos: beams, sprites, partículas, decals, luzes dinâmicas, shake, fade |
| `nx.sound` | `emitSound`, sons para um jogador, música |
| `nx.hud` | Textos de HUD, elementos persistentes, `clientPrint`, `clientPrintColor`, MOTD |
| `nx.menu` | Menus de texto e telas tipadas |
| `nx.command` | Comandos de cliente, console e servidor |
| `nx.cvar` | Cvars tipadas, cvars da engine como constantes tipadas |
| `nx.time` | `gameTime`, `setTask`, `wait`, coroutines |
| `nx.random` | Aleatoriedade determinística por plugin |
| `nx.net` | Mensagens de rede customizadas |
| `nx.storage` | KV e SQLite (capability `storage`) |
| `nx.http` | Requisições de saída (capability `http`) |
| `nx.log` | Log com contexto do plugin |
| `cs.player` | Jogadores, times, itens, filtros tipados |
| `cs.weapons` | Classes de arma (originais e customizadas), virtuais, munição |
| `cs.rules` | Round, placar, fim de round, `getGameVar`/`setGameVar` |
| `cs.economy` | Dinheiro e recompensas |
| `cs.objectives` | Bomba, reféns, VIP, zonas |
| `cs.admin` | Permissões tipadas |
| `cs.prelude` | Reexporta o uso mais comum (`import cs.prelude.*;`) |

## 3. Forwards

O tipo de erro dos forwards de ciclo de vida é escolhido pelo plugin, desde que seja um
`error enum`.
Por conveniência, `cs.prelude` define `PluginError`, um `error enum` com uma variante
`from` para cada erro da API. Ele permite usar `?` em qualquer chamada sem declarar um enum
próprio e continua totalmente tipado.

| Forward | Assinatura | Equivalente AMXX |
|---------|------------|------------------|
| `pluginPrecache` | `() -> Result<void, E>` | `plugin_precache` |
| `pluginInit` | `() -> Result<void, E>` | `plugin_init` |
| `pluginCfg` | `() -> Result<void, E>` | `plugin_cfg` |
| `pluginEnd` | `()` | `plugin_end` |
| `pluginPause` / `pluginUnpause` | `()` | idem |
| `clientConnect` | `(player: Player)` | `client_connect` |
| `clientAuthorized` | `(player: Player, identity: PlayerId)` | `client_authorized` |
| `clientPutinserver` | `(player: Player)` | `client_putinserver` |
| `clientDisconnected` | `(player: Player, reason: DisconnectReason)` | `client_disconnected` |
| `clientInfochanged` | `(player: Player)` | `client_infochanged` |
| `serverFrame` | `()` | `server_frame` / `FM_StartFrame` (prefira `@system`) |
| `roundFreezeStart` | `()` | `HLTV` event |
| `roundStart` | `()` | `logevent round_start` |
| `roundEnd` | `(reason: RoundEndReason, winner: Team?)` | `logevent round_end` |
| `playerSpawned` | `(player: Player)` | `Ham_Spawn` post |
| `playerKilled` | `(victim: Player, killer: Player?, weapon: WeaponClass?, headshot: bool)` | `DeathMsg` |
| `teamChanged` | `(player: Player, from: Team, to: Team)` | `TeamInfo` |
| `moneyChanged` | `(player: Player, from: Money, to: Money, reason: MoneyReason)` | `Money` msg |
| `itemPurchased` | `(player: Player, item: ItemId)` | — |
| `bombPlanted` / `bombDefused` / `bombExploded` | `(player: Player?)` | `bomb_planted` etc. (CSX) |
| `hostageRescued` | `(hostage: Entity, rescuer: Player)` | — |

## 4. Handles e catálogos de vars

Handles formam uma hierarquia: `Player` e `Weapon` também são `Entity`, então
`getEntityVar(player, "origin")` funciona. A lista abaixo é a inicial. O catálogo
completo é gerado em `rook doc`.

### 4.1 `Entity` — `getEntityVar` / `setEntityVar`

| Chave | Tipo | Acesso |
|-------|------|--------|
| `classname` | `string` | leitura |
| `targetname`, `target` | `string` | leitura/escrita |
| `origin` | `Vector3` | leitura/escrita |
| `angles` | `Angles` | leitura/escrita |
| `velocity` | `Vector3` | leitura/escrita |
| `angularVelocity` | `Angles` | leitura/escrita |
| `mins`, `maxs` | `Vector3` | leitura/escrita |
| `health`, `maxHealth` | `Health` | leitura/escrita |
| `armor` | `Armor` | leitura/escrita |
| `gravity`, `friction` | `Scale` | leitura/escrita |
| `maxSpeed` | `Speed` | leitura/escrita |
| `model` | `Model?` | leitura/escrita |
| `skin` | `i32` | leitura/escrita |
| `body` | `i64` | leitura/escrita (valor combinado legado, só para compatibilidade) |
| `renderMode` | `RenderMode` | leitura/escrita |
| `renderFx` | `RenderFx` | leitura/escrita |
| `renderAmount` | `RenderAmount` | leitura/escrita |
| `renderColor` | `Color` | leitura/escrita |
| `solid` | `SolidType` | leitura/escrita |
| `moveType` | `MoveType` | leitura/escrita |
| `takeDamage` | `DamageMode` | leitura/escrita |
| `owner` | `EntityRef?` | leitura/escrita |
| `flags` | `Flags<EntityFlag>` | leitura/escrita |
| `effects` | `Flags<Effect>` | leitura/escrita |
| `nextThink` | `GameTime` | leitura/escrita |

Body groups e attachments não têm teto ([06 §3.4](06-assets-e-recursos.md#34-limites-de-conteúdo-removidos))
e são acessados por **nome**:

```rook
setBodyGroup(entity, "scope", ScopeVariant.Holographic.submodel())?;   // Result<void, ModelError>
let muzzle: Vector3? = getAttachment(entity, "muzzle");
let parts = getBodyParts(model);                                      // List<BodyPartInfo>
```

Nome de body part, submodelo ou attachment inexistente gera `ModelError` com a lista de
nomes válidos.

### 4.2 `Player` — `getPlayerVar` / `setPlayerVar`

| Chave | Tipo | Acesso | Observação |
|-------|------|--------|------------|
| `name` | `string` | leitura | Alterar com `setUserName` (passa por hook e validação) |
| `identity` | `PlayerId` | leitura | |
| `team` | `Team` | leitura | Alterar com `changeTeam` (aplica regras) |
| `character` | `Character` | leitura/escrita | |
| `money` | `Money` | leitura/escrita | Passa pela economia e gera `moneyChanged` |
| `frags`, `deaths` | `i32` | leitura/escrita | |
| `armorType` | `ArmorType` | leitura/escrita | `None`, `Kevlar`, `KevlarHelmet` |
| `hasDefuser`, `hasNvg` | `bool` | leitura/escrita | |
| `hasC4`, `isVip`, `isAlive`, `isBot` | `bool` | leitura | |
| `inBuyZone`, `inBombZone`, `inRescueZone` | `bool` | leitura | |
| `activeWeapon` | `Weapon?` | leitura | Trocar com `selectWeapon` |
| `fov` | `Angle` | leitura/escrita | |
| `ping` | `Duration` | leitura | |
| `language` | `Language` | leitura | |

### 4.3 `Weapon` (instância) — `getWeaponVar` / `setWeaponVar`

| Chave | Tipo | Acesso |
|-------|------|--------|
| `class` | `WeaponClass` | leitura |
| `owner` | `Player?` | leitura |
| `clip` | `AmmoCount` | leitura/escrita |
| `maxClip` | `AmmoCount` | leitura/escrita (sobrepõe a classe nesta instância) |
| `ammoType` | `AmmoType` | leitura |
| `nextPrimaryAttack`, `nextSecondaryAttack`, `idleTime` | `GameTime` | leitura/escrita |
| `inReload` | `bool` | leitura |
| `silenced`, `burstMode` | `bool` | leitura/escrita |
| `zoomLevel` | `ZoomLevel` | leitura/escrita |
| `shotsFired` | `i32` | leitura/escrita |
| `accuracy` | `f32` | leitura/escrita |

### 4.4 `WeaponClass` — `getWeaponClassVar` / `setWeaponClassVar`

**As armas originais e as customizadas usam o mesmo sistema.** As 26 armas do CS são
classes registradas pelo jogo (constantes `WEAPON_AK47`, `WEAPON_AWP`…), e mudar o preço
da AK é o mesmo que configurar uma arma nova. O desenho segue a API de armas do ReZombie
(classes, herança, virtuais e dados por instância sem gastar entidades).

| Chave | Tipo | Acesso |
|-------|------|--------|
| `name` | `string` | leitura/escrita |
| `type` | `WeaponType` | leitura |
| `parent` | `WeaponClass?` | leitura |
| `abstract` | `bool` | leitura |
| `slot` | `WeaponSlot` | leitura |
| `viewModel`, `playerModel`, `worldModel` | `Model` | leitura/escrita |
| `price` | `Money` | leitura/escrita |
| `maxClip`, `maxAmmo` | `AmmoCount` | leitura/escrita |
| `ammoType` | `AmmoType` | leitura/escrita |
| `weight` | `i32` | leitura/escrita |
| `canDrop` | `bool` | leitura/escrita |
| `maxSpeed` | `Speed` | leitura/escrita |
| `damage` | `Damage` | leitura/escrita |
| `fireInterval` | `Duration` | leitura/escrita |
| `rangeModifier`, `armorRatio` | `Scale` | leitura/escrita |
| `penetration` | `Penetration` | leitura/escrita |
| `teams` | `Set<Team>` | leitura/escrita |

```rook
let rifle = createWeaponClass("drakar_iii", WeaponType.Primary, parent: WEAPON_AK47)?;
setWeaponClassVar(rifle, "name", "Drakar III");
setWeaponClassVar(rifle, "viewModel", precacheModel("models/rezombie/weapons/drakar/v.mdl")?);
setWeaponClassVar(rifle, "maxClip", AmmoCount(45));
bindWeaponVirtual(rifle, WeaponMethod.PrimaryAttack, onDrakarAttack);

fn onDrakarAttack(weapon: Weapon, owner: Player) -> WeaponReturn {
    return callWeaponBase(weapon, owner);     // run the parent implementation (AK-47)
}
```

- `bindWeaponVirtual` verifica em compilação que a função tem a assinatura exigida pelo
  método (`WeaponMethod.PrimaryAttack → fn(Weapon, Player) -> WeaponReturn`).
- Plugins `shared` com classes de arma rodam também no cliente, com **predição**
  ([13 §7](13-sistema-de-mods.md#7-plugins-no-cliente)).

### 4.5 Regras do jogo — `getGameVar` / `setGameVar`

| Chave | Tipo | Acesso |
|-------|------|--------|
| `roundNumber` | `i32` | leitura |
| `roundState` | `RoundState` | leitura |
| `roundTime`, `freezeTime`, `buyTime` | `Duration` | leitura/escrita (round atual) |
| `scoreTerrorist`, `scoreCt` | `i32` | leitura/escrita |
| `mapType` | `MapType` | leitura |
| `bombPlanted` | `bool` | leitura |
| `winConditions` | `WinConditionMode` | leitura/escrita (`Default` ou `PluginControlled`) |

### 4.6 Catálogos de plugins

Plugins de API declaram os próprios catálogos ([14 §13.2](14-linguagem-rook.md#132-catálogos-de-variáveis-vars)).
O ReZombie, por exemplo, define `vars Class`, `vars Props` e `vars Mode`, e os plugins
de conteúdo usam `setClassVar`, `setPropsVar` e `setModeVar` com a mesma
verificação de tipos da engine.

## 5. Ações hookáveis

Equivalentes ao ReAPI (`RegisterHookChain`) e ao Ham Sandwich. Todos têm fase `Pre` e `Post`.

| Hook | Argumentos principais | O que o `Pre` pode fazer |
|------|----------------------|-------------------------|
| `ClientConnect` | `address`, `identity`, `name` | Recusar com motivo (`args.reject = some("...")`) |
| `ClientCommand` | `player`, `command` | Bloquear |
| `ChatMessage` | `player`, `inout text`, `teamOnly` | Alterar ou bloquear (antiflood, filtros) |
| `PlayerSpawn` | `player` | Bloquear ou ajustar |
| `GiveDefaultItems` | `player` | `Supercede` para dar itens próprios |
| `PlayerTakeDamage` | `victim`, `inflictor`, `attacker?`, `inout damage: Damage`, `source`, `hitGroup` | Alterar ou bloquear |
| `PlayerTraceAttack` | `victim`, `attacker`, `inout damage`, `direction`, `hitGroup` | Alterar ou bloquear |
| `PlayerKilled` | `victim`, `killer?`, `source`, `gib` | Ajustar (ex.: sem gib) |
| `PlayerJump`, `PlayerDuck` | `player` | Bloquear |
| `PlayerResetMaxSpeed` | `player`, `inout maxSpeed: Speed` | Alterar velocidade |
| `PlayerMove` | `player`, `inout movement: MovementParams` | Movimento customizado (código `shared`, predito) |
| `ChooseTeam` | `player`, `inout team` | Forçar ou bloquear |
| `PlayerAddMoney` | `player`, `inout amount: Money`, `reason` | Alterar ou bloquear |
| `BuyItem` | `player`, `item`, `inout price: Money` | Alterar preço ou bloquear |
| `ItemPickup` | `player`, `item` | Bloquear |
| `WeaponDrop` | `player`, `weapon` | Bloquear |
| `BombPlant` / `BombDefuseStart` / `BombDefuseEnd` / `BombExplode` | `player?`, `bomb` | Bloquear e ajustar tempos |
| `RoundRestart` | — | Ajustar |
| `RoundEnd` | `inout reason`, `inout winner`, `inout delay: Duration` | Alterar ou bloquear |
| `CheckWinConditions` | — | `Supercede` para regras próprias (modos de jogo) |
| `EntitySpawn`, `EntityThink`, `EntityTouch`, `EntityUse` | `entity`, `other?` | Bloquear |
| `EntityTakeDamage` | `entity`, `attacker?`, `inout damage` | Alterar ou bloquear (`func_breakable` etc.) |
| `EmitSound` | `entity`, `channel`, `inout sound`, `inout volume`, `inout pitch` | Trocar ou bloquear sons |
| `SetModel` | `entity`, `inout model` | Trocar modelos |

## 6. Outras áreas da API

### 6.1 Jogadores com filtros tipados

```rook
// AMXX: getPlayers(players, count, "ae", "TERRORIST")
let terrorists = getPlayers(PlayerFilter { alive: some(true), team: some(Team.Terrorist) });
```

### 6.2 Recursos

```rook
let model: Model = precacheModel("models/rezombie/zombie.mdl")?;   // Result<Model, ResourceError>
let sound: Sound = precacheSound("rezombie/infect.wav")?;
```

Podem ser chamados **a qualquer momento** (sem limite). Em `pluginPrecache`, viram
downloads obrigatórios. Depois, são entregues sob demanda. O `rookc` verifica caminhos
literais contra os assets do mod e do jogo e avisa em build se o arquivo não existir.

### 6.3 Efeitos (sem `message_begin`/`write_byte`)

```rook
createBeam(BeamEffect {
    start:    getEntityVar(player, "origin"),
    end:      targetPosition,
    sprite:   LASER_SPRITE,
    width:    BEAM_WIDTH,
    color:    Color.GREEN,
    duration: 200ms,
});
screenFade(player, ScreenFade { color: Color.RED, duration: 500ms, hold: 0ms });
```

### 6.4 HUD, chat e menus

```rook
showHudText(player, HudText { message: tr("RZ_ROUND_START"), position: HudPosition.TOP_CENTER, hold: 3s });
clientPrintColor(allPlayers(), "{green}[RZ]{default} {name} foi infectado!");

showMenu(player, Menu<ModeChoice> {
    title: tr("RZ_MENU_MODES"),
    items: [
        MenuItem { label: "Infection", value: ModeChoice.Infection },
        MenuItem { label: "Nemesis",   value: ModeChoice.Nemesis, enabled: isAdmin },
    ],
    onSelect: onModeSelected,       // fn(Player, ModeChoice), no raw key numbers
});
```

As tags de cor de `clientPrintColor` são verificadas em compilação.

### 6.5 Comandos no cliente

Não existe `client_cmd` arbitrário: no AMXX ele era usado para alterar binds e configs do
jogador sem permissão (*slowhack*). Ações no cliente vêm de APIs específicas (tocar som,
abrir menu, mudar FOV) ou de plugins de cliente com sandbox.

### 6.6 Armazenamento

```rook
let store = kvOpen("rezombie_stats")?;                       // capability `storage`
kvSet(store, playerKey(player), PlayerStats { infections: 10, kills: 3 })?;
let stats: PlayerStats? = kvGet(store, playerKey(player))?;
```

O SQLite usa consultas preparadas com parâmetros e linhas mapeados para structs, sem
concatenar SQL.

## 7. Rede

Plugins definem mensagens tipadas, sem o teto de tamanho das mensagens de usuário do
original:

```rook
@message(channel: Channel.Reliable)
pub struct InfectionNotice {
    victim:   PlayerRef,
    attacker: PlayerRef?,
    class:    ClassId,
}

sendMessage(allPlayers(), InfectionNotice { victim: refOf(victim), attacker: none, class: ZOMBIE });

// client plugin
@onMessage
fn onInfection(notice: InfectionNotice) { ... }
```

O esquema da mensagem entra no hash de esquema do protocolo ([08 §10](08-rede.md#10-versionamento)).

## 8. Log e diagnóstico

- `logInfo`, `logWarn`, `logError` incluem automaticamente plugin, arquivo e linha.
- Logs de gameplay normal devem ir para o plugin de diagnóstico do mod (padrão de
  `DevRuntime` do ReZombie). O linter avisa sobre `logInfo` em caminhos quentes.

## 9. Catálogo de erros

Todos são `error enum` com mensagem padrão. Os da engine são gerados por reflexão a partir
dos `enum class` do C++.

| Erro | Variantes (exemplos) |
|------|----------------------|
| `ResourceError` | `NotFound{path}`, `InvalidPath{path}`, `UnsupportedFormat{path}`, `TooLarge{path, limit}` |
| `EntityError` | `UnknownClassname{name}`, `QuotaExceeded{limit}`, `NotAlive` |
| `ModelError` | `UnknownBodyPart{name, valid}`, `UnknownSubmodel{part, index}`, `UnknownAttachment{name, valid}` |
| `WeaponClassError` | `NameTaken{name}`, `ParentNotFound{name}`, `AbstractNotGivable{name}`, `IncompatibleType` |
| `CommandError` | `AlreadyRegistered{name}`, `InvalidName{name}` |
| `CvarError` | `AlreadyExists{name}`, `OutOfRange{value, min, max}`, `ReadOnly{name}` |
| `HookError` | `AlreadyRegistered`, `UnknownHook` |
| `MenuError` | `Empty`, `PlayerNotConnected` |
| `StorageError` | `CapabilityMissing`, `Corrupted`, `QuotaExceeded`, `SchemaMismatch{key}` |
| `HttpError` | `CapabilityMissing`, `HostNotAllowed{host}`, `Timeout`, `Status{code}` |
| `MessageError` | `NotRegistered`, `ClientPluginMissing` |
| `VarError` | `UnknownVar{key}`, `TypeMismatch{key, expected}`, `ReadOnly{key}` (só no acesso dinâmico) |
| `ConversionError`, `ParseError` | `OutOfRange`, `InvalidFormat` |
| `TaskError` | `OwnerGone`, `Cancelled` |

## 10. Versionamento e estabilidade

- A API segue **semver** (`api = "^1.0"` no `mod.toml`).
- Cada item tem nível de estabilidade:
  - `@stable`: só muda de forma incompatível em versão maior;
  - `@experimental`: exige `experimental = true` no `mod.toml`;
  - `@deprecated(since: "1.4", use: "novaFuncao")`: gera aviso de compilação com a
    alternativa e só é removido na próxima versão maior.
- O CI compara os `.rookd` gerados com os da última release e **bloqueia** mudanças
  incompatíveis em versões menores.
- `rook doc --api` publica a referência completa da API, com exemplos testados.
