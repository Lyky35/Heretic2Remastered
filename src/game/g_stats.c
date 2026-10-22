//
// g_stats.c -- H2R player stats (damage, most used weapon, dismemberment counters).
//
// Totals + per-mission counters for every client. Kept in file-static storage on
// purpose: nothing here is part of the binary savegame structs (game_locals_t /
// gclient_t), so old saves keep loading. Persistence is a JSON sidecar
// ("stats.json" next to game.ssv), written by WriteGame() and read by ReadGame().
//
// Copyright 1998 Raven Software
//

#include "g_local.h"
#include "g_stats.h"
#include "g_HitLocation.h"

#define STATS_SEND_INTERVAL	1.0f
#define STATS_JSON_VERSION	1

typedef struct
{
	int damage_dealt;
	int damage_taken;
	int deaths;
	int weapon_shots[PWID_MAX];
	int chops_head;
	int chops_torso;
	int chops_arm_left;
	int chops_arm_right;
	int chops_leg_left;
	int chops_leg_right;
} player_stats_t;

static player_stats_t s_totals[MAX_CLIENTS];
static player_stats_t s_mission[MAX_CLIENTS];
static player_stats_t s_last_mission[MAX_CLIENTS];

static char s_mission_name[MAX_QPATH];
static char s_last_mission_name[MAX_QPATH];

static uint s_seq[MAX_CLIENTS];
static qboolean s_dirty[MAX_CLIENTS];
static float s_next_send[MAX_CLIENTS];

static qboolean s_session_started;
static qboolean s_loaded_from_save;

#pragma region ========================== Helpers ==========================

static int G_Stats_ClientIndex(const edict_t* ent)
{
	if (ent == NULL || ent->client == NULL)
		return -1;

	const int num = ent->s.number - 1;
	if (num < 0 || num >= MAX_CLIENTS)
		return -1;

	return num;
}

static int MostUsedWeapon(const player_stats_t* stats, int* out_shots)
{
	int best = PWID_NONE;
	int best_shots = 0;

	for (int i = PWID_NONE + 1; i < PWID_MAX; i++)
	{
		if (stats->weapon_shots[i] > best_shots)
		{
			best = i;
			best_shots = stats->weapon_shots[i];
		}
	}

	if (out_shots != NULL)
		*out_shots = best_shots;

	return best;
}

static int GameMode(void) // 0 = single player, 1 = coop, 2 = deathmatch.
{
	if (DEATHMATCH)
		return 2;

	if (COOP)
		return 1;

	return 0;
}

#pragma endregion

#pragma region ========================== Runtime tracking ==========================

void G_Stats_DamageDealt(edict_t* attacker, const int damage)
{
	const int num = G_Stats_ClientIndex(attacker);
	if (num < 0 || damage <= 0)
		return;

	s_totals[num].damage_dealt += damage;
	s_mission[num].damage_dealt += damage;
	s_dirty[num] = true;
}

void G_Stats_DamageTaken(edict_t* victim, const int damage)
{
	const int num = G_Stats_ClientIndex(victim);
	if (num < 0 || damage <= 0)
		return;

	s_totals[num].damage_taken += damage;
	s_mission[num].damage_taken += damage;
	s_dirty[num] = true;
}

void G_Stats_Death(edict_t* victim)
{
	const int num = G_Stats_ClientIndex(victim);
	if (num < 0)
		return;

	s_totals[num].deaths++;
	s_mission[num].deaths++;
	s_dirty[num] = true;
}

void G_Stats_Chop(edict_t* attacker, edict_t* victim, const int hitloc)
{
	// Only parts chopped out of monsters by a player count (not players being dismembered).
	if (victim == NULL || victim->client != NULL)
		return;

	const int num = G_Stats_ClientIndex(attacker);
	if (num < 0)
		return;

	int part;

	switch (hitloc & ~hl_MeleeHit)
	{
		case hl_Head:
			part = 0;
			break;

		case hl_TorsoFront:
		case hl_TorsoBack: // "cut in half".
			part = 1;
			break;

		case hl_ArmUpperLeft:
		case hl_ArmLowerLeft:
			part = 2;
			break;

		case hl_ArmUpperRight:
		case hl_ArmLowerRight:
			part = 3;
			break;

		case hl_LegUpperLeft:
		case hl_LegLowerLeft:
			part = 4;
			break;

		case hl_LegUpperRight:
		case hl_LegLowerRight:
			part = 5;
			break;

		// 50/50 variants from MG_GetHitLocation(): attribute to the first candidate.
		case hl_Half_LLL_LRL:
		case hl_Half_ULL_URL:
			part = 4;
			break;

		case hl_Half_FT_BT:
		case hl_Half_FT_URA:
		case hl_Half_FT_ULA:
		case hl_Half_FT_LRA:
		case hl_Half_FT_LLA:
		case hl_Half_BT_URA:
		case hl_Half_BT_ULA:
		case hl_Half_BT_LRA:
		case hl_Half_BT_LLA:
			part = 1;
			break;

		default:
			return;
	}

	int* counters[2][6] = {
		{ &s_totals[num].chops_head, &s_totals[num].chops_torso, &s_totals[num].chops_arm_left, &s_totals[num].chops_arm_right, &s_totals[num].chops_leg_left, &s_totals[num].chops_leg_right },
		{ &s_mission[num].chops_head, &s_mission[num].chops_torso, &s_mission[num].chops_arm_left, &s_mission[num].chops_arm_right, &s_mission[num].chops_leg_left, &s_mission[num].chops_leg_right }
	};

	(*counters[0][part])++;
	(*counters[1][part])++;
	s_dirty[num] = true;
}

