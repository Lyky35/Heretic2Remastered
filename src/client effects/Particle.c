//
// Particle.c
//
// Copyright 1998 Raven Software
//

#include "Particle.h"
#include "Client Effects.h"
#include "ResourceManager.h"
#include "Random.h"
#include "Vector.h"
#include "g_playstats.h"

static ResourceManager_t particle_manager;

void InitParticleMngr(void)
{
#define PARTICLE_BLOCK_SIZE 256

	ResMngr_Con(&particle_manager, sizeof(client_particle_t), PARTICLE_BLOCK_SIZE);
}

void ReleaseParticleMngr(void)
{
	ResMngr_Des(&particle_manager);
}

void AddParticleToList(client_entity_t* ce, client_particle_t* fx)
{
	assert(ce);
	assert(fx);

	fx->next = ce->p_root;
	ce->p_root = fx;
}

void RemoveParticleList(client_particle_t** root)
{
	assert(root);

	client_particle_t* next = *root;
	while (next != NULL)
	{
		client_particle_t* to_free = next;
		next = next->next;
		ResMngr_DeallocateResource(&particle_manager, to_free, sizeof(*to_free));
	}

	*root = NULL;
}

int AddParticlesToView(client_entity_t* ce)
{
	client_particle_t* current;
	client_particle_t** prev;
	particle_t* r;
	int part_info;
	float yaw;
	float pitch;
	float radius;
	float offset; //mxd

	assert(ce->p_root);

	int numparticles = 0;
	const qboolean cull_particles = (R_DETAIL == DETAIL_LOW);
	const float maxdepth2 = r_farclipdist->value * r_farclipdist->value;
	const float mindepth2 = r_nearclipdist->value * r_nearclipdist->value;

	for (prev = &ce->p_root, current = ce->p_root; current != NULL; current = current->next)
	{
#ifdef _DEBUG
		const int ptype = (current->type & PFL_FLAG_MASK);
		assert(ptype < NUM_PARTICLE_TYPES);
#endif

		const int d_msec = fx_time - current->startTime;
		const float d_time = (float)d_msec * 0.001f;
		int alpha = current->color.a + (int)(d_time * current->d_alpha);

		// PULSE ALPHA means that once alpha is at max, reverse and head back down.
		if (alpha > 255 && ((current->type & PFL_PULSE_ALPHA) || (ce->flags & CEF_PULSE_ALPHA)))
			alpha = 255 - (alpha - 255); // A weird thing to do, but necessary because the alpha is based off a dtime from the CREATION of the particle.

		// Add to additive particle list.
		if ((ce->flags & CEF_ADDITIVE_PARTS) || (current->type & PFL_ADDITIVE))
		{
			if (fxi.cls->r_anumparticles >= MAX_PARTICLES)
				return numparticles;

			r = &fxi.cls->r_aparticles[fxi.cls->r_anumparticles];
			part_info = 1;
		}
		else
		{
			if (fxi.cls->r_numparticles >= MAX_PARTICLES)
				return numparticles;

			r = &fxi.cls->r_particles[fxi.cls->r_numparticles];
			part_info = 2;
		}

		r->type = current->type;
		r->color.c = current->color.c;
		r->color.a = (byte)min(255, alpha);
		r->scale = current->scale + (d_time * current->d_scale);

		const float d_time2 = d_time * d_time * 0.5f;
		// Bounce particles are integrated per-frame in UpdateParticles; draw
		// their integrated origin instead of the closed-form ballistic path.
		const qboolean bounce = ((current->type & PFL_BOUNCE) && r_particle_bounce->value > 0.0f);

		if (ce->flags & CEF_ABSOLUTE_PARTS)
		{
			for (int i = 0; i < 3; i++)
				r->origin[i] = bounce ? current->origin[i]
					: current->origin[i] + (current->velocity[i] * d_time) + (current->acceleration[i] * d_time2);
		}
		else
		{
			switch (current->type & PFL_MOVE_MASK)
			{
				case PFL_MOVE_SPHERE:
					yaw =    current->origin[SPH_YAW]    + (current->velocity[SPH_YAW]    * d_time) + (current->acceleration[SPH_YAW]    * d_time2);
					pitch =  current->origin[SPH_PITCH]  + (current->velocity[SPH_PITCH]  * d_time) + (current->acceleration[SPH_PITCH]  * d_time2); //mxd. Fixed: was using SPH_YAW.
					radius = current->origin[SPH_RADIUS] + (current->velocity[SPH_RADIUS] * d_time) + (current->acceleration[SPH_RADIUS] * d_time2);
					r->origin[0] = ce->r.origin[0] + cosf(yaw) * cosf(pitch) * radius;
					r->origin[1] = ce->r.origin[1] + sinf(yaw) * cosf(pitch) * radius;
					r->origin[2] = ce->r.origin[2] + sinf(pitch) * radius;
					break;

				case PFL_MOVE_CYL_X:
					offset = current->origin[CYL_Z]      + (current->velocity[CYL_Z] * d_time)      + (current->acceleration[CYL_Z]      * d_time2);
					yaw =    current->origin[CYL_YAW]    + (current->velocity[CYL_YAW] * d_time)    + (current->acceleration[CYL_YAW]    * d_time2);
					radius = current->origin[CYL_RADIUS] + (current->velocity[CYL_RADIUS] * d_time) + (current->acceleration[CYL_RADIUS] * d_time2);
					r->origin[0] = ce->r.origin[0] + offset;
					r->origin[1] = ce->r.origin[1] + cosf(yaw) * radius;
					r->origin[2] = ce->r.origin[2] + sinf(yaw) * radius;
					break;

				case PFL_MOVE_CYL_Y:
					offset = current->origin[CYL_Z]      + (current->velocity[CYL_Z] * d_time)      + (current->acceleration[CYL_Z]      * d_time2);
					yaw =    current->origin[CYL_YAW]    + (current->velocity[CYL_YAW] * d_time)    + (current->acceleration[CYL_YAW]    * d_time2);
					radius = current->origin[CYL_RADIUS] + (current->velocity[CYL_RADIUS] * d_time) + (current->acceleration[CYL_RADIUS] * d_time2);
					r->origin[0] = ce->r.origin[0] + cosf(yaw) * radius;
					r->origin[1] = ce->r.origin[1] + offset;
					r->origin[2] = ce->r.origin[2] + sinf(yaw) * radius;
					break;

				case PFL_MOVE_CYL_Z:
					offset = current->origin[CYL_Z]      + (current->velocity[CYL_Z] * d_time)      + (current->acceleration[CYL_Z]      * d_time2);
					yaw =    current->origin[CYL_YAW]    + (current->velocity[CYL_YAW] * d_time)    + (current->acceleration[CYL_YAW]    * d_time2);
					radius = current->origin[CYL_RADIUS] + (current->velocity[CYL_RADIUS] * d_time) + (current->acceleration[CYL_RADIUS] * d_time2);
					r->origin[0] = ce->r.origin[0] + cosf(yaw) * radius;
					r->origin[1] = ce->r.origin[1] + sinf(yaw) * radius;
					r->origin[2] = ce->r.origin[2] + offset;
					break;

			case PFL_MOVE_NORM:
			default:
				for (int i = 0; i < 3; i++)
					r->origin[i] = ce->r.origin[i] + (bounce ? current->origin[i]
						: current->origin[i] + (current->velocity[i] * d_time) + (current->acceleration[i] * d_time2));
				break;
			}
		}

		if (cull_particles || (current->type & PFL_NEARCULL))
		{
			const float depth = VectorSeparationSquared(r->origin, fxi.cl->refdef.vieworg);
			if (depth > maxdepth2 || depth < mindepth2)
				part_info = 0;
		}

		switch (part_info)
		{
			case 0: break;
			case 1: fxi.cls->r_anumparticles++; break;
			case 2: fxi.cls->r_numparticles++; break;

			default:
				assert(0);
				break;
		}

		numparticles++;
		prev = &(*prev)->next;
	}

	return numparticles;
}

