//
// gl3_Warp.h
//
// OpenGL 3.3 warped surfaces.
// Based on gl1_Warp.h -- Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

extern void R_EmitWaterPolys(const msurface_t* fa, qboolean undulate, qboolean use_reflect);
extern void R_EmitUnderwaterPolys(const msurface_t* fa);
extern void R_EmitQuakeFloorPolys(const msurface_t* fa);
extern void R_SubdivideSurface(const model_t* mdl, msurface_t* fa);

// Interaction ripples (W1): CPU pool fed by warp-only billboard twins.
// R_UpdateWaterRipples() scans entities pre-world-draw; R_GetWaterRipples()
// fills the vec4 array (x, y, birth, amplitude) for the water uniforms.
#define MAX_WATER_RIPPLES 24
extern void R_UpdateWaterRipples(void);
extern int R_GetWaterRipples(float out[MAX_WATER_RIPPLES][4]);
