//
// fx_staff.c
//
// Copyright 1998 Raven Software
//

#include "Client Effects.h"
#include "Matrix.h"
#include "Random.h"
#include "Reference.h"
#include "Utilities.h"
#include "Vector.h"
#include "ce_DLight.h"
#include "g_playstats.h"

#define STAFF_LENGTH		27
#define STAFF_TYPE_SWORD	3
#define STAFF_TYPE_HELL		4

enum 
{
	STAFF_TRAIL,
	STAFF_HALO,
	STAFF_TRAIL2,
	STAFF_TRAIL_SMOKE,
	STAFF_TRAIL3,

	NUM_MODELS
};

static struct model_s* staff_models[NUM_MODELS];

//mxd. Models used by the ref-independent melee swing arc. Kept separate from the
// persistent staff trail models, even though the sprites are the same.
enum
{
	SLASH_PATBALL,
	SLASH_HALO,
	SLASH_FLAME,
	SLASH_HALOBLUE,

	SLASH_NUM_MODELS
};

static struct model_s* slash_models[SLASH_NUM_MODELS];

void PreCacheStaff(void)
{
	staff_models[0] = fxi.RegisterModel("sprites/spells/patball.sp2");
	staff_models[1] = fxi.RegisterModel("sprites/fx/halo.sp2");
	staff_models[2] = fxi.RegisterModel("sprites/spells/wflame2.sp2");
	staff_models[3] = fxi.RegisterModel("sprites/fx/steam.sp2");
	staff_models[4] = fxi.RegisterModel("sprites/fx/haloblue.sp2");
}

void PreCacheStaffSlash(void) //mxd
{
	slash_models[SLASH_PATBALL]  = fxi.RegisterModel("sprites/spells/patball.sp2");
	slash_models[SLASH_HALO]     = fxi.RegisterModel("sprites/fx/halo.sp2");
	slash_models[SLASH_FLAME]    = fxi.RegisterModel("sprites/spells/wflame2.sp2");
	slash_models[SLASH_HALOBLUE] = fxi.RegisterModel("sprites/fx/haloblue.sp2");
}

static qboolean StaffElementUpdate(client_entity_t* self, centity_t* owner) //mxd. Named 'FXStaffElementThink' in original logic.
{
	float frac = (float)(fx_time - self->startTime) / 100.0f;

	if (self->AnimSpeed > 0.0f)
		frac *= self->AnimSpeed;

	const int frame_num = (int)(floorf(frac));
	if (frame_num >= self->NoOfAnimFrames - 1)
		return false;

	self->r.frame = frame_num + 1;

	const float multiplier = 1.0f - frac / (float)(self->NoOfAnimFrames - 1);
	self->r.color.r = (byte)((float)self->color.r * multiplier);
	self->r.color.b = (byte)((float)self->color.g * multiplier);
	self->r.color.g = (byte)((float)self->color.b * multiplier);

	return true;
}

