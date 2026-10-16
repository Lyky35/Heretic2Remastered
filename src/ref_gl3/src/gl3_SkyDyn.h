//
// gl3_SkyDyn.h
//
// Dynamic sky + volumetric clouds (Phase 0 scaffold + Phase 1 Tier-A).
// Port plan: DYNAMIC_SKY_PLAN.md — source: fede-vaccaro/TerrainEngine-OpenGL.
//

#pragma once

#include "gl3_Local.h"

// Register r_dynsky / r_volclouds and tuning cvars. Called from R_Register().
extern void GL3_SkyDyn_RegisterCvars(void);

// One-time init (Tier-A program + compute-capability probe; Phase 2+ adds
// noise textures and cloud FBOs). Always succeeds; Tier B is disabled with a
// console message if GL < 4.3, Tier A falls back to legacy sky if its program
// fails to compile. Called after GL3_InitShaders() in RI_Init().
extern qboolean GL3_SkyDyn_Init(void);

// Release Tier A/B resources. Called in RI_Shutdown() before GL3_ShutdownShaders().
extern void GL3_SkyDyn_Shutdown(void);

// Runtime gates (read cvar values; safe before init).
extern qboolean GL3_SkyDyn_Enabled(void);           // r_dynsky  (Tier A: gradient sky)
extern qboolean GL3_SkyDyn_VolumetricEnabled(void); // r_volclouds (Tier B: volumetric clouds)

// True when the Tier-A gradient program compiled and is ready to draw.
extern qboolean GL3_SkyDyn_TierAReady(void);

// Draw the Tier-A fullscreen gradient + sun sky (programmable replacement for
// the 6 textured quads). Caller (R_DrawSkyBox) keeps depth writes off, modelview
// uploaded for aurora/stars, and draws the particle tail afterwards.
// No-op if Tier A is not ready.
extern void GL3_SkyDyn_DrawTierA(void);

// Per-frame maintenance: regenerates the weather map when r_cloud_seed /
// r_cloud_freq changed (perlin/worley are generated once). Cheap (float compares).
// Called at the top of R_DrawSkyBox.
extern void GL3_SkyDyn_Update(void);

// Fullscreen noise debug view (r_cloud_debug 1 = weather, 2 = perlin slice,
// 3 = worley slice). Returns true when a view was drawn (caller skips sky).
extern qboolean GL3_SkyDyn_DrawDebug(void);

// Fog-density multiplier for the current map (1.0 = untouched). The server
// owns r_fog_density (per-frame lerp), so levels scale it at consumption time
// in R_Fog() instead of latching the cvar.
extern float GL3_SkyDyn_FogScale(void);

// Wave-height multiplier for the current map (1.0 = untouched). Water reads
// r_water_wave_height live per draw, so levels scale it at upload time in
// GL3_DrawWaterPoly() instead of latching the cvar.
extern float GL3_SkyDyn_WaveScale(void);

// Tier-B volumetric clouds (Phase 4): raymarch at r_cloud_res scale into an
// offscreen buffer, then alpha-blend over the sky. Runs in the reflection
// pass too (mirrored camera state is current there), so water reflects clouds.
// Caller must have depth writes off + cull off.
extern void GL3_SkyDyn_DrawClouds(void);

// Per-surface PBR sky feed (P1): live preset colors + sun/moon light in H2
// Z-up world space (moon at night, dimmed). Presets are static data, so this
// works with r_dynsky 0 too.
extern void GL3_SkyDyn_PbrSky(float* top, float* bottom, float* lightDirW, float* lightColor);
// Per-frame PBR uniform upload (strength from r_pbr, debug from r_pbr_debug;
// sun transformed to view space via r_world_matrix). Call from R_DrawWorld.
extern void GL3_SkyDyn_UpdatePbr(void);
