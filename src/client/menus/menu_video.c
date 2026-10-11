//
// menu_video.c
//
// Copyright 1998 Raven Software
//

#include "client.h"
#include "vid_dll.h"
#include "menu_video.h"

cvar_t* m_banner_video;

cvar_t* m_item_driver; // "Renderer"
cvar_t* m_item_vidmode; // "Video resolution"
cvar_t* m_item_target_fps; //mxd. "Target FPS"
cvar_t* m_item_gamma;
cvar_t* m_item_brightness;
cvar_t* m_item_contrast;
cvar_t* m_item_minlight; // YQ2
cvar_t* m_item_detail;
cvar_t* m_item_hd_mode;
cvar_t* m_item_antialiasing;
cvar_t* m_item_color_profile;
cvar_t* m_item_water_reflections;
cvar_t* m_item_water_reflection_res;
cvar_t* m_item_light_emission;
cvar_t* m_item_shadows;

static float m_gamma;
static float m_brightness;
static float m_contrast;
static float m_minlight; //mxd. gl_minlight when entering menu.
static float m_hd_mode; //mxd. r_hd_textures when entering menu.
static float m_antialiasing; //mxd. r_antialiasing when entering menu.
static float m_color_profile; //mxd. r_colorprofile when entering menu.

static menuframework_t s_video_menu;

static menulist_t s_ref_list;
static menulist_t s_mode_list;
static menulist_t s_target_fps_list; //mxd
static menuslider_t s_gamma_slider;
static menuslider_t s_brightness_slider;
static menuslider_t s_contrast_slider;
static menuslider_t s_minlight_slider; // YQ2
static menuslider_t s_detail_slider;
static menulist_t s_hd_mode_list;
static menulist_t s_antialiasing_list;
static menulist_t s_color_profile_list;
static menulist_t s_water_reflections_list;
static menulist_t s_water_reflection_res_list;
static menulist_t s_light_emission_list;
static menulist_t s_shadows_list;

static const char* ref_list_titles[MAX_REFLIBS];
static int initial_reflib_index; // vid_ref index when entering menu.

#define MAX_DISPLAYED_VIDMODES	64 //mxd. This is kinda ugly, since vid_modes array itself is dynamically allocated... 
static const char* vid_mode_titles[MAX_DISPLAYED_VIDMODES];
static int initial_vid_mode; // vid_mode when entering menu.

#pragma region ========================== MENU ITEM CALLBACKS ==========================

static void UpdateTargetFPSFunc(void* self) //mxd
{
	static const int fps_values[] = { 30, 60, 90, 120, 144, 240 };
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("vid_maxfps", (float)fps_values[list->curvalue]);
}

static void UpdateGammaFunc(void* self) // H2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("vid_gamma", (16.0f - slider->curvalue) / 16.0f);
}

static void UpdateBrightnessFunc(void* self)
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("vid_brightness", slider->curvalue / 16.0f);
}

static void UpdateContrastFunc(void* self) // H2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("vid_contrast", slider->curvalue / 16.0f);
}

static void UpdateMinlightFunc(void* self) // YQ2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("gl_minlight", slider->curvalue * 4.0f);
}

static void UpdateDetailFunc(void* self) // H2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("r_detail", slider->curvalue);
}

static void UpdateHDModeFunc(void* self)
{
	Cvar_SetValue("r_hd_textures", (float)s_hd_mode_list.curvalue);
	vid_restart_required = true;
}

static void UpdateAntiAliasingFunc(void* self)
{
	Cvar_SetValue("r_antialiasing", (float)s_antialiasing_list.curvalue);
	vid_restart_required = true;
}

static void UpdateColorProfileFunc(void* self)
{
	Cvar_SetValue("r_colorprofile", (float)s_color_profile_list.curvalue);
}

static void UpdateWaterReflectionsFunc(void* self)
{
	Cvar_SetValue("r_reflections", (float)(s_water_reflections_list.curvalue == 0));
}

static void UpdateWaterReflectionResFunc(void* self)
{
	Cvar_SetValue("r_reflections_res", (float)s_water_reflection_res_list.curvalue);
}