static qboolean StaffLevel3Update(client_entity_t* self, centity_t* owner) //mxd. Named 'FXStaffLevel2Think' in original logic.
{
#define TRAIL_LVL3_LIFETIME	2000 //mxd

	// If we've timed out, stop the effect (allow for fading).
	if (self->LifeTime > 0 && self->LifeTime < fx_time)
	{
		self->Update = RemoveSelfAI;
		self->updateTime = TRAIL_LVL3_LIFETIME; //BUGFIX: mxd. 'fxi.cl->time + 500' in original logic (makes no sense: updateTime is ADDED to fxi.cl->time in UpdateEffects()).

		return true;
	}

	// This tells if we are wasting our time, because the reference points are culled.
	if (!RefPointsValid(owner))
		return false; // Remove the effect in this case.

	// If this reference point hasn't changed since the last frame, return.
	vec3_t diff;
	const int ref_index = self->NoOfAnimFrames;
	VectorSubtract(owner->referenceInfo->references[ref_index].placement.origin, owner->referenceInfo->oldReferences[ref_index].placement.origin, diff);

	if (fabsf(diff[0] + diff[1] + diff[2]) < 0.1f)
		return true;

	const int num_of_intervals = GetScaledCount((int)(VectorLength(diff) * 0.5f), 1.0f);
	if (num_of_intervals > 40)
		return false;

	// Average out the two right hand positions to get a pivot point.
	vec3_t cur_pivot = VEC3_INIT(owner->referenceInfo->oldReferences[CORVUS_RIGHTHAND].placement.origin);

	vec3_t delta_pivot;
	VectorSubtract(owner->referenceInfo->references[CORVUS_RIGHTHAND].placement.origin, cur_pivot, delta_pivot);
	Vec3ScaleAssign(1.0f / (float)num_of_intervals, delta_pivot);

	vec3_t cur_normal = VEC3_INIT(owner->referenceInfo->oldReferences[ref_index].placement.direction);

	vec3_t delta_normal;
	VectorSubtract(owner->referenceInfo->references[ref_index].placement.direction, cur_normal, delta_normal);
	Vec3ScaleAssign(1.0f / (float)num_of_intervals, delta_normal);

	vec3_t dir;
	VectorNormalize2(cur_normal, dir);

	for (int i = 0; i < num_of_intervals; i++)
	{
		vec3_t trail_org;
		VectorMA(cur_pivot, STAFF_LENGTH, dir, trail_org);

		client_entity_t* trail = ClientEntity_new(FX_SPELLHANDS, (int)(self->flags & ~CEF_NO_DRAW), trail_org, NULL, TRAIL_LVL3_LIFETIME);

		trail->r.model = &staff_models[STAFF_TRAIL2];
		trail->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
		trail->r.scale = flrand(0.2f, 0.3f);
		trail->d_scale = flrand(-1.0f, -0.5f);
		trail->d_alpha = -2.0f;

		VectorSet(trail->velocity, flrand(-8.0f, 8.0f), flrand(-8.0f, 8.0f), flrand(64.0f, 128.0f)); //mxd. Original logic uses irand() here.

		for (int c = 0; c < 3; c++)
			trail->origin[c] += flrand(-1.0f, 1.0f); //mxd. Original logic uses irand() here.

		// Attach a dynamic light to the last one.
		if (i == num_of_intervals - 1 && R_DETAIL >= DETAIL_NORMAL)
		{
			const byte c = (byte)irand(8, 16);
			const paletteRGBA_t color = { .r = (byte)irand(236, 255), .g = 64 + c, .b = 16 + c, .a = (byte)irand(80, 192) };
			trail->dlight = CE_DLight_new(color, flrand(50.0f, 150.0f), -100.0f); //mxd. Original logic uses irand() here.
		}

		matrix3_t rotation;
		Matrix3FromAngles(owner->lerp_angles, rotation);

		vec3_t origin;
		Matrix3MultByVec3(rotation, trail->origin, origin);
		VectorAdd(owner->origin, origin, trail->r.origin);

		AddEffect(NULL, trail);

		// Create smoke?
		if (irand(0, 3) == 0)
		{
			client_entity_t* smoke = ClientEntity_new(FX_SPELLHANDS, (int)(self->flags & ~CEF_NO_DRAW), trail_org, NULL, 5000);

			smoke->r.model = &staff_models[STAFF_TRAIL_SMOKE];
			smoke->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
			smoke->r.scale = flrand(0.1f, 0.15f);
			smoke->d_scale = 1.0f;
			smoke->alpha = 0.75f;
			smoke->d_alpha = -1.0f;

			VectorSet(smoke->velocity, flrand(-16.0f, 16.0f), flrand(-16.0f, 16.0f), flrand(64.0f, 128.0f)); //mxd. Original logic uses irand() here.

			const int c = irand(32, 64);
			COLOUR_SETA(smoke->r.color, c, c, c, 128);

			// Attach a dynamic light to the last one.
			if (i == num_of_intervals - 1 && R_DETAIL >= DETAIL_NORMAL)
			{
				const byte c2 = (byte)irand(8, 16);
				const paletteRGBA_t color = { .r = (byte)irand(236, 255), .g = 64 + c2, .b = 16 + c2, .a = (byte)irand(80, 192) };
				smoke->dlight = CE_DLight_new(color, flrand(50.0f, 150.0f), -100.0f); //mxd. Original logic uses irand() here.
			}

			Matrix3FromAngles(owner->lerp_angles, rotation);
			Matrix3MultByVec3(rotation, smoke->origin, origin);
			VectorAdd(owner->origin, origin, smoke->r.origin);

			AddEffect(NULL, smoke);
		}

		Vec3AddAssign(delta_pivot, cur_pivot);
		Vec3AddAssign(delta_normal, cur_normal);
		VectorNormalize2(cur_normal, dir);
	}

	return true;
}

