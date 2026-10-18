# Per-Surface PBR Plan — Beta (ref_gl3 world surfaces)

Goal: give world floors/walls/boxes per-surface material response
(stone vs wood vs metal vs gravel) with a cheap PBR-style specular,
without new assets. `ref_gl3` only; `ref_gl1` keeps today's path.
Plan only — no code committed for PBR yet.

Re-explored 2026-10-16 on Beta @ `9532e46a`. Prior note:
`PROGRESS.md:931-938` (2026-10-13). This file expands that verdict
into buildable phases.

---

## 1. What exists today (verified in-tree)

- **One usable material ID.** `flags >> 24` on `csurface`/`mtexinfo` =
  2-bit index into `{ gravel, metal, stone, wood }`
  (`src/H2Common/SurfaceProps.c:11-20`, header
  `src/qcommon/SurfaceProps.h:11`). Baked by the BSP compiler, present
  in the renderer's `mtexinfo_t.flags` (`src/ref_gl3/src/gl3_Model.h:49-56`,
  loaded `gl3_Model.c:304-346`). Today consumed only for footstep/
  landing sounds (`src/Player/p_animactor.c:548`,
  `src/Player/p_actions.c:731,1222`). Directly reusable as a PBR ID.
  Low bits are the Q2 surface flags (`src/game/q_shared.h:361-376`:
  `SURF_SKY 0x4`, `WARP 0x8`, `TRANS33/66`, `FLOWING`, `NODRAW`,
  `TALL_WALL 0x400`, `UNDULATE 0x2000`); bits 24+ are free of those,
  so `(flags >> 24) % 4` is safe.
- **`MaterialID_t` (12 mats) is NOT world-surface data.**
  `src/qcommon/FX.h:189-205` (`MAT_STONE … MAT_INSECT`) lives on
  entities (`g_Edict.h:364 materialtype`) for debris/blood/sounds
  (`g_combat.c`, `g_weapon.c:373`). Not usable for floors/walls.
- **World draw path.** Opaque world goes through the texture chains
  (`gl3_Surface.c:483-502` multitexture chain →
  `R_RenderLightmappedPoly :330-388` → `GL3_DrawLMPoly` per poly fan
  `gl3_Shaders.c:1201-1208`, streaming VBO, `VERTEXSIZE=7`
  pos3+tc2+lmtc2). Each poly is already a separate `glDrawArrays`
  call, so a per-poly `uMaterial` uniform is cheap and needs NO
  vertex-format change. Brush models reuse the same shader with
  `GL3_SetWorldSpace(0)` (`gl3_Surface.c:569`).
- **World shader today is NOT PBR.** `fragmentSource3DLM`
  (`gl3_Shaders.c:158-197`): `diffuse * lightmap + lightmap * spec`
  where spec is procedural luminance-derivative bump
  (`dFdx/dFdy(h) * uBumpScale * 24.0`, `pow(...,32.0)`, `:177-182`)
  with a fixed view-space light dir. No roughness/metalness/IBL
  uniforms anywhere. `r_bump 1 / r_bump_scale_world 0`
  (`gl3_Main.c:333-335,781-782`) means bump is effectively OFF by
  default — the base look is pure `diffuse * lightmap`.
- **No companion textures.** HD scanner (`gl3_Image.c:518-648`)
  loads one `.png` per texture (`R_LoadHDTexture :790-809`,
  `R_UploadHD :661-676`); no `_n/_orm/_r/_m` loading exists.
- **No IBL.** Only water mirror/refraction
  (`GL3_CopySceneToRefract`, `fragmentSourceWater`). BUT we own an
  analytic sky: Tier-A presets (`gl3_SkyDyn.c:271-336 SkyDyn_Preset`:
  top/bottom/sunDir/sunColor/moonDir/moonColor/night per 0/1/3/4/5)
  — samplable by reflect direction for a free ambient.
- HDR pipeline is already float (`fbo3D` RGBA16F), so adding a
  specular term cannot clip.

## 2. Phases

- **P0 — plumbing (no visual change).** Per-poly `uMaterial`
  (`(texinfo->flags >> 24) % 4`) + `uPbrStrength` uniforms on
  `shader3DLightmap`, `GL3_SetPbrMaterial()` called from
  `R_RenderLightmappedPoly`; cvars `r_pbr` (0 off default, archived),
  `r_pbr_debug` (0/1/2: off / tint-by-material / numeric overlay
  via flat color). Off path must be pixel-identical (uniforms set
  but unused when strength 0).
