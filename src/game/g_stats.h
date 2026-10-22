//
// g_stats.h -- H2R player stats (damage, most used weapon, dismemberment counters).
//
// Copyright 1998 Raven Software
//

#pragma once

#include "q_shared.h"

struct edict_s;
struct playerinfo_s;

// Called from InitGame(): rolls mission stats into "last mission" on level
// transitions, and (re)starts everything on a fresh server session.
void G_Stats_LevelInit(void);

// Throttled configstring pushes for connected clients.
void G_Stats_RunFrame(void);

// Hooks (all safe to call with non-player/world entities).
void G_Stats_DamageDealt(struct edict_s* attacker, const int damage);
void G_Stats_DamageTaken(struct edict_s* victim, const int damage);
void G_Stats_Death(struct edict_s* victim);
void G_Stats_Chop(struct edict_s* attacker, struct edict_s* victim, const int hitloc);
void G_Stats_Shot(const struct playerinfo_s* info, const player_weapon_id_e weapon);

// Immediate stats push for one client ("statrefresh" command).
void G_Stats_Statrefresh(struct edict_s* ent);

// Savegame sidecar ("stats.json" next to game.ssv).
void G_Stats_WriteJSON(const char* save_filename);
void G_Stats_ReadJSON(const char* save_filename);
