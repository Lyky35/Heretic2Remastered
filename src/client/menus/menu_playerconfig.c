//
// menu_playerconfig.c
//
// Copyright 1998 Raven Software
//

#include "client.h"
#include "menu_playerconfig.h"

cvar_t* m_banner_pconfig;

cvar_t* m_item_name;
cvar_t* m_item_skin;
cvar_t* m_item_shownames;

#define MAX_PLAYER_SKINS	128

static int s_num_player_skins;
static int s_current_skin_index;
static const char* skin_names[MAX_PLAYER_SKINS + 1]; // +1 room for the NULL terminator.

static menuframework_t s_player_config_menu;

static menufield_t s_player_name_field;
static menulist_t s_player_skin_box;
static menulist_t s_shownames_box;

//mxd. Returns the number of skins loaded.
static int PlayerConfig_ScanDirectories(void)
{
	static char skin_names_array[MAX_PLAYER_SKINS][MAX_OSPATH];

	s_num_player_skins = 0;
	s_current_skin_index = 0;

	// Player skins usually live inside base.pak, so scan the game filesystem instead of the
	// OS directory tree (Sys_FindFirst() can't see files that only exist in packs).
	int num_files;
	char** files = FS_ListFiles(playerdir->string, ".m8", &num_files);

	if (files == NULL)
		return 0;

	for (int i = 0; i < num_files && s_num_player_skins < MAX_PLAYER_SKINS; i++)
	{
		// Only skin icons define selectable skins: "players/male/Corvus_i.m8" -> "male/Corvus".
		const int len = (int)strlen(files[i]);
		if (len <= 5 || Q_stricmp(files[i] + len - 5, "_i.m8") != 0)
			continue;

		char skin_name[MAX_OSPATH];
		Com_sprintf(skin_name, sizeof(skin_name), "%s", files[i]);
		skin_name[len - 5] = 0; // Drop the '_i.m8' part.

		char skin_path[MAX_OSPATH];
		Com_sprintf(skin_path, sizeof(skin_path), "%s/%s.m8", playerdir->string, skin_name);

		char skin_icon_path[MAX_OSPATH];
		Com_sprintf(skin_icon_path, sizeof(skin_icon_path), "%s/%s_i.m8", playerdir->string, skin_name);

		int skin_w;
		int skin_h;
		re.DrawGetPicSize(&skin_w, &skin_h, skin_path);

		int icon_w;
		int icon_h;
		re.DrawGetPicSize(&icon_w, &icon_h, skin_icon_path);

		// Both the skin and its icon must actually load (sizes aren't fixed: HD replacements
		// can change them, so only require valid dimensions).
		if (skin_w <= 0 || skin_h <= 0 || icon_w <= 0 || icon_h <= 0)
			continue;

		skin_names[s_num_player_skins] = skin_names_array[s_num_player_skins];
		strcpy_s(skin_names_array[s_num_player_skins], sizeof(skin_names_array[s_num_player_skins]), skin_name); //mxd. strcpy -> strcpy_s

		if (Q_stricmp(skin->string, skin_name) == 0)
			s_current_skin_index = s_num_player_skins;

		s_num_player_skins++;
	}

	FS_FreeFileList(files);
	skin_names[s_num_player_skins] = NULL;

	return s_num_player_skins;
}

static qboolean PlayerConfig_MenuInit(void)
{
	static char name_name[MAX_QPATH];
	static char name_skin[MAX_QPATH];
	static char name_shownames[MAX_QPATH];

	if (PlayerConfig_ScanDirectories() == 0)
		return false;

	s_player_config_menu.nitems = 0;

	Com_sprintf(name_name, sizeof(name_name), "\x02%s", m_item_name->string);
	s_player_name_field.generic.type = MTYPE_FIELD;
	s_player_name_field.generic.name = name_name;
	s_player_name_field.generic.width = re.BF_Strlen(name_name);
	s_player_name_field.generic.callback = NULL;
	s_player_name_field.generic.y = 0;
	s_player_name_field.length = 20;
	s_player_name_field.visible_length = 20;
	strcpy_s(s_player_name_field.buffer, sizeof(s_player_name_field.buffer), player_name->string); //mxd. strcpy -> strcpy_s
	s_player_name_field.cursor = (int)strlen(s_player_name_field.buffer);

	Com_sprintf(name_skin, sizeof(name_skin), "\x02%s", m_item_skin->string);
	s_player_skin_box.generic.type = MTYPE_PLAYER_SKIN;
	s_player_skin_box.generic.y = 220;
	s_player_skin_box.generic.name = name_skin;
	s_player_skin_box.generic.width = re.BF_Strlen(name_skin);
	s_player_skin_box.curvalue = s_current_skin_index;
	s_player_skin_box.itemnames = skin_names;

	Com_sprintf(name_shownames, sizeof(name_shownames), "\x02%s", m_item_shownames->string);
	s_shownames_box.generic.type = MTYPE_SPINCONTROL;
	s_shownames_box.generic.x = 0;
	s_shownames_box.generic.y = 280;
	s_shownames_box.generic.name = name_shownames;
	s_shownames_box.generic.width = re.BF_Strlen(name_shownames);
	s_shownames_box.generic.flags = QMF_SINGLELINE;
	s_shownames_box.itemnames = yes_no_names;
	s_shownames_box.curvalue = ((int)shownames->value != 0);

	Menu_AddItem(&s_player_config_menu, &s_player_name_field);
	Menu_AddItem(&s_player_config_menu, &s_player_skin_box);
	Menu_AddItem(&s_player_config_menu, &s_shownames_box);

	Menu_Center(&s_player_config_menu);

	return true;
}

static void PlayerConfig_MenuDraw(void)
{
	char title[MAX_QPATH];

	// Draw menu BG.
	Menu_DrawBG("book/back/b_conback8.bk", cls.m_menuscale);

	if (cls.m_menualpha == 0.0f)
		return;

	// Draw menu title.
	Com_sprintf(title, sizeof(title), "\x03%s", m_banner_pconfig->string);
	const int x = M_GetMenuLabelX(re.BF_Strlen(title));
	const int y = M_GetMenuOffsetY(&s_player_config_menu);
	re.DrawBigFont(x, y, title, cls.m_menualpha);

	// Draw menu items.
	s_player_config_menu.x = M_GetMenuLabelX(s_player_config_menu.width);
	Menu_Draw(&s_player_config_menu);
}

static const char* PlayerConfig_MenuKey(const int key)
{
	if (cls.m_menustate != MS_OPENED)
		return NULL;

	// Save on the way out: Default_MenuKey() pops the menu on both of these.
	if (key == K_ESCAPE || key == K_JOY2)
	{
		const char* selected_skin = skin_names[s_player_skin_box.curvalue];

		Cvar_Set("name", s_player_name_field.buffer);

		if (selected_skin != NULL)
			Cvar_Set("skin", selected_skin);

		Cvar_SetValue("shownames", (float)s_shownames_box.curvalue);
	}

	return Default_MenuKey(&s_player_config_menu, key);
}

void M_Menu_PlayerConfig_f(void)
{
	if (PlayerConfig_MenuInit()) // H2
		M_PushMenu(PlayerConfig_MenuDraw, PlayerConfig_MenuKey);
}