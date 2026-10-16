//
// gl3_Warp.c
//
// OpenGL 3.3 warped surfaces rendering.
// Based on gl1_Warp.c -- Copyright 1998 Raven Software
//

#include "gl3_Warp.h"
#include "gl3_Shaders.h"
#include "Hunk.h"
#include "turbsin.h"
#include "Vector.h"
#include "q_Sprite.h"

#define SUBDIVIDE_SIZE	8.0f  // Dense warp mesh for ripple displacement: with the
                             // 8.0f minimum-split guard this bottoms out at ~8-16u pieces
                             // (~2-4x the 16u count). Design ripple wavelengths >= 24u
                             // (3+ quads per wave); per-pixel normals stay the detail
                             // carrier. Below 8u would need guard changes — not worth it.

#pragma region ========================== POLYGON GENERATION ==========================

// Build a 9-float (pos3+tc2+col4) vertex from glpoly vert + explicit s,t + pos_z override.
static void WriteWarpVert(float* out, const float* v, float s, float t, float pz)
{
	out[0] = v[0]; out[1] = v[1]; out[2] = pz;
	out[3] = s;    out[4] = t;
	out[5] = 1.0f; out[6] = 1.0f; out[7] = 1.0f; out[8] = 1.0f;
}

void R_EmitWaterPolys(const msurface_t* fa, const qboolean undulate, const qboolean use_reflect)
{
	float scroll;

	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64.0f * ((r_newrefdef.time * 0.5f) - floorf(r_newrefdef.time * 0.5f));
	else
		scroll = 0.0f;

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		float vbuf[64 * 9];
		const float* v = p->verts[0];

		for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE)
		{
			const float os = v[3];
			const float ot = v[4];

			float s = os + turbsin[(int)((ot * 0.125f + r_newrefdef.time) * TURBSCALE) & 255];
			s += scroll;
			s /= 64.0f;

			float t = ot + turbsin[(int)((os * 0.125f + r_newrefdef.time) * TURBSCALE) & 255];
			t /= 64.0f;

			float pz = v[2];
			if (undulate)
				pz += turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * 0.25f +
				      turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * 0.125f;

			WriteWarpVert(&vbuf[i * 9], v, s, t, pz);
		}

		// Always use the water shader so the Gerstner waves and refraction are
		// applied; reflection is only sampled when this frame's planar
		// reflection is valid (use_reflect).
		GL3_DrawWaterPoly(GL_TRIANGLE_FAN, vbuf, p->numverts, use_reflect);
	}
}

void R_EmitUnderwaterPolys(const msurface_t* fa)
{
	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		float vbuf[64 * 9];
		const float* v = p->verts[0];

		for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE)
		{
			const float pz = v[2] +
				turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * 0.5f +
				turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * 0.25f;

			WriteWarpVert(&vbuf[i * 9], v, v[3], v[4], pz);
		}

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
	}
}

void R_EmitQuakeFloorPolys(const msurface_t* fa)
{
	const float amount = quake_amount->value * 0.05f;

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		float vbuf[64 * 9];
		const float* v = p->verts[0];

		for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE)
		{
			const float pz = v[2] +
				turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * amount * 0.5f +
				turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * amount * 0.25f;

			WriteWarpVert(&vbuf[i * 9], v, v[3], v[4], pz);
		}

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
	}
}

#pragma endregion

static void R_BoundPoly(const int numverts, const float* verts, vec3_t mins, vec3_t maxs)
{
	ClearBounds(mins, maxs);

	const float* v = verts;
	for (int i = 0; i < numverts; i++)
		for (int j = 0; j < 3; j++, v++)
		{
			mins[j] = min(*v, mins[j]);
			maxs[j] = max(*v, maxs[j]);
		}
}

