//
// gl3_SkyDyn.h
//
// Dynamic sky + volumetric clouds scaffold (Phase 0).
// Port plan: DYNAMIC_SKY_PLAN.md — source: fede-vaccaro/TerrainEngine-OpenGL.
// This phase registers cvars and init/shutdown hooks only; no visual change.
//

#pragma once

#include "gl3_Local.h"

// Register r_dynsky / r_volclouds and tuning cvars. Called from R_Register().
extern void GL3_SkyDyn_RegisterCvars(void);

// One-time init (compute-capability probe, future noise/FBO setup).
// Always succeeds; disables Tier B with a console message if GL < 4.3.
// Called after GL3_InitShaders() in RI_Init().
extern qboolean GL3_SkyDyn_Init(void);

// Release Tier A/B resources. Called in RI_Shutdown() before GL3_ShutdownShaders().
extern void GL3_SkyDyn_Shutdown(void);

// Runtime gates (read cvar values; safe before init).
extern qboolean GL3_SkyDyn_Enabled(void);       // r_dynsky  (Tier A: gradient sky)
extern qboolean GL3_SkyDyn_VolumetricEnabled(void); // r_volclouds (Tier B: volumetric clouds)