// Per-frame world-collision bounce for one particle. Integrates one frame,
// traces the path against the world BSP, and reflects velocity on hit.
// Mutates p->origin / p->velocity / p->bounces.
static void Particle_Bounce(client_entity_t* ce, client_particle_t* p, const float dt)
{
	const float dt2 = dt * dt * 0.5f;
	vec3_t new_origin;
	vec3_t world_prev;
	vec3_t world_new;

	for (int i = 0; i < 3; i++)
		new_origin[i] = p->origin[i] + p->velocity[i] * dt + p->acceleration[i] * dt2;

	for (int i = 0; i < 3; i++)
	{
		world_prev[i] = ce->r.origin[i] + p->origin[i];
		world_new[i]  = ce->r.origin[i] + new_origin[i];
	}

	trace_t trace;
	fxi.Trace(world_prev, vec3_origin, vec3_origin, world_new, MASK_SOLID, CTF_CLIP_TO_WORLD, &trace);

	if (trace.fraction >= 1.0f || trace.allsolid)
	{
		// Free flight — accept the step and apply gravity.
		for (int i = 0; i < 3; i++)
			p->velocity[i] += p->acceleration[i] * dt;
		VectorCopy(new_origin, p->origin);
		return;
	}

	// Hit — clamp to the surface.
	VectorCopy(trace.endpos, world_new);
	VectorSubtract(world_new, ce->r.origin, p->origin);

	if (p->bounces >= PARTICLE_MAX_BOUNCES)
	{
		// Done bouncing: settle (stop gravity so it rests on the surface).
		VectorClear(p->velocity);
		VectorClear(p->acceleration);
		return;
	}

	p->bounces++;

	// Reflect with restitution + friction: v' = friction*v - (friction+e)*dot(v,n)*n
	vec3_t normal;
	VectorCopy(trace.plane.normal, normal);
	const float vn = DotProduct(p->velocity, normal);
	for (int i = 0; i < 3; i++)
		p->velocity[i] = PARTICLE_FRICTION * p->velocity[i]
			- (PARTICLE_FRICTION + PARTICLE_RESTITUTION) * vn * normal[i];

	// Keep gravity acting so it settles after the bounce.
	for (int i = 0; i < 3; i++)
		p->velocity[i] += p->acceleration[i] * dt;
}