static void UpdateLightEmissionFunc(void* self)
{
	Cvar_SetValue("gl_dlight_scale", (float)s_light_emission_list.curvalue);
}

static void UpdateShadowsFunc(void* self)
{
	Cvar_SetValue("r_shadows", (float)(s_shadows_list.curvalue == 0));
}

static void ApplyChanges(const qboolean close_menu) //mxd. +close_menu arg.
{
	if (initial_vid_mode != s_mode_list.curvalue)
	{
		Cvar_SetValue("vid_mode", (float)s_mode_list.curvalue);
		initial_vid_mode = s_mode_list.curvalue;
		vid_restart_required = true;
	}

	if ((int)m_minlight != (int)m_gl_minlight->value) // YQ2
		vid_restart_required = true;

	if (vid_restart_required)
	{
		M_ForceMenuOff();
		return;
	}

	if (close_menu)
	{
		//mxd. These don't require vid_restart, but we still need to update ALL textures in RI_BeginFrame() AFTER menu is closed.
		// (only it_pic/it_sky textures are updated by R_GammaAffect() when menus are open (for performance reasons)).
		if (m_gamma != vid_gamma->value || m_brightness != vid_brightness->value || m_contrast != vid_contrast->value || m_color_profile != Cvar_VariableValue("r_colorprofile"))
			Cvar_SetValue("vid_textures_refresh_required", 1.0f);

		M_PopMenu();
	}
}

#pragma endregion

void VID_PreMenuInit(void)
{
	//mxd. Refresher library titles.
	for (int i = 0; i < num_reflib_infos; i++)
	{
		ref_list_titles[i] = reflib_infos[i].title;

		if (Q_stricmp(vid_ref->string, reflib_infos[i].id) == 0)
		{
			initial_reflib_index = i;
			s_ref_list.curvalue = i;
		}
	}

	ref_list_titles[num_reflib_infos] = NULL;

	//mxd. Window resolution labels.
	for (int i = 0; i < min(MAX_DISPLAYED_VIDMODES, num_vid_modes); i++)
		vid_mode_titles[i] = vid_modes[i].description;

	vid_mode_titles[num_vid_modes] = NULL;

	if (vid_mode == NULL)
	{
		vid_mode = Cvar_Get("vid_mode", "0", 0);
		vid_restart_required = true;
	}

	initial_vid_mode = (int)vid_mode->value; //mxd

	if (scr_viewsize == NULL)
		scr_viewsize = Cvar_Get("viewsize", "100", CVAR_ARCHIVE);
}

