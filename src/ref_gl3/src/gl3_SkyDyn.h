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