static void R_SubdividePolygon(msurface_t* warpface, const int numverts, float* verts)
{
	vec3_t mins, maxs;
	vec3_t front[64], back[64];
	float dist[64];
	vec3_t total;

	if (numverts > 60)
		ri.Sys_Error(ERR_DROP, "numverts = %i", numverts);

	R_BoundPoly(numverts, verts, mins, maxs);

	for (int i = 0; i < 3; i++)
	{
		float m = (mins[i] + maxs[i]) * 0.5f;
		m = SUBDIVIDE_SIZE * floorf(m / SUBDIVIDE_SIZE + 0.5f);

		if (maxs[i] - m < 8.0f || m - mins[i] < 8.0f)
			continue;

		float* v = verts + i;
		for (int j = 0; j < numverts; j++, v += 3)
			dist[j] = *v - m;

		dist[numverts] = dist[0];
		v -= i;
		VectorCopy(verts, v);

		int f = 0, b = 0;
		v = verts;
		for (int j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0) { VectorCopy(v, front[f]); f++; }
			if (dist[j] <= 0) { VectorCopy(v, back[b]);  b++; }

			if (dist[j] == 0.0f || dist[j + 1] == 0.0f)
				continue;

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				const float frac = dist[j] / (dist[j] - dist[j + 1]);
				for (int k = 0; k < 3; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);
				f++; b++;
			}
		}

		R_SubdividePolygon(warpface, f, front[0]);
		R_SubdividePolygon(warpface, b, back[0]);
		return;
	}

	glpoly_t* poly = Hunk_Alloc((int)sizeof(glpoly_t) + ((numverts - 4) + 2) * VERTEXSIZE * sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts + 2;
	VectorClear(total);

	float total_s = 0.0f, total_t = 0.0f;

	for (int i = 0; i < numverts; i++, verts += 3)
	{
		VectorCopy(verts, poly->verts[i + 1]);
		const float s = DotProduct(verts, warpface->texinfo->vecs[0]);
		const float t = DotProduct(verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		Vec3AddAssign(verts, total);

		poly->verts[i + 1][3] = s;
		poly->verts[i + 1][4] = t;
	}

	VectorScale(total, 1.0f / (float)numverts, poly->verts[0]);
	poly->verts[0][3] = total_s / (float)numverts;
	poly->verts[0][4] = total_t / (float)numverts;

	memcpy(poly->verts[numverts + 1], poly->verts[1], sizeof(poly->verts[0]));
}

void R_SubdivideSurface(const model_t* mdl, msurface_t* fa)
{
	static vec3_t verts[64];
	float* vec;

	int numverts;
	for (numverts = 0; numverts < fa->numedges; numverts++)
	{
		const int lindex = mdl->surfedges[fa->firstedge + numverts];

		if (lindex > 0)
			vec = mdl->vertexes[mdl->edges[lindex].v[0]].position;
		else
			vec = mdl->vertexes[mdl->edges[-lindex].v[1]].position;

		VectorCopy(vec, verts[numverts]);
	}

	R_SubdividePolygon(fa, numverts, verts[0]);
}

#pragma endregion

#pragma region ========================== INTERACTION RIPPLES (W1) ==========================

// Ripple pool: warp-only billboard twins (swim/wade wakes, splashes, drips)
// register here on first sighting with renderer-clock births; the water shader
// expands analytic rings from them. One frame of latency max, invisible.
#define RIPPLE_MAX_AGE 2.5f // seconds; matches the shader's cutoff

typedef struct
{
	const entity_t* ent; // key; NULL = free slot
	float x, y;          // world XY (H2 Z-up: horizontal plane)
	float birth;         // r_newrefdef.time at first sighting
	float amp;           // from twin size (bigger splash = bigger ripple)
} water_ripple_t;

static water_ripple_t water_ripples[MAX_WATER_RIPPLES];

void R_UpdateWaterRipples(void)
{
	const float now = r_newrefdef.time;
	int i, k;

	// Age out regardless (also when distortion is off and no twins exist).
	for (i = 0; i < MAX_WATER_RIPPLES; i++)
	{
		if (water_ripples[i].ent != NULL && now - water_ripples[i].birth > RIPPLE_MAX_AGE)
			water_ripples[i].ent = NULL;
	}

	if (r_trail_distort == NULL || (int)r_trail_distort->value == 0)
		return; // twins only exist with distortion on; pool drains via age-out above

	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		const entity_t* e = r_newrefdef.entities[i];
		float radius, amp;
		int slot = -1;
		int oldest = -1;
		float oldestBirth = now;

		if (e == NULL)
			continue;
		if (((e->flags & (RF_DISTORT | RF_NOCORE)) != (RF_DISTORT | RF_NOCORE)) ||
			e->spriteType != SPRITE_DYNAMIC)
			continue;

		// World radius from roll-quad extent (RE_SetupRollSprite sizes in world units).
		radius = 0.0f;
		for (k = 0; k < 4; k++)
		{
			float ex = (e->verts[k].x < 0.0f) ? -e->verts[k].x : e->verts[k].x;
			float ey = (e->verts[k].y < 0.0f) ? -e->verts[k].y : e->verts[k].y;
			if (ex > radius)
				radius = ex;
			if (ey > radius)
				radius = ey;
		}
		radius *= e->scale;
		if (radius < 1.0f)
			continue;

		amp = radius * 0.0125f;
		if (amp < 0.2f)
			amp = 0.2f;
		if (amp > 1.2f)
			amp = 1.2f;

		for (k = 0; k < MAX_WATER_RIPPLES; k++)
		{
			if (water_ripples[k].ent == e)
			{
				slot = k;
				break;
			}
			if (water_ripples[k].ent == NULL)
			{
				slot = k; // remember first free, keep looking for a match
			}
			else if (slot < 0 || water_ripples[k].birth < oldestBirth)
			{
				oldest = k;
				oldestBirth = water_ripples[k].birth;
			}
		}
		if (slot < 0)
			slot = oldest; // saturated: steal oldest (ripples are fungible)
		if (slot < 0)
			continue;

		if (water_ripples[slot].ent != e)
		{
			water_ripples[slot].ent = e;
			water_ripples[slot].birth = now;
		}
		water_ripples[slot].x = e->origin[0];
		water_ripples[slot].y = e->origin[1];
		water_ripples[slot].amp = amp;
	}
}

int R_GetWaterRipples(float out[MAX_WATER_RIPPLES][4])
{
	int count = 0;

	for (int i = 0; i < MAX_WATER_RIPPLES; i++)
	{
		if (water_ripples[i].ent == NULL)
			continue;
		out[count][0] = water_ripples[i].x;
		out[count][1] = water_ripples[i].y;
		out[count][2] = water_ripples[i].birth;
		out[count][3] = water_ripples[i].amp;
		count++;
	}

	return count;
}

#pragma endregion