static qboolean StaffLevel2Update(client_entity_t* self, centity_t* owner) //mxd. Named 'FXStaffLevel3Think' in original logic.
{
#define TRAIL_LVL2_LIFETIME	500 //mxd

	// If we've timed out, stop the effect (allow for fading).
	if (self->LifeTime > 0 && self->LifeTime < fx_time)
	{
		self->Update = RemoveSelfAI;
		self->updateTime = TRAIL_LVL2_LIFETIME; //BUGFIX: mxd. 'fxi.cl->time + 500' in original logic (makes no sense: updateTime is ADDED to fxi.cl->time in UpdateEffects()).

		return true;
	}

	// This tells if we are wasting our time, because the reference points are culled.
	if (!RefPointsValid(owner))
		return false; // Remove the effect in this case.

	// If this reference point hasn't changed since the last frame, return.
	vec3_t diff;
	const int ref_index = self->NoOfAnimFrames;
	VectorSubtract(owner->referenceInfo->references[ref_index].placement.origin, owner->referenceInfo->oldReferences[ref_index].placement.origin, diff);

	if (fabsf(diff[0] + diff[1] + diff[2]) < 0.1f)
		return true;

	const int num_of_intervals = GetScaledCount((int)(VectorLength(diff) * 0.5f), 1.0f);
	if (num_of_intervals > 40)
		return false;

	// Take the before and after points and try to draw an arc.

	// Average out the two right hand positions to get a pivot point.
	vec3_t cur_pivot = VEC3_INIT(owner->referenceInfo->oldReferences[CORVUS_RIGHTHAND].placement.origin);

	vec3_t delta_pivot;
	VectorSubtract(owner->referenceInfo->references[CORVUS_RIGHTHAND].placement.origin, cur_pivot, delta_pivot);
	Vec3ScaleAssign(1.0f / (float)num_of_intervals, delta_pivot);

	vec3_t cur_normal = VEC3_INIT(owner->referenceInfo->oldReferences[ref_index].placement.direction);

	vec3_t delta_normal;
	VectorSubtract(owner->referenceInfo->references[ref_index].placement.direction, cur_normal, delta_normal);
	Vec3ScaleAssign(1.0f / (float)num_of_intervals, delta_normal);

	vec3_t dir;
	VectorNormalize2(cur_normal, dir);

	for (int i = 0; i < num_of_intervals; i++)
	{
		vec3_t trail_org;
		VectorMA(cur_pivot, STAFF_LENGTH, dir, trail_org);

		client_entity_t* trail = ClientEntity_new(FX_SPELLHANDS, (int)(self->flags & ~CEF_NO_DRAW), trail_org, NULL, TRAIL_LVL2_LIFETIME);

		trail->r.model = &staff_models[STAFF_TRAIL3];
		trail->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD);
		trail->r.scale = 0.3f;
		trail->d_scale = -0.75f;
		trail->alpha = 0.75f;
		trail->d_alpha = -4.0f;

		const int c = irand(128, 208);
		if (owner->current.effects & EF_BLOOD_ENABLED)
			COLOUR_SETA(trail->r.color, irand(236, 255), c, c, irand(80, 192)); //mxd. Use macro.
		else
			COLOUR_SETA(trail->r.color, c, c, irand(236, 255), irand(80, 192)); //mxd. Use macro.

		// Attach a dynamic light to the last one.
		if (i == num_of_intervals - 1 && R_DETAIL >= DETAIL_NORMAL)
			trail->dlight = CE_DLight_new(trail->r.color, 100.0f, -100.0f);

		trail->AddToView = OffsetLinkedEntityUpdatePlacement;

		AddEffect(owner, trail);

		Vec3AddAssign(delta_pivot, cur_pivot);
		Vec3AddAssign(delta_normal, cur_normal);
		VectorNormalize2(cur_normal, dir);
	}

	return true;
}