void G_Stats_Shot(const playerinfo_t* info, const player_weapon_id_e weapon)
{
	if (info == NULL || weapon <= PWID_NONE || weapon >= PWID_MAX)
		return;

	const int num = G_Stats_ClientIndex((const edict_t*)info->self);
	if (num < 0)
		return;

	s_totals[num].weapon_shots[weapon]++;
	s_mission[num].weapon_shots[weapon]++;
	s_dirty[num] = true;
}

#pragma endregion

#pragma region ========================== Configstring transport ==========================

// 6 slots per client: for each of the 3 blocks (current mission / totals / previous
// mission) we use two configstrings, because a single one (MAX_QPATH = 64 bytes)
// can't hold all twelve fields once the damage counters get large.
// Block A: seq,mode,dmg_dealt,dmg_taken,deaths
// Block B: head,torso,arm_l,arm_r,leg_l,leg_r,weapon,weapon_shots
static void BuildStatsStringA(const player_stats_t* stats, const int seq, char* buffer, const int buffer_size)
{
	Com_sprintf(buffer, buffer_size, "%u,%d,%d,%d,%d",
				seq, GameMode(),
				stats->damage_dealt, stats->damage_taken, stats->deaths);
}

static void BuildStatsStringB(const player_stats_t* stats, char* buffer, const int buffer_size)
{
	int weapon_shots = 0;
	const int weapon = MostUsedWeapon(stats, &weapon_shots);

	Com_sprintf(buffer, buffer_size, "%d,%d,%d,%d,%d,%d,%d,%d",
				stats->chops_head, stats->chops_torso,
				stats->chops_arm_left, stats->chops_arm_right,
				stats->chops_leg_left, stats->chops_leg_right,
				weapon, weapon_shots);
}

static void SendClientStats(const int num)
{
	char buffer[64];

	BuildStatsStringA(&s_mission[num], s_seq[num], buffer, sizeof(buffer));
	gi.configstring(CS_PLAYERSTATS + num * 6 + 0, buffer);
	BuildStatsStringB(&s_mission[num], buffer, sizeof(buffer));
	gi.configstring(CS_PLAYERSTATS + num * 6 + 1, buffer);

	BuildStatsStringA(&s_totals[num], s_seq[num], buffer, sizeof(buffer));
	gi.configstring(CS_PLAYERSTATS + num * 6 + 2, buffer);
	BuildStatsStringB(&s_totals[num], buffer, sizeof(buffer));
	gi.configstring(CS_PLAYERSTATS + num * 6 + 3, buffer);

	BuildStatsStringA(&s_last_mission[num], s_seq[num], buffer, sizeof(buffer));
	gi.configstring(CS_PLAYERSTATS + num * 6 + 4, buffer);
	BuildStatsStringB(&s_last_mission[num], buffer, sizeof(buffer));
	gi.configstring(CS_PLAYERSTATS + num * 6 + 5, buffer);

	s_dirty[num] = false;
}

void G_Stats_LevelInit(void)
{
	if (s_loaded_from_save)
	{
		// A save was just loaded: keep whatever G_Stats_ReadJSON() restored.
		s_loaded_from_save = false;
		s_session_started = true;
	}
	else if (!s_session_started)
	{
		// Fresh server session (new game): the game DLL was just loaded, so the
		// statics are already zero. Just make sure of it.
		s_session_started = true;
		memset(s_last_mission, 0, sizeof(s_last_mission));
		s_last_mission_name[0] = 0;
	}
	else
	{
		// Level transition: totals stay, the finished mission is rolled aside.
		memcpy(s_last_mission, s_mission, sizeof(s_last_mission));
		strcpy_s(s_last_mission_name, sizeof(s_last_mission_name), s_mission_name);
		memset(s_mission, 0, sizeof(s_mission));
	}

	// Filled in by G_Stats_RunFrame() - level.mapname is parsed after InitGame().
	s_mission_name[0] = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		s_dirty[i] = true;
		s_next_send[i] = 0.0f;
		s_seq[i]++;
	}
}