static void VID_MenuInit(void)
{
	static const char* target_fps_names[] = { "30", "60", "90", "120", "144", "240", NULL }; //mxd
	static const int target_fps_values[] = { 30, 60, 90, 120, 144, 240 };
	static const char* antialiasing_names[] = { "Off", "MSAA", "FXAA", NULL };
	static const char* color_profile_names[] = { "sRGB", "Adobe RGB", "DCI-P3", "Rec.2020", NULL };
	static const char* on_off_names[] = { "On", "Off", NULL };
	static const char* reflection_res_names[] = { "Native", "Half", NULL };
	static const char* light_emission_names[] = { "0", "1", "2", "3", "4", "5", "6", NULL };

	static char name_driver[MAX_QPATH];
	static char name_vidmode[MAX_QPATH];
	static char name_target_fps[MAX_QPATH]; //mxd
	static char name_gamma[MAX_QPATH];
	static char name_brightness[MAX_QPATH];
	static char name_contrast[MAX_QPATH];
	static char name_minlight[MAX_QPATH]; // YQ2
	static char name_detail[MAX_QPATH];
	static char name_hd_mode[MAX_QPATH];
	static char name_antialiasing[MAX_QPATH];
	static char name_color_profile[MAX_QPATH];
	static char name_water_reflections[MAX_QPATH];
	static char name_water_reflection_res[MAX_QPATH];
	static char name_light_emission[MAX_QPATH];
	static char name_shadows[MAX_QPATH];

	VID_PreMenuInit();

	m_gamma = Cvar_VariableValue("vid_gamma");
	m_brightness = Cvar_VariableValue("vid_brightness");
	m_contrast = Cvar_VariableValue("vid_contrast");
	m_minlight = Cvar_VariableValue("gl_minlight"); // YQ2
	m_hd_mode = Cvar_VariableValue("r_hd_textures");
	m_antialiasing = Cvar_VariableValue("r_antialiasing");
	m_color_profile = Cvar_VariableValue("r_colorprofile");

	s_video_menu.nitems = 0;

	Cvar_SetValue("r_detail", Clamp(m_r_detail->value, 0.0f, 3.0f));
	Cvar_SetValue("gl_minlight", Clamp(m_gl_minlight->value, 0.0f, 32.0f)); //mxd
	Cvar_SetValue("vid_maxfps", Clamp(vid_maxfps->value, 30.0f, 240.0f)); //mxd
	Cvar_SetValue("r_antialiasing", Clamp(Cvar_VariableValue("r_antialiasing"), 0.0f, 2.0f));
	Cvar_SetValue("r_colorprofile", Clamp(Cvar_VariableValue("r_colorprofile"), 0.0f, 3.0f));
	Cvar_SetValue("r_reflections", (Cvar_VariableValue("r_reflections") != 0.0f) ? 1.0f : 0.0f);
	Cvar_SetValue("r_reflections_res", Clamp(Cvar_VariableValue("r_reflections_res"), 0.0f, 1.0f));
	Cvar_SetValue("gl_dlight_scale", Clamp((float)(int)(Cvar_VariableValue("gl_dlight_scale") + 0.5f), 0.0f, 6.0f));
	Cvar_SetValue("r_shadows", (Cvar_VariableValue("r_shadows") != 0.0f) ? 1.0f : 0.0f);

	Com_sprintf(name_color_profile, sizeof(name_color_profile), "\x02%s", m_item_color_profile->string);
	s_color_profile_list.generic.type = MTYPE_SPINCONTROL;
	s_color_profile_list.generic.x = 0;
	s_color_profile_list.generic.y = 40;
	s_color_profile_list.generic.name = name_color_profile;
	s_color_profile_list.generic.width = re.BF_Strlen(name_color_profile);
	s_color_profile_list.generic.flags = QMF_SINGLELINE;
	s_color_profile_list.generic.callback = UpdateColorProfileFunc;
	s_color_profile_list.curvalue = (int)Cvar_VariableValue("r_colorprofile");
	s_color_profile_list.itemnames = color_profile_names;

	Com_sprintf(name_water_reflections, sizeof(name_water_reflections), "\x02%s", m_item_water_reflections->string);
	s_water_reflections_list.generic.type = MTYPE_SPINCONTROL;
	s_water_reflections_list.generic.x = 0;
	s_water_reflections_list.generic.y = 60;
	s_water_reflections_list.generic.name = name_water_reflections;
	s_water_reflections_list.generic.width = re.BF_Strlen(name_water_reflections);
	s_water_reflections_list.generic.flags = QMF_SINGLELINE;
	s_water_reflections_list.generic.callback = UpdateWaterReflectionsFunc;
	s_water_reflections_list.curvalue = (Cvar_VariableValue("r_reflections") != 0.0f) ? 0 : 1;
	s_water_reflections_list.itemnames = on_off_names;

	Com_sprintf(name_water_reflection_res, sizeof(name_water_reflection_res), "\x02%s", m_item_water_reflection_res->string);
	s_water_reflection_res_list.generic.type = MTYPE_SPINCONTROL;
	s_water_reflection_res_list.generic.x = 0;
	s_water_reflection_res_list.generic.y = 80;
	s_water_reflection_res_list.generic.name = name_water_reflection_res;
	s_water_reflection_res_list.generic.width = re.BF_Strlen(name_water_reflection_res);
	s_water_reflection_res_list.generic.flags = QMF_SINGLELINE;
	s_water_reflection_res_list.generic.callback = UpdateWaterReflectionResFunc;
	s_water_reflection_res_list.curvalue = (int)Clamp(Cvar_VariableValue("r_reflections_res"), 0.0f, 1.0f);
	s_water_reflection_res_list.itemnames = reflection_res_names;

	Com_sprintf(name_light_emission, sizeof(name_light_emission), "\x02%s", m_item_light_emission->string);
	s_light_emission_list.generic.type = MTYPE_SPINCONTROL;
	s_light_emission_list.generic.x = 0;
	s_light_emission_list.generic.y = 100;
	s_light_emission_list.generic.name = name_light_emission;
	s_light_emission_list.generic.width = re.BF_Strlen(name_light_emission);
	s_light_emission_list.generic.flags = QMF_SINGLELINE;
	s_light_emission_list.generic.callback = UpdateLightEmissionFunc;
	s_light_emission_list.curvalue = (int)Clamp((float)(int)(Cvar_VariableValue("gl_dlight_scale") + 0.5f), 0.0f, 6.0f);
	s_light_emission_list.itemnames = light_emission_names;

	Com_sprintf(name_shadows, sizeof(name_shadows), "\x02%s", m_item_shadows->string);
	s_shadows_list.generic.type = MTYPE_SPINCONTROL;
	s_shadows_list.generic.x = 0;
	s_shadows_list.generic.y = 120;
	s_shadows_list.generic.name = name_shadows;
	s_shadows_list.generic.width = re.BF_Strlen(name_shadows);
	s_shadows_list.generic.flags = QMF_SINGLELINE;
	s_shadows_list.generic.callback = UpdateShadowsFunc;
	s_shadows_list.curvalue = (Cvar_VariableValue("r_shadows") != 0.0f) ? 0 : 1;
	s_shadows_list.itemnames = on_off_names;

	Com_sprintf(name_hd_mode, sizeof(name_hd_mode), "\x02%s", m_item_hd_mode->string);
	s_hd_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_hd_mode_list.generic.x = 0;
	s_hd_mode_list.generic.y = 0;
	s_hd_mode_list.generic.name = name_hd_mode;
	s_hd_mode_list.generic.width = re.BF_Strlen(name_hd_mode);
	s_hd_mode_list.generic.flags = QMF_SINGLELINE;
	s_hd_mode_list.generic.callback = UpdateHDModeFunc;
	s_hd_mode_list.curvalue = (int)(Cvar_VariableValue("r_hd_textures") != 0.0f);
	s_hd_mode_list.itemnames = yes_no_names;

	Com_sprintf(name_antialiasing, sizeof(name_antialiasing), "\x02%s", m_item_antialiasing->string);
	s_antialiasing_list.generic.type = MTYPE_SPINCONTROL;
	s_antialiasing_list.generic.x = 0;
	s_antialiasing_list.generic.y = 20;
	s_antialiasing_list.generic.name = name_antialiasing;
	s_antialiasing_list.generic.width = re.BF_Strlen(name_antialiasing);
	s_antialiasing_list.generic.flags = QMF_SINGLELINE;
	s_antialiasing_list.generic.callback = UpdateAntiAliasingFunc;
	s_antialiasing_list.curvalue = (int)Cvar_VariableValue("r_antialiasing");
	s_antialiasing_list.itemnames = antialiasing_names;

	Com_sprintf(name_driver, sizeof(name_driver), "\x02%s", m_item_driver->string);
	s_ref_list.generic.type = MTYPE_SPINCONTROL;
	s_ref_list.generic.x = 0;
	s_ref_list.generic.y = 40;
	s_ref_list.generic.name = name_driver;
	s_ref_list.generic.width = re.BF_Strlen(name_driver);
	s_ref_list.curvalue = initial_reflib_index;
	s_ref_list.itemnames = ref_list_titles;

	Com_sprintf(name_vidmode, sizeof(name_vidmode), "\x02%s", m_item_vidmode->string);
	s_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_mode_list.generic.x = 0;
	s_mode_list.generic.y = 160;
	s_mode_list.generic.name = name_vidmode;
	s_mode_list.generic.width = re.BF_Strlen(name_vidmode);
	s_mode_list.curvalue = initial_vid_mode;
	s_mode_list.itemnames = vid_mode_titles;

	Com_sprintf(name_target_fps, sizeof(name_target_fps), "\x02%s", m_item_target_fps->string);
	s_target_fps_list.generic.type = MTYPE_SPINCONTROL;
	s_target_fps_list.generic.x = 0;
	s_target_fps_list.generic.y = 200;
	s_target_fps_list.generic.name = name_target_fps;
	s_target_fps_list.generic.width = re.BF_Strlen(name_target_fps);
	s_target_fps_list.generic.flags = QMF_SINGLELINE;
	s_target_fps_list.generic.callback = UpdateTargetFPSFunc;
	s_target_fps_list.itemnames = target_fps_names;

	// Find matching FPS value index.
	s_target_fps_list.curvalue = 1; // Default to 60 FPS.
	for (int i = 0; i < 6; i++)
	{
		if ((int)vid_maxfps->value == target_fps_values[i])
		{
			s_target_fps_list.curvalue = i;
			break;
		}
	}

	Com_sprintf(name_gamma, sizeof(name_gamma), "\x02%s", m_item_gamma->string);
	s_gamma_slider.generic.type = MTYPE_SLIDER;
	s_gamma_slider.generic.flags = QMF_SELECT_SOUND;
	s_gamma_slider.generic.x = 0;
	s_gamma_slider.generic.y = 220;
	s_gamma_slider.generic.name = name_gamma;
	s_gamma_slider.generic.width = re.BF_Strlen(name_gamma);
	s_gamma_slider.generic.callback = UpdateGammaFunc;
	s_gamma_slider.minvalue = 0.0f;
	s_gamma_slider.maxvalue = 16.0f;
	s_gamma_slider.curvalue = 16.0f - vid_gamma->value * 16.0f;

	Com_sprintf(name_brightness, sizeof(name_brightness), "\x02%s", m_item_brightness->string);
	s_brightness_slider.generic.type = MTYPE_SLIDER;
	s_brightness_slider.generic.flags = QMF_SELECT_SOUND;
	s_brightness_slider.generic.x = 0;
	s_brightness_slider.generic.y = 260;
	s_brightness_slider.generic.name = name_brightness;
	s_brightness_slider.generic.width = re.BF_Strlen(name_brightness);
	s_brightness_slider.generic.callback = UpdateBrightnessFunc;
	s_brightness_slider.minvalue = 0.0f;
	s_brightness_slider.maxvalue = 16.0f;
	s_brightness_slider.curvalue = vid_brightness->value * 16.0f;

	Com_sprintf(name_contrast, sizeof(name_contrast), "\x02%s", m_item_contrast->string);
	s_contrast_slider.generic.type = MTYPE_SLIDER;
	s_contrast_slider.generic.flags = QMF_SELECT_SOUND;
	s_contrast_slider.generic.x = 0;
	s_contrast_slider.generic.y = 300;
	s_contrast_slider.generic.name = name_contrast;
	s_contrast_slider.generic.width = re.BF_Strlen(name_contrast);
	s_contrast_slider.generic.callback = UpdateContrastFunc;
	s_contrast_slider.minvalue = 1.6f;
	s_contrast_slider.maxvalue = 14.4f;
	s_contrast_slider.curvalue = vid_contrast->value * 16.0f;

	// YQ2
	Com_sprintf(name_minlight, sizeof(name_minlight), "\x02%s", m_item_minlight->string);
	s_minlight_slider.generic.type = MTYPE_SLIDER;
	s_minlight_slider.generic.flags = QMF_SELECT_SOUND;
	s_minlight_slider.generic.x = 0;
	s_minlight_slider.generic.y = 340;
	s_minlight_slider.generic.name = name_minlight;
	s_minlight_slider.generic.width = re.BF_Strlen(name_minlight);
	s_minlight_slider.generic.callback = UpdateMinlightFunc;
	s_minlight_slider.minvalue = 0.0f;
	s_minlight_slider.maxvalue = 8.0f;
	s_minlight_slider.curvalue = m_gl_minlight->value * 0.25f;

	Com_sprintf(name_detail, sizeof(name_detail), "\x02%s", m_item_detail->string);
	s_detail_slider.generic.type = MTYPE_SLIDER;
	s_detail_slider.generic.flags = QMF_SELECT_SOUND; //mxd. QMF_SELECT_SOUND flag was missing in original version.
	s_detail_slider.generic.x = 0;
	s_detail_slider.generic.y = 380;
	s_detail_slider.generic.name = name_detail;
	s_detail_slider.generic.width = re.BF_Strlen(name_detail);
	s_detail_slider.generic.callback = UpdateDetailFunc;
	s_detail_slider.minvalue = 0.0f;
	s_detail_slider.maxvalue = 3.0f;
	s_detail_slider.curvalue = m_r_detail->value; //mxd. Original version used Cvar_VariableValue("r_detail") here.

	Menu_AddItem(&s_video_menu, &s_hd_mode_list);
	Menu_AddItem(&s_video_menu, &s_antialiasing_list);
	Menu_AddItem(&s_video_menu, &s_color_profile_list);
	Menu_AddItem(&s_video_menu, &s_water_reflections_list);
	Menu_AddItem(&s_video_menu, &s_water_reflection_res_list);
	Menu_AddItem(&s_video_menu, &s_light_emission_list);
	Menu_AddItem(&s_video_menu, &s_shadows_list);
	Menu_AddItem(&s_video_menu, &s_mode_list);
	Menu_AddItem(&s_video_menu, &s_target_fps_list); //mxd
	Menu_AddItem(&s_video_menu, &s_gamma_slider);
	Menu_AddItem(&s_video_menu, &s_brightness_slider);
	Menu_AddItem(&s_video_menu, &s_contrast_slider);
	Menu_AddItem(&s_video_menu, &s_minlight_slider); // YQ2
	Menu_AddItem(&s_video_menu, &s_detail_slider);

	Menu_Center(&s_video_menu);
}