static qboolean StaffLevel1Update(client_entity_t* self, centity_t* owner) //mxd. Named 'FXStaffThink' in original logic.
{
#define TRAIL_LVL1_LIFETIME	1500 //mxd

	// If we've timed out, stop the effect (allow for fading).
	if (self->LifeTime > 0 && self->LifeTime < fx_time)
	{
		self->Update = RemoveSelfAI;
		self->updateTime = TRAIL_LVL1_LIFETIME; //BUGFIX: mxd. 'fxi.cl->time + 500' in original logic (makes no sense: updateTime is ADDED to fxi.cl->time in UpdateEffects()).

		return true;
	}

	// This tells if we are wasting our time, because the reference points are culled.
	if (!RefPointsValid(owner))
		return false; // Remove the effect in this case.

	// If this reference point hasn't changed since the last frame, return.
	vec3_t diff;
	const int ref_index = self->NoOfAnimFrames;
	VectorSubtract(owner->referenceInfo->references[ref_index].placement.origin, owner->referenceInfo->oldReferences[ref_index].placement.origin, diff);

	if (fabsf(diff[0] + diff[1] + diff[2]) < 0.1f)
		return true;

	const int num_of_intervals = GetScaledCount((int)(VectorLength(diff) * 0.75f), 1.0f);
	if (num_of_intervals > 40)
		return false;

	// Take the before and after points and try to draw an arc.

	// Average out the two right hand positions to get a pivot point.
	vec3_t cur_pivot = VEC3_INIT(owner->referenceInfo->oldReferences[CORVUS_RIGHTHAND].placement.origin);

	vec3_t delta_pivot;
	VectorSubtract(owner->referenceInfo->references[CORVUS_RIGHTHAND].placement.origin, cur_pivot, delta_pivot);
	Vec3ScaleAssign(1.0f / (float)num_of_intervals, delta_pivot);

	vec3_t cur_normal = VEC3_INIT(owner->referenceInfo->oldReferences[ref_index].placement.direction);

	vec3_t delta_normal;
	VectorSubtract(owner->referenceInfo->references[ref_index].placement.direction, cur_normal, delta_normal);
	Vec3ScaleAssign(1.0f / (float)num_of_intervals, delta_normal);

	vec3_t dir;
	VectorNormalize2(cur_normal, dir);

	for (int i = 0; i < num_of_intervals; i++)
	{
		// Get the position of this sprite.
		vec3_t trail_org;
		VectorMA(cur_pivot, STAFF_LENGTH, dir, trail_org);

		client_entity_t* trail = ClientEntity_new(FX_SPELLHANDS, (int)(self->flags & ~CEF_NO_DRAW), trail_org, NULL, TRAIL_LVL1_LIFETIME);

		trail->r.model = &staff_models[STAFF_TRAIL]; // patball sprite.
		trail->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
		trail->r.frame = 1;
		trail->d_scale = -0.5f;
		trail->alpha = 0.5f;
		trail->d_alpha = -1.0f;
		trail->r.scale = self->xscale;
		trail->r.color.c = ((owner->current.effects & EF_BLOOD_ENABLED) ? 0x50000018 : self->color.c);
		trail->AddToView = OffsetLinkedEntityUpdatePlacement;

		AddEffect(owner, trail);

		Vec3AddAssign(delta_pivot, cur_pivot);
		Vec3AddAssign(delta_normal, cur_normal);
		VectorNormalize2(cur_normal, dir);
	}

	return true;
}