void G_Stats_RunFrame(void)
{
	if (s_mission_name[0] == 0 && level.mapname[0] != 0)
		strcpy_s(s_mission_name, sizeof(s_mission_name), level.mapname);

	const int max_clients = (int)maxclients->value;

	for (int i = 0; i < max_clients && i < MAX_CLIENTS; i++)
	{
		const edict_t* ent = g_edicts + i + 1;

		if (!ent->inuse || ent->client == NULL)
			continue;

		if (s_dirty[i] && level.time >= s_next_send[i])
		{
			SendClientStats(i);
			s_next_send[i] = level.time + STATS_SEND_INTERVAL;
		}
	}
}

void G_Stats_Statrefresh(edict_t* ent)
{
	const int num = G_Stats_ClientIndex(ent);
	if (num < 0)
		return;

	s_dirty[num] = true;
	s_next_send[num] = 0.0f;
	SendClientStats(num);
}

#pragma endregion

#pragma region ========================== JSON savegame sidecar ==========================

// ".../game.ssv" -> ".../stats.json".
static void Stats_JsonPath(const char* save_filename, char* buffer, const int buffer_size)
{
	strcpy_s(buffer, buffer_size, save_filename);

	const int len = (int)strlen(buffer);
	const char* suffix = "game.ssv";
	const int suffix_len = (int)strlen(suffix);

	if (len >= suffix_len && Q_stricmp(buffer + len - suffix_len, suffix) == 0)
	{
		buffer[len - suffix_len] = 0;
		strcat_s(buffer, buffer_size, "stats.json");
	}
	else
	{
		strcat_s(buffer, buffer_size, ".json");
	}
}

static void WriteStatsBlock(FILE* f, const player_stats_t* stats)
{
	fprintf(f, "\"damage_dealt\":%d,\"damage_taken\":%d,\"deaths\":%d,",
			stats->damage_dealt, stats->damage_taken, stats->deaths);
	fprintf(f, "\"chops_head\":%d,\"chops_torso\":%d,", stats->chops_head, stats->chops_torso);
	fprintf(f, "\"chops_arm_left\":%d,\"chops_arm_right\":%d,", stats->chops_arm_left, stats->chops_arm_right);
	fprintf(f, "\"chops_leg_left\":%d,\"chops_leg_right\":%d,", stats->chops_leg_left, stats->chops_leg_right);
	fprintf(f, "\"weapon_shots\":[");

	qboolean first = true;

	for (int i = PWID_NONE + 1; i < PWID_MAX; i++)
	{
		if (stats->weapon_shots[i] == 0)
			continue;

		fprintf(f, "%s{\"id\":%d,\"shots\":%d}", first ? "" : ",", i, stats->weapon_shots[i]);
		first = false;
	}

	fprintf(f, "]}");
}

void G_Stats_WriteJSON(const char* save_filename)
{
	char path[MAX_OSPATH];
	Stats_JsonPath(save_filename, path, sizeof(path));

	FILE* f;
	if (fopen_s(&f, path, "w") != 0)
	{
		gi.dprintf("G_Stats_WriteJSON: couldn't open '%s'\n", path);
		return;
	}

	const int max_clients = (int)maxclients->value;

	fprintf(f, "{\n\"version\":%d,\n\"players\":[", STATS_JSON_VERSION);

	qboolean first = true;

	for (int i = 0; i < max_clients && i < MAX_CLIENTS; i++)
	{
		const edict_t* ent = g_edicts + i + 1;

		if (!ent->inuse || ent->client == NULL)
			continue;

		const char* name = ent->client->playerinfo.pers.netname;
		if (name == NULL || name[0] == 0)
			continue;

		// Note: netnames are info-validated (no quotes/backslashes), so no escaping needed.
		fprintf(f, "%s\n{\"slot\":%d,\"name\":\"%s\",", first ? "" : ",", i, name);
		first = false;

		fprintf(f, "\"totals\":{");
		WriteStatsBlock(f, &s_totals[i]);
		fprintf(f, "},\"mission\":{");
		WriteStatsBlock(f, &s_mission[i]);
		fprintf(f, "},\"last_mission\":{");
		WriteStatsBlock(f, &s_last_mission[i]);
		fprintf(f, "}}");
	}

	fprintf(f, "\n]}\n");
	fclose(f);

	gi.dprintf("Wrote player stats to '%s'\n", path);
}