static void VID_MenuDraw(void)
{
	char title[MAX_QPATH];

	// Draw menu BG.
	Menu_DrawBG("book/back/b_conback8.bk", cls.m_menuscale);

	if (cls.m_menualpha == 0.0f)
		return;

	// Draw menu title.
	Com_sprintf(title, sizeof(title), "\x03%s", m_banner_video->string);
	const int x = M_GetMenuLabelX(re.BF_Strlen(title));
	const int y = M_GetMenuOffsetY(&s_video_menu);
	re.DrawBigFont(x, y, title, cls.m_menualpha);

	s_video_menu.x = M_GetMenuLabelX(s_video_menu.width);
	Menu_AdjustCursor(&s_video_menu, 1);
	Menu_Draw(&s_video_menu);
}

static const char* VID_MenuKey(const int key)
{
	if (cls.m_menustate != MS_OPENED)
		return NULL;

	switch (key)
	{
		case K_ENTER:
		case K_KP_ENTER:
			ApplyChanges(false);
			return SND_MENU_ENTER;

		case K_ESCAPE:
			ApplyChanges(true);
			return SND_MENU_CLOSE;

		case K_UPARROW:
		case K_KP_UPARROW:
		case K_AUX8:	// D-pad up
		case 'w':
		case 'W':
			s_video_menu.cursor--;
			Menu_AdjustCursor(&s_video_menu, -1);
			return SND_MENU_SELECT;

		case K_DOWNARROW:
		case K_KP_DOWNARROW:
		case K_AUX9:	// D-pad down
		case 's':
		case 'S':
			s_video_menu.cursor++;
			Menu_AdjustCursor(&s_video_menu, 1);
			return SND_MENU_SELECT;

		case K_LEFTARROW:
		case K_KP_LEFTARROW:
		case K_AUX10:	// D-pad left
		case 'a':
		case 'A':
			//mxd. Original logic calls se.StopAllSounds_Sounding() here - no longer needed.
			return (Menu_SlideItem(&s_video_menu, -1) ? SND_MENU_TOGGLE : NULL); //mxd. Add sound.

		case K_RIGHTARROW:
		case K_KP_RIGHTARROW:
		case K_AUX11:	// D-pad right
		case 'd':
		case 'D':
			//mxd. Original logic calls se.StopAllSounds_Sounding() here - no longer needed.
			return (Menu_SlideItem(&s_video_menu, 1) ? SND_MENU_TOGGLE : NULL); //mxd. Add sound.

		default:
			break;
	}

	return NULL;
}

// Q2 counterpart
void M_Menu_Video_f(void)
{
	VID_MenuInit();
	M_PushMenu(VID_MenuDraw, VID_MenuKey);
}