- **P1 — presets + analytic sky IBL (THIS IS THE BETA TARGET).**
  No assets. In `fragmentSource3DLM`:
  - Face normal from derivatives (faceted, correct for flat world):
    `N = normalize(cross(dFdx(vViewPos), dFdy(vViewPos)))`
    (view space; flip toward viewer).
  - Preset table indexed by `uMaterial`
    (gravel `{rough .95, metal .0}`, metal `{.32, .9}`,
    stone `{.85, .02}`, wood `{.7, .0}`) → `F0 = mix(0.04, albedo, metal)`.
  - Ambient = Tier-A gradient sampled by world reflect dir:
    `mix(uSkyBottom, uSkyTop, R.z*0.5+0.5)` (needs new uniforms
    fed from `SkyDyn_Preset` live values + world→view conversion
    already available via existing matrices; brush-model path
    reuses the same ambient, caustics logic untouched).
  - Sun spec = Schlick `F0 + (1-F0)*(1-cosTheta)^5`,
    power from roughness (`mix(8,128,1-rough)`), scaled by
    `uPbrStrength * preset strength`; ADDED to (never replacing)
    `diffuse*lm`. Night preset (`night=1`) dims sun to moon color.
  - Keep lightmaps/dlights/bump/caustics/fog byte-identical when
    `r_pbr 0`.
- **P2 — REVERTED (user did not like the result).** Tried three times, all
  reverted to P1:
  1. per-pixel roughness from albedo luminance + material ID (option 3) —
     too subtle (narrow albedo range).
  2. amplified roughness + metalness (rust) variation — still flat.
  3. per-pixel TBN bump from albedo height (`r_pbr_bump`) + broadened
     specular + sun-diffuse from the bumped normal — the tight dim P1
     specular gave the bump nothing to modulate; result not liked.
  P1 (presets + analytic-sky IBL + tight specular) is the current Beta
  target. Revisit only with real normal maps (P3) / authored height data.
- **P3 — out for Beta.** Real normal maps (`_n`), gl1 backport,
  VSM/PCF shadows interplay, brush-model per-entity materials.
  Explicitly deferred.

## 3. P1 uniforms + cvars (exact list for the implement step)

- Shader: `uMaterial (int)`, `uPbrStrength (float)`,
  `uSkyTop/uSkyBottom (vec3)`, `uSunDirV (vec3, view space)`,
  `uSunColor (vec3)`, `uNight (float)`.
- C: `GL3_SetPbrMaterial(int)`, `GL3_UpdatePbrSky(...)`,
  `GL3_UpdatePbrStrength(float)` in `gl3_Shaders.c/h`;
  `r_pbr`, `r_pbr_debug` in `gl3_Local.h` (extern) + `gl3_Main.c`
  (define + `Cvar_Get` archived, default 0).
- Sky feed: small getter in `gl3_SkyDyn.c`
  (e.g. `GL3_SkyDyn_PbrSky(top, bottom, sunDirV, sunColor, night)`)
  reusing `SkyDyn_Preset` + current level auto resolution;
  called per-frame before world draw (precedent: caustic update).

## 4. Risks / limits

- Faceted normals only (no smooth vertex normals in format) —
  fine for axis-aligned H2 architecture, wrong for curved
  `UNDULATE` rock (keep strength low there or mask by flag).
- 4 materials max without recompiling maps (compiler-baked byte);
  mapper discipline unknown — `r_pbr_debug 1` exists to audit.
- Lightmap is already irradiance; P1 only ADDS spec/ambient,
  never re-lights diffuse (no double-darkening).
- Per-poly uniforms ≈ +1 `glUniform` per world poly; negligible
  vs existing per-poly `glBufferData` streaming.
- `r_pbr` archived: fresh installs get default 0 (safe);
  testers must set `r_pbr 1` once.
- Paletted `.m8` albedo banding under spec — HD PNG path
  mitigates; not a blocker for P1.

## 5. Acceptance (Windows manual test)

- `r_pbr 0`: pixel-identical to today (screenshot diff).
- `r_pbr 1` day: metal trims/rails spec pop, wood/stone stay
  matte; `r_pbr_debug 1` tints 4 mats distinctly on some map.
- Preset 3 (night): spec dim/cool, no orange glare on moon side.
- No crash on maps with sparse materials; `ref_gl1` unaffected.
- FPS: no measurable drop at native res (uniform-only change).

## 6. Files the implement step will touch (P0+P1 only)

`gl3_Shaders.c/h` (LM frag + uniforms + setters),
`gl3_Surface.c` (per-poly material set),
`gl3_SkyDyn.c/h` (sky getter),
`gl3_Local.h` + `gl3_Main.c` (cvars),
`PROGRESS.md` (log). No `gl3_Image.c`, no `gl1`, no savegame,
no BSP rebuild.