// Parses one stats block as written by WriteStatsBlock().
static qboolean Stats_JsonReadBlock(const char* p, player_stats_t* stats)
{
	if (sscanf(p, "\"damage_dealt\":%d,\"damage_taken\":%d,\"chops_head\":%d,\"chops_torso\":%d,"
				 "\"chops_arm_left\":%d,\"chops_arm_right\":%d,\"chops_leg_left\":%d,\"chops_leg_right\":%d",
				&stats->damage_dealt, &stats->damage_taken,
				&stats->chops_head, &stats->chops_torso,
				&stats->chops_arm_left, &stats->chops_arm_right,
				&stats->chops_leg_left, &stats->chops_leg_right) != 8)
	{
		return false;
	}

	// Written since R8.9; older stats.json sidecars simply have no deaths field.
	const char* d = strstr(p, "\"deaths\":");
	if (d != NULL)
		sscanf(d, "\"deaths\":%d", &stats->deaths);

	const char* w = strstr(p, "\"weapon_shots\":[");
	if (w == NULL)
		return false;

	w += strlen("\"weapon_shots\":[");

	while (*w != 0 && *w != ']')
	{
		while (*w == ',' || *w == ' ' || *w == '\n')
			w++;

		if (*w != '{')
			break;

		int id = 0;
		int shots = 0;

		if (sscanf(w, "{\"id\":%d,\"shots\":%d}", &id, &shots) == 2 && id > PWID_NONE && id < PWID_MAX)
			stats->weapon_shots[id] = shots;

		const char* end = strchr(w, '}');
		if (end == NULL)
			break;

		w = end + 1;
	}

	return true;
}

void G_Stats_ReadJSON(const char* save_filename)
{
	char path[MAX_OSPATH];
	Stats_JsonPath(save_filename, path, sizeof(path));

	// Old saves have no sidecar: start from zeros (totals are per session anyway).
	memset(s_totals, 0, sizeof(s_totals));
	memset(s_mission, 0, sizeof(s_mission));
	memset(s_last_mission, 0, sizeof(s_last_mission));
	s_mission_name[0] = 0;
	s_last_mission_name[0] = 0;

	FILE* f;

	if (fopen_s(&f, path, "rb") == 0)
	{
		fseek(f, 0, SEEK_END);
		const long file_size = ftell(f);
		fseek(f, 0, SEEK_SET);

		if (file_size > 0)
		{
			char* text = malloc((size_t)file_size + 1);
			if (text != NULL)
			{
				const size_t read = fread(text, 1, (size_t)file_size, f);
				text[read] = 0;

				const char* players = strstr(text, "\"players\":[");
				if (players != NULL)
				{
					const char* p = players + strlen("\"players\":[");

					while (true)
					{
						const char* start = strchr(p, '{');
						if (start == NULL)
							break;

						// Find the matching closing brace (blocks contain nested objects).
						int depth = 0;
						const char* end = start;

						for (; *end != 0; end++)
						{
							if (*end == '{')
								depth++;
							else if (*end == '}' && --depth == 0)
								break;
						}

						if (depth != 0)
							break; // Malformed.

						char block[2048];
						const int len = (int)(end - start) + 1;

						if (len < (int)sizeof(block))
						{
							memcpy(block, start, len);
							block[len] = 0;

							int slot = -1;
							char name[MAX_QPATH];

							if (sscanf(block, "{\"slot\":%d,\"name\":\"%63[^\"]\"", &slot, name) == 2 && slot >= 0 && slot < MAX_CLIENTS)
							{
								const char* b;

								if ((b = strstr(block, "\"totals\":{")) != NULL)
									Stats_JsonReadBlock(b + 10, &s_totals[slot]);

								if ((b = strstr(block, "\"mission\":{")) != NULL)
									Stats_JsonReadBlock(b + 11, &s_mission[slot]);

								if ((b = strstr(block, "\"last_mission\":{")) != NULL)
									Stats_JsonReadBlock(b + 16, &s_last_mission[slot]);
							}
						}

						p = end + 1;
					}
				}

				free(text);
			}
		}

		fclose(f);

		gi.dprintf("Loaded player stats from '%s'\n", path);
	}

	// Even without a sidecar: skip the mission roll for this level (a loaded
	// game keeps its state, it doesn't "continue" the previous session).
	s_loaded_from_save = true;
}

#pragma endregion
