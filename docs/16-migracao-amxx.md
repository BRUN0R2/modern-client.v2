# 16 — Migração AMX Mod X (Pawn) → Rook

## 1. Filosofia

O modelo mental continua o mesmo: plugins com `pluginPrecache`/`pluginInit`, natives,
forwards, hooks, `setTask`, cvars e comandos. A migração consiste em **trocar índices,
tags e strings soltas por tipos**. O compilador aponta tudo que estava errado e passava
despercebido no Pawn.

A conversão pode ser feita à mão com este guia ou assistida por
`rook migrate` ([§6](#6-rook-migrate)).

## 2. Sintaxe

| Pawn | Rook |
|------|------|
| `#include <amxmodx>` | `import cs.prelude.*;` |
| `#include <rezombie_main>` | `import rezombie.prelude.*;` |
| `#pragma semicolon 1` | Sempre obrigatório |
| `#pragma compress 1` | Desnecessário (pacotes já são comprimidos) |
| `#define MAX_ZOMBIES 10` | `const MAX_ZOMBIES: i32 = 10;` |
| `register_plugin("Nome", VERSION, AUTHOR)` | Bloco `plugin { name: ..., version: ..., author: ... }` |
| `new x = 10;` | `let x = 10;` (imutável) ou `var x = 10;` |
| `new Float:speed = 250.0;` | `let speed: Speed = 250ups;` |
| `new Class:class = ...` (tag) | `let class: Class = ...` (handle nominal) |
| `any:` | Genéricos (`fn f<T>(x: T)`) |
| `_:valor` (remover tag) | `.value()` ou conversão explícita |
| `stock foo()` | `fn foo()` (ou `pub fn` para exportar) |
| `public callback()` referenciado por string | `fn callback()` passado por referência |
| `@OnPrimaryAttack(...)` | `fn onPrimaryAttack(...)` |
| `forward @round_start(...)` (declaração) | `pub forward roundStart(...);` |
| `native create_class(...)` + `register_native` | `pub fn createClass(...)` no plugin de API |
| `#pragma unused weapon` | Parâmetro com prefixo `_`: `_weapon: Weapon` |
| `new players[MAX_PLAYERS]; new count;` | `let players: List<Player> = ...;` |
| `new g_data[MAX_PLAYERS + 1];` | `var data: PlayerMap<T> = PlayerMap.new();` (limpo ao desconectar) |
| `new szName[32]; get_user_name(id, szName, charsmax(szName));` | `let name = getPlayerVar(player, "name");` |
| `formatex(buffer, charsmax(buffer), "%s tem %d", a, b)` | `"{a} tem {b}"` |
| `equal(a, b)` / `equali(a, b)` | `a == b` / `a.eqIgnoreCase(b)` |
| `str_to_num(s)` | `i32.parse(s)?` |
| `switch (x) { case 1: ... }` | `match x { ... }` (exaustivo) |
| `ArrayCreate` / `TrieCreate` | `List<T>` / `Map<K, V>` |
| `null` (`any:0`) | `none` (com `T?`) |

## 3. API

| AMXX / ReAPI / Fakemeta / Ham | Rook |
|-------------------------------|------|
| `pev(id, pev_origin, origin)` / `get_entvar(id, var_origin, origin)` | `let origin = getEntityVar(entity, "origin");` |
| `set_pev(id, pev_health, 100.0)` / `set_entvar(...)` | `setEntityVar(entity, "health", 100hp);` |
| `get_member(id, m_iAccount)` / `cs_get_user_money(id)` | `getPlayerVar(player, "money")` |
| `set_member(id, m_iAccount, 800)` / `cs_set_user_money` | `setPlayerVar(player, "money", Money(800));` |
| `get_user_health(id)` / `set_user_health(id, 100)` | `getEntityVar(player, "health")` / `setEntityVar(player, "health", 100hp)` |
| `is_user_alive(id)` | `getPlayerVar(player, "isAlive")` |
| `get_user_team(id)` / `cs_get_user_team` | `getPlayerVar(player, "team")` |
| `rg_set_user_team(id, TEAM_CT)` | `changeTeam(player, Team.CounterTerrorist)?` |
| `get_players(players, num, "ae", "TERRORIST")` | `getPlayers(PlayerFilter { alive: some(true), team: some(Team.Terrorist) })` |
| `give_item(id, "weapon_ak47")` / `rg_give_item` | `giveWeapon(player, WEAPON_AK47)?` |
| `precache_model("models/x.mdl")` | `precacheModel("models/x.mdl")?` → `Model` tipado, sem limite |
| `RegisterHam(Ham_TakeDamage, "player", "fw_TakeDamage")` | `@hook(Hook.PlayerTakeDamage, HookPhase.Pre)` |
| `RegisterHookChain(RG_CBasePlayer_Spawn, "OnSpawn", .post = true)` | `@hook(Hook.PlayerSpawn, HookPhase.Post)` ou `registerHook(...)` |
| `SetHookChainArg(4, ATYPE_FLOAT, dmg)` | `args.damage = dmg;` (`inout args`) |
| `HC_CONTINUE` / `HC_SUPERCEDE` / `HC_BREAK` | `HookResult.Continue` / `.Supercede` / `.Break` |
| `register_event("DeathMsg", "on_death", "a")` | `forward playerKilled(victim, killer, weapon, headshot) { ... }` |
| `register_logevent("on_round_start", 2, "1=Round_Start")` | `forward roundStart() { ... }` |
| `register_clcmd("say /menu", "cmd_menu")` | `registerClcmd("say /menu", cmdMenu)?` |
| `read_argv(1, arg, charsmax(arg))` | Parâmetros tipados na assinatura do comando |
| `create_cvar("rz_delay", "10.0")` + `bind_pcvar_float` | `createCvar("rz_delay", 10s, min: 1s, max: 60s)?` + `getCvar(cvar)` |
| `set_task(1.0, "task", id)` / `remove_task(id)` | `setTask(1s, task, owner: refOf(player))` / `removeTask(handle)` |
| `set_hudmessage(...)` + `show_hudmessage(id, ...)` | `showHudText(player, HudText { ... })` |
| `client_print(id, print_chat, "...")` | `clientPrint(player, PrintTarget.Chat, "...")` |
| `client_print_color(id, print_team_default, "^4[RZ]^1 ...")` | `clientPrintColor(player, "{green}[RZ]{default} ...")` |
| `menu_create` / `menu_additem` / `menu_display` | `showMenu(player, Menu<T> { ... })` |
| `message_begin(MSG_BROADCAST, SVC_TEMPENTITY)` + `write_byte`… | `createBeam(...)`, `createSpriteEffect(...)`, `screenFade(...)` |
| `engfunc(EngFunc_TraceLine, ...)` | `traceLine(TraceQuery { ... })` |
| `client_cmd(id, "...")` | Não existe (proteção contra *slowhack*). Use a API específica |
| Offsets de `pdata` / Orpheu | Desnecessários: catálogo `vars` e hooks |
| `nvault_open` / `nvault_get` | `kvOpen` / `kvGet` (capability `storage`) |
| `SQL_MakeDbTuple` / `SQL_ThreadQuery` | `sqlOpen` + consultas preparadas tipadas (`await`) |
| `set_fail_state("motivo")` | `return err(MeuErro.Variante);` no forward de ciclo de vida |
| `log_amx("...")` | `logInfo("...")` |

## 4. Exemplos convertidos (ReZombie)

### 4.1 Classe Assassin

**Pawn**

```pawn
#include <rezombie_main>

#pragma semicolon 1
#pragma compress 1

public plugin_precache()
{
	register_plugin("Class: Assassin", REZOMBIE_VERSION, REZOMBIE_AUTHOR);

	new Class:class = create_class("assassin", TEAM_ZOMBIE);
	set_class_var(class, "name", "Assassin");

	new ModelsPack:models = get_class_var(class, "models");
	models_pack_add_model(models, create_model("models/player/rz_banshee/rz_banshee.mdl"));

	new Props:props = get_class_var(class, "props");
	set_props_var(props, "health", 5000);
	set_props_var(props, "armor", 0);
	set_props_var(props, "speed", 320);
	set_props_var(props, "gravity", 0.75);
	set_props_var(props, "weapons_interaction", false);

	new WeaponClass:melee = get_class_var(class, "melee");
	set_weapon_class_var(melee, "name", "Assassin Claws");
	set_weapon_class_var(melee, "view_model", create_model("models/player/rz_banshee/hand.mdl"));
}
```

**Rook**

```rook
import rezombie.prelude.*;

plugin {
    name:    "Class: Assassin",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub const ASSASSIN: ClassId = classId("assassin");

forward pluginPrecache() -> Result<void, PrecacheError> {
    let class = createClass(ASSASSIN, Team.Zombie)?;
    setClassVar(class, "name", "Assassin");

    let models = getClassVar(class, "models");
    modelsPackAddModel(models, createModel("models/player/rz_banshee/rz_banshee.mdl")?);

    let props = getClassVar(class, "props");
    setPropsVar(props, "health", 5000hp);
    setPropsVar(props, "armor", 0ap);
    setPropsVar(props, "speed", 320ups);
    setPropsVar(props, "gravity", 0.75x);
    setPropsVar(props, "weaponsInteraction", false);

    let melee = getClassVar(class, "melee");
    setWeaponClassVar(melee, "name", "Assassin Claws");
    setWeaponClassVar(melee, "viewModel", createModel("models/player/rz_banshee/hand.mdl")?);

    return ok();
}
```

O que mudou:

- Mesma estrutura. Os nomes só mudam de grafia para o camelCase do projeto
  (`set_props_var` → `setPropsVar`, `"weapons_interaction"` → `"weaponsInteraction"`).
- As chaves (`"health"`, `"speed"`) continuam strings, agora **verificadas em compilação**
  com o tipo certo.
- `5000hp`, `320ups`, `0.75x`: unidade explícita, sem número mágico e sem misturar vida
  com velocidade.
- `createModel(...)?`: um modelo inexistente vira erro tipado na carga, não um modelo
  invisível em runtime.
- A vida de 5000 aparece correta no HUD ([12 §3.4](12-interface-hud-e-console.md#34-sem-truncamento-de-valores)).

### 4.2 Classe Human (dependência sem ordem manual)

**Pawn**: dependia da ordem no `plugins.manifest` ("coldsteel must register before Class:
Human").

```pawn
	// Default knife: custom Cold Steel (registered before this class plugin).
	set_class_var(class, "melee", RequireWeaponClass("coldsteel"));
```

**Rook**

```rook
import rezombie.prelude.*;
import rezombie.weapons.coldsteel.{COLDSTEEL};    // the import itself orders the load

plugin {
    name:    "Class: Human",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub const HUMAN: ClassId = classId("human");

forward pluginPrecache() -> Result<void, PrecacheError> {
    let class = createClass(HUMAN, Team.Human)?;
    setClassVar(class, "name", "Human");

    let props = getClassVar(class, "props");
    setPropsVar(props, "health", 100hp);
    setPropsVar(props, "armor", 100ap);
    setPropsVar(props, "speed", 250ups);
    setPropsVar(props, "gravity", 1.0x);
    setPropsVar(props, "weaponsInteraction", true);

    // Default knife: custom Cold Steel. The import above guarantees it is registered first.
    setClassVar(class, "melee", requireWeaponClass(COLDSTEEL)?);

    return ok();
}
```

### 4.3 Arma Cold Steel

```rook
import rezombie.prelude.*;

plugin {
    name:    "Weapon: Cold Steel",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub const COLDSTEEL: WeaponClassId = weaponClassId("coldsteel");

const DRAW_SOUND: string = "rezombie/weapons/coldsteel/draw.wav";

forward pluginPrecache() -> Result<void, PrecacheError> {
    precacheSound(DRAW_SOUND)?;

    let weaponClass = createWeaponClass(COLDSTEEL, WeaponType.Melee)?;
    setWeaponClassVar(weaponClass, "name", "Cold Steel");
    setWeaponClassVar(weaponClass, "viewModel", createModel("models/rezombie/weapons/coldsteel/model_v.mdl")?);
    setWeaponClassVar(weaponClass, "playerModel", createModel("models/rezombie/weapons/coldsteel/model_p.mdl")?);
    setWeaponClassVar(weaponClass, "worldModel", createModel("models/rezombie/weapons/coldsteel/model_w.mdl")?);
    setWeaponClassVar(weaponClass, "weight", 10);
    setWeaponClassVar(weaponClass, "canDrop", true);
    bindWeaponVirtual(weaponClass, WeaponMethod.PrimaryAttack, onPrimaryAttack);

    return ok();
}

fn onPrimaryAttack(_weapon: Weapon, _player: Player) -> WeaponReturn {
    return WeaponReturn.Supercede;
}
```

- `bindWeaponVirtual` recebe a **função**, não a string `"@OnPrimaryAttack"`. Assinatura
  errada é erro de compilação.
- Com `target = "shared"` no `mod.toml`, a arma é **predita no cliente**.

### 4.4 Modo Nemesis

**Pawn** (trecho)

```pawn
@LaunchNemesis(target)
{
	new players[MAX_PLAYERS];
	new playersCount;
	CollectAliveHumans(players, playersCount);

	if (playersCount <= 0)
		return false;
	...
	if (change_player_class(.id = player, .class = nemesisClass, ...) > RZ_CONTINUE)
		return false;

	return true;
}

EndRoundEvent:@CheckNemesisWin()
{
	...
	if (humans <= 0 && zombies <= 0)
		return EndRoundEventEndDraw;
	...
}
```

**Rook**

```rook
import rezombie.prelude.*;
import rezombie.classes.nemesis.{NEMESIS};

plugin {
    name:    "Mode: Nemesis",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub error enum NemesisLaunchError {
    @message("no alive human can become the Nemesis")
    NoEligiblePlayers,

    @message("the chosen target is not an alive human")
    TargetNotEligible,

    from ClassChange(ClassChangeError),
}

forward pluginPrecache() -> Result<void, PrecacheError> {
    let mode = createMode(modeId("nemesis"), launchNemesis)?;
    setModeVar(mode, "name", "Nemesis");
    setModeVar(mode, "noticeMessage", "Nemesis");
    setModeVar(mode, "minPlayers", 2);
    setModeVar(mode, "roundTime", 300s);
    // Single boss: no auto-respawn (no cloned nemesis from horde respawn).
    setModeVar(mode, "respawn", RespawnType.Off);
    setModeVar(mode, "supportTarget", true);
    setModeVar(mode, "targetClass", NEMESIS);
    setModeVar(mode, "winCheck", checkNemesisWin);
    return ok();
}

/// One super-zombie (Nemesis) vs the remaining humans.
/// No infection; no class override (one living boss only).
fn launchNemesis(target: Player?) -> Result<void, NemesisLaunchError> {
    let humans = collectAliveHumans();

    if let chosen = target {
        if !isAliveHuman(chosen) {
            return err(NemesisLaunchError.TargetNotEligible);
        }
    }

    let player = pickPlayer(humans, target).okOr(NemesisLaunchError.NoEligiblePlayers)?;
    changePlayerClass(player, NEMESIS, reason: ClassChangeReason.Mode)?;
    return ok();
}

fn checkNemesisWin() -> EndRoundEvent {
    let humans  = countAliveHumans();
    let zombies = countAliveZombies();

    return match (humans, zombies) {
        (0, 0) => EndRoundEvent.Draw,
        (0, _) => EndRoundEvent.ZombiesWin,
        (_, 0) => EndRoundEvent.HumansWin,
        _      => EndRoundEvent.None,
    };
}
```

O que mudou:

- `launchNemesis` retorna **por que** falhou (`NemesisLaunchError`), não só `false`. O
  dono do fluxo (`GameRules`) reage de acordo e o motivo aparece no log.
- `target` é `Player?`: acabou o `RZ_MODE_NO_TARGET = 0`.
- Os callbacks `prepare`/`end` vazios do original deixam de ser obrigatórios: chaves
  opcionais do catálogo `vars Mode`.
- `match` em tupla substitui a cadeia de `if` e o compilador garante que todos os casos
  foram cobertos.

## 5. Diferenças de comportamento importantes

| Tema | AMXX | Rook |
|------|------|------|
| Índice de entidade | `id` inteiro reutilizável, `0` é o mundo | Handle geracional. O mundo é `world()`. Ausência é `none` |
| Guardar jogador para depois | `g_target = id` (pode virar outro jogador) | `PlayerRef` + `resolve()` (obrigatório pelo compilador) |
| Dados por jogador | Zerar à mão em `client_disconnected` | `PlayerMap<T>` limpa sozinho |
| Tempo de `setTask` | Relógio do servidor, afetado por FPS | Tempo de jogo determinístico por tick |
| Falha de inicialização | `set_fail_state` | `err(...)` no forward e dependentes param com o motivo |
| Strings | Buffers fixos e problemas com multibyte | `string` UTF-8 sem tamanho fixo |
| Arrays | Fora dos limites gera erro de runtime genérico | Índice verificado com falha tipada e *stack trace* |
| Overflow | *Wrap* silencioso | `panic` (ou `wrapping_*` explícito) |
| Ordem de plugins | Manual em `plugins.ini` | Pelo grafo de imports |
| Recarregar | Troca de mapa | `plugins reload` com migração de `persist var` |
| Precache | Limitado e só no início do mapa | Sem limite e a qualquer momento |

## 6. `rook migrate`

```bash
rook migrate src/class/Assassin.sma --out plugins/class/assassin.rook --report migrate-report.md
```

O que a ferramenta faz:

1. Faz o *parse* do Pawn, incluindo includes e `#define`s.
2. Converte sintaxe (declarações, tags para tipos, `switch` para `match`, `format` para
   interpolação) e renomeia identificadores e chaves de `vars` para camelCase
   (`set_props_var(props, "max_clip", ...)` → `setPropsVar(props, "maxClip", ...)`).
3. Mapeia natives conhecidos (AMXX, fun, cstrike, fakemeta, hamsandwich, ReAPI) pela
   tabela da [§3](#3-api).
4. Infere unidades pela chave do catálogo (`"health"` → `hp`) e por nomes de variáveis.
5. Marca cada ponto que exige decisão humana com `// MIGRATE(Rxxxx): explicação`, por
   exemplo handles guardados em globais, `client_cmd` ou offsets de pdata.
6. Gera um relatório com o que foi convertido automaticamente, o que precisa de revisão e
   o que não tem equivalente.

A ferramenta **não promete conversão 100% automática**. O objetivo é eliminar o trabalho
mecânico e deixar explícito o que precisa ser pensado.

## 7. Checklist por plugin

- [ ] Imports e bloco `plugin` no lugar dos includes e de `register_plugin`.
- [ ] IDs de conteúdo exportados como `pub const` e importados por quem depende deles.
- [ ] Nomes convertidos para camelCase (`set_props_var` → `setPropsVar`, `"max_clip"` → `"maxClip"`).
- [ ] Todas as chaves de `get*Var`/`set*Var` compilam e com unidades.
- [ ] Nenhum handle efêmero guardado: usar `Ref` ou `PlayerMap`/`EntityMap`.
- [ ] Callbacks por referência, sem strings.
- [ ] Forwards de ciclo de vida retornando `Result` com `error enum` próprio.
- [ ] `setTask` de controle de fluxo revisado (preferir `@system` ou coroutine).
- [ ] Sem `logInfo` em caminho quente.
- [ ] Testes `@test` para o comportamento principal.
