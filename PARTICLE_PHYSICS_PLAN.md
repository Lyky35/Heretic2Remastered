# Particle Physics Plan — Beta (client effects)

Goal: explosion / weapon-impact particles should **bounce off walls and
floors** instead of passing through geometry. Exploration only — no code yet.

## 1. Current architecture (verified in-tree)

### Particle system — `src/client effects/Particle.c` (client-side, visual only)
- `client_particle_t`: `origin`, `velocity`, `acceleration`, `color`,
  `d_alpha`, `scale`, `d_scale`, `type`, `startTime`, `duration`.
- `ClientParticle_new()` — every particle gets `acceleration[2] = -PARTICLE_GRAVITY`
  (gravity 80). Created with a random velocity by the spawner.
- Position is **closed-form ballistic**: `origin + velocity*t + 0.5*accel*t²`
  (t = ms since birth). Stateless — no per-frame integration.
- `UpdateParticles()` (per-frame, `Client Entities.c:414`) — **only ages**
  particles (kills when `d_msec > duration` or alpha ≤ 0). No movement, no
  collision.
- `AddParticlesToView()` (per-frame, `Client Entities.c:271`) — computes each
  particle's position analytically and pushes it to the render lists. **No
  world interaction.** Particles pass straight through walls/floors.
- Particle `type` bits (`ParticleFlags.h`): move-mode (NORM / CYL_X/Y/Z /
  SPHERE) at bits 24-27; `PFL_ADDITIVE`, `PFL_NEARCULL`, etc. Bit 29
  (`PFL_SOFT_MASK`) is documented "unused in gl" — free for a `PFL_BOUNCE`.

### Effects — `client_entity_t` (client-side)
- Effects **can** clip the world: `clip_flags & CTF_CLIP_TO_ALL` →
  `Physics_MoveEnt()` (`Client Entities.c:418-433`). This moves/collides the
  effect's model, **not** the particles it owns.
- Particles ride at `ce->r.origin + particle_local_pos` (unless `CEF_ABSOLUTE_PARTS`).

### Weapon fire / explosions — server → client
- Server fires weapons (`src/game/g_weapon.c`), does its own traces, and spawns
  client effects via `gi.CreateEffect(...)` — e.g. `FX_SPARKS`,
  `FX_WEAPON_STAFF_STRIKE`, `FXExplosion1/2`.
- `fx_Explosion.c::CreateExplosionParticles()` — 256 particles, random
  velocity (192), gravity, 500 ms life. Purely ballistic, no collision.
- The **server** already handles projectile/weapon collision (it traces the
  shot and spawns the impact effect at `trace.endpos`). Particles are spawned
  at that point and fly ballistically.

### Client world trace — available
- Client has the world BSP loaded (`CM_LoadMap`, `cl_main.c:1005`).
- `CL_Trace(start, mins, maxs, end, brushmask, flags, &trace)` and
  `fxi.cls->Trace(...)` — used all over (camera, `cl_effects.c:178`). Returns
  `trace_t` (`endpos`, `fraction`, `plane.normal`, `allsolid`/`startsolid`,
  `contents`). This is the hook for per-particle collision.

## 2. Why particles look flat / pass through
- No collision anywhere in the particle path (client visual only).
- Ballistic + gravity only → particles fly through walls and sink through
  floors.

## 3. Approaches

### Option A — Per-frame client particle physics (RECOMMENDED)
Add a bounce path for particles that opt in (explosion debris / sparks).
- New flag `PFL_BOUNCE` (reuse bit 29, or add a bit).
- In `UpdateParticles()` (already per-frame, has `rframetime`), for bounce
  particles: integrate one frame (`new = origin + v*dt + 0.5*a*dt²`), trace
  `world_prev → world_new` via `CL_Trace` (point or tiny box). On hit:
  clamp origin to the surface, reflect velocity about `plane.normal` with a
  restitution (~0.45) and a little friction; cap bounce count. On miss:
  `origin = new`.
- `AddParticlesToView()` uses the integrated `origin` directly for bounce
  particles (skip the closed-form).
- Only explosion/debris particles opt in → bounded perf cost (a few hundred
  traces/frame worst case, only during explosions). Gate behind a cvar.

**Pros:** client-only (no server/network change — particles are visual);
reuses `CL_Trace`; real bounce; deterministic (no sync needed).
**Cons:** switches bounce particles from closed-form to per-frame Euler
integration; per-frame trace cost; restitution/friction tuning.

### Option B — Analytic ballistic bounce
Intersect the closed-form path with BSP planes analytically. Complex for a
general BSP (many planes), brittle. **Not recommended.**

### Option C — Post-hoc cull
Trace the closed-form path, kill the particle on hit. Simpler, but particles
just vanish at walls (no bounce). Only acceptable as a cheap first step.

### Server-side sim
Move particle physics to the server and sync positions. **Rejected** — large
architectural change + network traffic for a purely visual effect.

## 4. Open questions / decisions
1. **Which particles bounce?** Explosion debris + weapon sparks only, or all?
   (Recommend: explosion/spark debris via `PFL_BOUNCE`; smoke/dust/mist stay
   ballistic.)
2. **Restitution / friction** values (bounciness). Start ~0.45 restitution,
   ~0.8 tangent keep.
3. **Perf guard**: max bounce particles per frame + a cvar
   (`r_particle_bounce`, default on) to disable.
4. **Coordinate frames**: bounce uses `PFL_MOVE_NORM` particles only (explosion
   debris is NORM); spherical/cylindrical stay ballistic.
5. **Effect-origin motion**: explode at a fixed origin; if the effect moves,
   the world trace must use the current `ce->r.origin`.

## 5. Acceptance (Windows manual test)
- Fire a weapon that explodes into a wall/floor: particles bounce/roll along
  the surface instead of clipping through.
- `r_particle_bounce 0` → back to today's ballistic behavior (no regression).
- No crash when many explosions at once; `ref_gl1` unaffected (client effect).
- FPS: no measurable drop during a typical explosion.
