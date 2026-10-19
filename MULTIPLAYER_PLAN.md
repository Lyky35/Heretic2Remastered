# Multiplayer / Coop Plan — Beta

Goal: expose working multiplayer + cooperative play, using m-x-d/Heretic2R as
the reference.

**Status: implemented on Beta.** Added the main-menu **Multiplayer** button
(`s_multiplayer_action`, y=32) in `menu_main.c` → `M_Menu_Multiplayer_f()`.
The MP menu it opens already existed and is complete (Join/Start Server,
Player Setup, DM Options, Address Book). The rest of the MP/coop stack
(server, netcode, coop logic) was already in the tree.

## 1. Current state (verified in-tree) — already has nearly everything

The Beta branch **already contains a complete multiplayer implementation**:

- **Server**: `src/server/` — `sv_main.c`, `sv_init.c`, `sv_world.c`,
  `sv_user.c`, `sv_send.c`, `sv_entities.c`, `sv_effects.c`, `sv_ccmds.c`.
- **Netcode**: `src/qcommon/net_chan.c`, `netmsg_read.c`, `netmsg_write.c`;
  platform impl `src/win32/net_wins.c` (`NET_Init`, `NET_SendPacket`,
  `NET_GetPacket`, `NET_Config`). `NET_Init`/`Netchan_Init`/`SV_Init`/`CL_Init`
  all called from `common.c`.
- **Dedicated server**: `dedicated` cvar + `dedicated_start` (`common.c:654`).
- **Coop / deathmatch**: `coop` + `deathmatch` cvars (`g_main.c`),
  `DEATHMATCH`/`COOP` macros (`g_local.h`), `info_player_coop` spawn point
  (`g_spawn.c`), `sv_cooptimeout` "wait for players" logic (`sv_init.c`).
- **MP menus**: `menu_multiplayer.c` (Join Server / Start Server / Player
  Setup / Download Options), `menu_joinserver.c`, `menu_startserver.c`,
  `menu_playerconfig.c`, `menu_downloadoptions.c`, `menu_dmoptions.c`,
  `menu_addressbook.c`. Console commands `menu_multiplayer` /
  `menu_startserver` registered (`menu.c`).
- **Protocol**: `PROTOCOL_VERSION 51` + `H2R_PROTOCOL_VERSION 55` (m-x-d's).

So the backend, netcode, coop logic, and MP menus are all present.

## 2. m-x-d/Heretic2R comparison

- m-x-d has the **same** menu files (verified: `menu_multiplayer.c` is
  byte-identical modulo the `client.h` include path).
- m-x-d's `menu_main.c` adds a **Multiplayer** button (`s_multiplayer_action`,
  y=32) that calls `M_Menu_Multiplayer_f()`.
- The Beta branch's `menu_main.c` has **Game / Options / Video / Sound /
  Info / Quit only** — the Multiplayer button is missing, so the MP menu is
  reachable only via the console command `menu_multiplayer`.

## 3. Gap

The **only** missing piece is the main-menu Multiplayer button. Everything
else (server, netcode, coop logic, MP menus) is already in the tree.

## 4. The change (port m-x-d's button into `menu_main.c`)

1. `#include "menu_multiplayer.h"`.
2. `static menuaction_t s_multiplayer_action;`
   + `static void MainMultiplayerFunc(void* data) { M_Menu_Multiplayer_f(); }`
   + `static char name_multiplayer[MAX_QPATH];`
3. Insert the action setup at y=32; shift the others down by 32:
   game 0, **multiplayer 32**, options 64, video 96, sound 128, info 160,
   quit 192.
4. `Menu_AddItem(&s_main_menu, &s_multiplayer_action);` right after the game
   action.

This is a small, self-contained menu change; the MP menu it opens already
exists and is complete.

## 5. Risks / open questions

- **"Works but practically untested"** (m-x-d's own note): the netcode/coop
  path needs real multi-machine testing (LAN). Local verification = two
  clients on one host, or a listen server + a loopback client.
- **Coop specifics**: verify `info_player_coop` spawn points exist in the
  stock maps and that `sv_cooptimeout` behaves; enemy/monster scaling for
  multiple players may need tuning (coop is a design question, not just the
  button).
- **GameSpy / master server**: not implemented (per m-x-d); LAN/direct IP
  only. The address book + direct connect cover this.
- **No regression to single player** when the button is added (menu-only
  change; `coop`/`deathmatch` default to 0).

## 6. Acceptance (Windows manual test)

- Main menu shows a **Multiplayer** entry; it opens the MP menu.
- Start Server (coop, `coop 1`) → connect a second client → both players
  spawn in the same map and can progress together.
- Deathmatch mode still works.
- Single player unchanged (menu-only change).
- LAN/direct-IP connect works; no crash on join/leave.