int UpdateParticles(client_entity_t* ce)
{
	client_particle_t* current;
	client_particle_t** prev;

	assert(ce->p_root);

	int numparticles = 0;

	for (prev = &ce->p_root, current = ce->p_root; current != NULL; current = current->next)
	{
		const int d_msec = fx_time - current->startTime;
		const float d_time = (float)d_msec * 0.001f;
		int alpha = current->color.a + (int)(d_time * current->d_alpha);

		// PULSE ALPHA means that once alpha is at max, reverse and head back down.
		if (alpha > 255 && ((ce->flags & CEF_PULSE_ALPHA) || (current->type & PFL_PULSE_ALPHA)))
		{
			// A weird thing to do, but necessary because the alpha is based off a dtime from the CREATION of the particle.
			alpha = 255 - (alpha - 255);
		}

		// Per-frame world bounce for flagged particles.
		if ((current->type & PFL_BOUNCE) && r_particle_bounce->value > 0.0f)
			Particle_Bounce(ce, current, fxi.cls->rframetime);

		if (d_msec > current->duration || alpha <= 0)
		{
			*prev = current->next;
			ResMngr_DeallocateResource(&particle_manager, current, sizeof(*current));

			// current = current->next is still valid in the for loop.
			// A deallocated resource is guaranteed not to be changed until it is reallocated, when the manager is not shared between threads.
			continue;
		}

		prev = &(*prev)->next;
		numparticles++;
	}

	return numparticles;
}

// This frees all particles attached to the client entity.
void FreeParticles(client_entity_t* ce)
{
	client_particle_t* current;
	client_particle_t** prev;

	for (prev = &ce->p_root, current = ce->p_root; current != NULL; current = current->next)
	{
		*prev = current->next;
		ResMngr_DeallocateResource(&particle_manager, current, sizeof(*current));
	}
}

client_particle_t* ClientParticle_new(const int type, const paletteRGBA_t color, const int duration)
{
	client_particle_t* p = ResMngr_AllocateResource(&particle_manager, sizeof(client_particle_t));
	memset(p, 0, sizeof(client_particle_t));

	p->acceleration[2] = -PARTICLE_GRAVITY;
	p->startTime = fx_time;
	p->duration = duration;
	p->type = type;
	p->scale = 1.0f;
	p->color = color;
	p->d_alpha = -255.0f / (flrand(0.8f, 1.0f) * (float)duration / 1000.0f);

	return p;
}