void FXStaff(centity_t* owner, const int type, const int flags, vec3_t origin)
{
	byte powerlevel;
	char lifetime;
	fxi.GetEffect(owner, flags, clientEffectSpawners[FX_STAFF].formatString, &powerlevel, &lifetime);

	if (!ReferencesInitialized(owner))
		return;

	// Add a fiery trail effect to the player's staff.
	client_entity_t* trail = ClientEntity_new(type, CEF_OWNERS_ORIGIN | CEF_DONT_LINK, origin, NULL, 0); //mxd. next_think_time:17 in original logic.

	trail->flags |= CEF_NO_DRAW;
	trail->NoOfAnimFrames = CORVUS_BLADE;
	trail->SpawnData = powerlevel;
	trail->LifeTime = ((lifetime > 0) ? fx_time + lifetime * 100 : (int)lifetime);

	switch (powerlevel)
	{
		case 1: // Blue.
		default:
			trail->Update = StaffLevel1Update;
			trail->color.c = 0x02201010;
			trail->xscale = 0.175f;
			break;

		//NOTE: These were swapped after the functions were created.
		case 2: // Fire.
			trail->Update = StaffLevel2Update;
			trail->color = color_white;
			trail->xscale = 0.2f;
			break;

		case 3: // Energy blast.
		case 4:
			trail->Update = StaffLevel3Update;
			trail->color = color_white;
			trail->xscale = 0.225f;
			break;
	}

	AddEffect(owner, trail);
}

#pragma region ========================== Ref-independent staff swing arc ==========================

#define SLASH_LIFETIME			220	//mxd. ms.
#define SLASH_DLIGHT_LIFETIME	-120

// Per trail type: {start yaw offset, end yaw offset (degrees, relative to owner facing), arc radius}.
// Row order matches Player/p_anim_data.h trailtype_e (TRAIL_SPIN1 .. TRAIL_COUNTERRIGHT).
static const float slash_arc[8][3] =
{
	{ -85.0f,  85.0f, 44.0f }, // TRAIL_SPIN1
	{  85.0f, -85.0f, 44.0f }, // TRAIL_SPIN2
	{ -70.0f,  25.0f, 34.0f }, // TRAIL_STAND
	{ -55.0f,  40.0f, 34.0f }, // TRAIL_STEP
	{ 120.0f, 205.0f, 34.0f }, // TRAIL_BACK
	{ -16.0f,  16.0f, 22.0f }, // TRAIL_STAB
	{ -75.0f,   5.0f, 34.0f }, // TRAIL_COUNTERLEFT
	{  -5.0f,  75.0f, 34.0f }, // TRAIL_COUNTERRIGHT
};

