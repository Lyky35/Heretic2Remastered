//
// gl3_SkyDyn.c
//
// Dynamic sky + volumetric clouds scaffold (Phase 0).
// Port plan: DYNAMIC_SKY_PLAN.md — source: fede-vaccaro/TerrainEngine-OpenGL.
//
// Phase 0 scope: cvars + init/shutdown plumbing only. No draw-path changes,
// so r_dynsky 0 (default) is pixel-identical to current behavior.
//

#include "gl3_SkyDyn.h"

// Cvar storage. Externs live in gl3_Local.h; definitions live in gl3_Main.c
// (same pattern as r_reflections / r_fog_*).

void GL3_SkyDyn_RegisterCvars(void)
{
	// Master switches (default off -> no visual change).
	r_dynsky = ri.Cvar_Get("r_dynsky", "0", CVAR_ARCHIVE);
	r_volclouds = ri.Cvar_Get("r_volclouds", "0", CVAR_ARCHIVE);

	// Tier A: gradient-sky preset. 0 = day, 1 = sunset, 2 = auto by sun height.
	r_sky_preset = ri.Cvar_Get("r_sky_preset", "2", CVAR_ARCHIVE);

	// Tier B tuning (TerrainEngine defaults; rescaled later in Phase 4).
	r_cloud_res = ri.Cvar_Get("r_cloud_res", "0.5", CVAR_ARCHIVE); // render scale
	r_cloud_coverage = ri.Cvar_Get("r_cloud_coverage", "0.45", CVAR_ARCHIVE);
	r_cloud_speed = ri.Cvar_Get("r_cloud_speed", "450.0", CVAR_ARCHIVE);
	r_cloud_density = ri.Cvar_Get("r_cloud_density", "0.02", CVAR_ARCHIVE);
	r_cloud_crisp = ri.Cvar_Get("r_cloud_crisp", "40.0", CVAR_ARCHIVE);
	r_cloud_curl = ri.Cvar_Get("r_cloud_curl", "0.1", CVAR_ARCHIVE);
	r_cloud_absorb = ri.Cvar_Get("r_cloud_absorb", "0.35", CVAR_ARCHIVE);
	r_cloud_seed = ri.Cvar_Get("r_cloud_seed", "0", CVAR_ARCHIVE);
	r_cloud_debug = ri.Cvar_Get("r_cloud_debug", "0", 0);
	r_cloud_blur = ri.Cvar_Get("r_cloud_blur", "1", CVAR_ARCHIVE);
	r_cloud_godrays = ri.Cvar_Get("r_cloud_godrays", "0", CVAR_ARCHIVE);
}

qboolean GL3_SkyDyn_Init(void)
{
	// Probe compute-shader support. Context is 4.6 Core (gl3_SDL.c), but
	// check at runtime so old drivers fail soft instead of crashing.
	if (!GLAD_GL_VERSION_4_3)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: GL 4.3+ not available (compute missing) — volumetric clouds disabled, gradient sky still available.\n");
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: scaffold init (GL 4.3+ compute available). r_dynsky/r_volclouds default off — no visual change.\n");
	}

	// Phase 1+ will allocate Tier A/B programs, noise textures and cloud FBOs here.
	return true;
}

void GL3_SkyDyn_Shutdown(void)
{
	// Phase 1+ will delete programs/textures/FBOs here. Nothing allocated yet.
}

qboolean GL3_SkyDyn_Enabled(void)
{
	return (r_dynsky != NULL && (int)r_dynsky->value != 0) ? true : false;
}

qboolean GL3_SkyDyn_VolumetricEnabled(void)
{
	if (r_volclouds == NULL || (int)r_volclouds->value == 0)
		return false;
	if (!GLAD_GL_VERSION_4_3)
		return false;
	return GL3_SkyDyn_Enabled();
}