//mxd. Draws a staff melee swing arc from an explicit world-space origin, so the attack reads visually
// even when the player model's per-frame staff/blade reference points are unavailable (which silently
// kills the reference-driven FXStaff trails).
void FXStaffSlash(centity_t* owner, const int type, const int flags, vec3_t origin)
{
	byte powerlevel;
	byte trail_type;
	fxi.GetEffect(owner, flags, clientEffectSpawners[FX_STAFF_SLASH].formatString, &powerlevel, &trail_type);

	if (trail_type >= 8)
		trail_type = 2; // TRAIL_STAND

	const float start_ang = slash_arc[trail_type][0] * ANGLE_TO_RAD;
	const float end_ang = slash_arc[trail_type][1] * ANGLE_TO_RAD;
	const float radius = slash_arc[trail_type][2];

	const float yaw = (owner != NULL ? owner->lerp_angles[YAW] * ANGLE_TO_RAD : 0.0f);
	const float yaw_cs = cosf(yaw);
	const float yaw_sn = sinf(yaw);

	// Power level determines the sprite model, tint and light brightness.
	int model_index;
	paletteRGBA_t color;
	float base_scale;
	float dlight_intensity;

	switch (powerlevel)
	{
		case 2: // Fire.
			model_index = SLASH_FLAME;
			color = (paletteRGBA_t){ .r = 0xff, .g = 0x90, .b = 0x28, .a = 0xff };
			base_scale = 0.34f;
			dlight_intensity = 110.0f;
			break;

		case 3: // Energy.
		case 4:
			model_index = SLASH_HALOBLUE;
			color = (paletteRGBA_t){ .r = 0xc0, .g = 0xd0, .b = 0xff, .a = 0xff };
			base_scale = 0.4f;
			dlight_intensity = 140.0f;
			break;

		default: // Blue.
		case 1:
			model_index = SLASH_PATBALL;
			color = (paletteRGBA_t){ .r = 0x50, .g = 0x90, .b = 0xff, .a = 0xff };
			base_scale = 0.28f;
			dlight_intensity = 80.0f;
			break;
	}

	const float sweep = fabsf(slash_arc[trail_type][1] - slash_arc[trail_type][0]);
	int num_sprites = 4 + (int)(sweep * 3.0f);
	num_sprites = (num_sprites < 4 ? 4 : (num_sprites > 12 ? 12 : num_sprites));

	// Lay the arc out in the horizontal plane, relative to the owner's facing.
	for (int i = 0; i < num_sprites; i++)
	{
		const float t = (num_sprites > 1 ? (float)i / (float)(num_sprites - 1) : 0.5f);
		const float ang = start_ang + (end_ang - start_ang) * t;

		const float dir_x = cosf(ang);
		const float dir_y = sinf(ang);
		const float wx = dir_x * yaw_cs - dir_y * yaw_sn;
		const float wy = dir_x * yaw_sn + dir_y * yaw_cs;

		vec3_t sprite_org;
		sprite_org[0] = origin[0] + wx * radius;
		sprite_org[1] = origin[1] + wy * radius;
		sprite_org[2] = origin[2] + (t - 0.5f) * 10.0f;

		client_entity_t* trail = ClientEntity_new(FX_SPELLHANDS, flags & ~CEF_NO_DRAW, sprite_org, NULL, SLASH_LIFETIME);

		trail->r.model = &slash_models[model_index];
		trail->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
		trail->r.scale = base_scale * flrand(0.85f, 1.2f);
		trail->r.color = color;
		trail->alpha = 0.85f;
		trail->d_scale = -base_scale * 0.5f;
		trail->d_alpha = -3.5f;

		AddEffect(NULL, trail);
	}

	// Bright flash at the arc centre.
	client_entity_t* flash = ClientEntity_new(FX_SPELLHANDS, flags & ~CEF_NO_DRAW, origin, NULL, SLASH_LIFETIME);

	flash->r.model = &slash_models[SLASH_HALO];
	flash->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
	flash->r.scale = base_scale * 1.6f;
	flash->r.color = color;
	flash->alpha = 0.9f;
	flash->d_scale = -base_scale;
	flash->d_alpha = -3.0f;

	AddEffect(NULL, flash);

	// A short dynamic light, so the swing also lights its surroundings.
	if (R_DETAIL >= DETAIL_NORMAL)
	{
		client_entity_t* dlight = ClientEntity_new(FX_SPELLHANDS, flags | CEF_NO_DRAW, origin, NULL, SLASH_LIFETIME);

		dlight->dlight = CE_DLight_new(color, dlight_intensity, SLASH_DLIGHT_LIFETIME);
		AddEffect(NULL, dlight);
	}
}

#pragma endregion

static qboolean StaffCreateOrRemoveUpdate(client_entity_t* self, centity_t* owner, const qboolean create) //mxd. Added to reduce code duplication.
{
	self->updateTime = MIN_UPDATE_TIME; //FIXME: with a next think time this effect does not look right.

	// This tells if we are wasting our time, because the reference points are culled.
	if (!RefPointsValid(owner))
		return false; // Remove the effect in this case.

	vec3_t start_pt;
	vec3_t end_pt;
	uint color;

	// If this reference point hasn't changed since the last frame, return.
	switch (self->refPoint)
	{
		case STAFF_TYPE_HELL:
			VectorAdd(owner->referenceInfo->references[CORVUS_RIGHTHAND].placement.origin, owner->referenceInfo->references[CORVUS_STAFF].placement.origin, start_pt);
			Vec3ScaleAssign(0.5f, start_pt);
			VectorCopy(owner->referenceInfo->references[CORVUS_HELL_HEAD].placement.origin, end_pt);
			color = 0xff2020ff;
			break;

		case STAFF_TYPE_SWORD:
		default:
			VectorCopy(owner->referenceInfo->references[CORVUS_STAFF].placement.origin, start_pt);
			VectorCopy(owner->referenceInfo->references[CORVUS_BLADE].placement.origin, end_pt);
			color = 0xff20ff20;
			break;
	}

	vec3_t diff;
	VectorSubtract(end_pt, start_pt, diff);

	const int num_of_intervals = (int)(VectorLength(diff) * 0.5f);
	if (num_of_intervals > 40)
		return false;

	Vec3ScaleAssign(1.0f / (float)num_of_intervals, diff);

	vec3_t trail_org = VEC3_INIT(start_pt); // This rides on the assumption that the normal given is already a unit norm. //TODO: the normal given is NOT a unit norm!
	const float trail_alpha = (create ? 0.8f : 0.6f); //mxd

	for (int i = 0; i < num_of_intervals; i++)
	{
		client_entity_t* trail = ClientEntity_new(FX_SPELLHANDS, (int)(self->flags & ~CEF_NO_DRAW), trail_org, NULL, 100);

		trail->r.model = &staff_models[self->classID];
		trail->alpha = trail_alpha - (float)self->NoOfAnimFrames * 0.1f;
		trail->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
		trail->AddToView = OffsetLinkedEntityUpdatePlacement;

		if (self->classID == STAFF_TRAIL || self->refPoint == STAFF_TYPE_HELL)
		{
			trail->r.frame = 1;
			trail->d_scale = -0.25f;
			trail->d_alpha = -0.1f;
			trail->color.c = color;
			trail->r.scale = (float)self->NoOfAnimFrames * 0.05f;
			trail->AnimSpeed = 0.2f;
			trail->NoOfAnimFrames = 2;
			trail->Update = StaffElementUpdate;

			AddEffect(owner, trail);
			StaffElementUpdate(trail, owner);
		}
		else if (self->classID == STAFF_TRAIL2)
		{
			trail->r.scale = flrand(0.1f, 0.2f);
			trail->d_scale = (create ? flrand(-0.5f, -0.25f) : flrand(-1.0f, -0.5f));
			trail->d_alpha = -2.0f;

			VectorSet(trail->velocity, flrand(-8.0f, 8.0f), flrand(-8.0f, 8.0f), flrand(64.0f, 128.0f)); //mxd. Original logic uses irand() here.

			for (int c = 0; c < 3; c++)
				trail->origin[c] += flrand(-1.0f, 1.0f); //mxd. Original logic uses irand() here.

			AddEffect(owner, trail);
		}
		else if (self->classID == STAFF_TRAIL3)
		{
			trail->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD);
			trail->r.scale = 0.2f;
			trail->d_scale = -0.35f;
			trail->alpha = 0.75f;
			trail->d_alpha = -4.0f;

			if (owner->current.effects & EF_BLOOD_ENABLED)
			{
				trail->r.color.c = 0x50000018;
			}
			else
			{
				const int c = irand(128, 208);
				COLOUR_SETA(trail->r.color, c, c, irand(236, 255), irand(80, 192)); //mxd. Use macro.
			}

			AddEffect(owner, trail);
		}

		Vec3AddAssign(diff, trail_org);
	}

	return true;
}

static qboolean StaffCreateUpdate(client_entity_t* self, centity_t* owner) //mxd. Named 'FXStaffCreateThink' in original logic.
{
	if (!StaffCreateOrRemoveUpdate(self, owner, true)) //mxd
		return false;

	self->NoOfAnimFrames--;
	return self->NoOfAnimFrames > 0;
}

void FXStaffCreate(centity_t* owner, const int type, const int flags, vec3_t origin)
{
	// This tells if we are wasting our time, because the reference points are culled.
	if (!RefPointsValid(owner))
		return; // Abandon the effect in this case.

	client_entity_t* staff_fx = ClientEntity_new(type, flags, origin, NULL, 17);

	if (flags & CEF_FLAG7) // Blue.
		staff_fx->classID = STAFF_TRAIL3;
	else if (flags & CEF_FLAG8) // Flames.
		staff_fx->classID = STAFF_TRAIL2;
	else // Normal.
		staff_fx->classID = STAFF_TRAIL;

	staff_fx->flags |= CEF_NO_DRAW;
	staff_fx->NoOfAnimFrames = 7;
	staff_fx->refPoint = (short)((flags & CEF_FLAG6) ? STAFF_TYPE_HELL : STAFF_TYPE_SWORD);
	staff_fx->Update = StaffCreateUpdate;

	AddEffect(owner, staff_fx);
}

//mxd. Added to reduce code duplication.
static void CreatePuff(centity_t* owner, const int flags, vec3_t origin)
{
	client_entity_t* puff = ClientEntity_new(FX_SPELLHANDS, (int)(flags & ~CEF_NO_DRAW), origin, NULL, 100);

	puff->r.model = &staff_models[STAFF_HALO];
	puff->r.flags = (RF_TRANSLUCENT | RF_TRANS_ADD | RF_TRANS_ADD_ALPHA);
	puff->r.frame = 1;
	puff->r.scale = 0.5f;
	puff->d_scale = -0.3f;
	puff->alpha = 0.75f;
	puff->d_alpha = -0.2f;
	puff->AnimSpeed = 0.2f;
	puff->NoOfAnimFrames = 2;

	puff->AddToView = OffsetLinkedEntityUpdatePlacement;
	puff->Update = StaffElementUpdate;

	AddEffect(owner, puff);
	StaffElementUpdate(puff, owner);
}

void FXStaffCreatePoof(centity_t* owner, int type, const int flags, vec3_t origin)
{
	// This tells if we are wasting our time, because the reference points are culled.
	if (!RefPointsValid(owner))
		return; // Remove the effect in this case.

	const int ref_id = ((flags & CEF_FLAG6) ? CORVUS_HELL_HEAD : CORVUS_BLADE); //mxd
	CreatePuff(owner, flags, owner->referenceInfo->references[ref_id].placement.origin); //mxd

	if (!(flags & CEF_FLAG6)) // Just for the sword staff.
		CreatePuff(owner, flags, owner->referenceInfo->references[CORVUS_STAFF].placement.origin); //mxd
}

static qboolean StaffRemoveUpdate(client_entity_t* self, centity_t* owner) //mxd. Named 'FXStaffRemoveThink' in original logic.
{
	if (!StaffCreateOrRemoveUpdate(self, owner, false)) //mxd
		return false;

	self->NoOfAnimFrames++;
	return self->NoOfAnimFrames < 6;
}

void FXStaffRemove(centity_t* owner, const int type, const int flags, vec3_t origin)
{
	if (!ReferencesInitialized(owner))
		return;

	client_entity_t* staff_fx = ClientEntity_new(type, flags, origin, NULL, 17);

	if (flags & CEF_FLAG7) // Blue.
		staff_fx->classID = STAFF_TRAIL3;
	else if (flags & CEF_FLAG8) // Flames.
		staff_fx->classID = STAFF_TRAIL2;
	else // Normal.
		staff_fx->classID = STAFF_TRAIL;

	staff_fx->flags |= CEF_NO_DRAW;
	staff_fx->NoOfAnimFrames = 1;
	staff_fx->refPoint = (short)((flags & CEF_FLAG6) ? STAFF_TYPE_HELL : STAFF_TYPE_SWORD);
	staff_fx->Update = StaffRemoveUpdate;

	AddEffect(owner, staff_fx);
}