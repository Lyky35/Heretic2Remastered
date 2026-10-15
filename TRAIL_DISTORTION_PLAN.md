# Staff Trail Distortion Plan — Max-Payne-style refraction over a ribbon (Beta)

Goal: replace the current staff-melee sprite-puff chain with a ribbon trail
that refracts the scene behind it (heat-haze / Max Payne bullet-trail look),
plus an optional faint hot core. `ref_gl3` only; `ref_gl1` keeps sprites.

---

## 1. What exists today (Beta @ `12729ebe`, verified in-tree)

- **No ribbon, no distortion.** `src/client effects/fx_staff.c` `StaffLevel{1,2,3}Update`
  (`:63-343`) emits billboarded `.sp2` sprites (`patball`/`haloblue`/...) along
  the pivot→tip segment each frame (`GetScaledCount(len*0.5|0.75)`, clamp 40),
  lifetimes 500/1500/2000 ms, additive blending (`RF_TRANS_ADD[_ALPHA]`).
- **Blade sampling is already fixed.** Tip/pivot come from baked
  `refsForFrame` (`CORVUS_BLADE=5`, `CORVUS_RIGHTHAND=1`), `r_references` off
  by default; `c517ae12` fixed the double-`FrameLerp` zero-diff. `FX_STAFF_SLASH`
  arc history was removed again in `94f1437f` — nothing to reuse verbatim, but
  the sampling points (`Utilities.c` `RefPointsValid`, `OffsetLinkedEntityUpdatePlacement`)
  are exactly what a ribbon builder needs.
- **Render path:** sprites → `r_alpha_entities` → `R_DrawSpriteModel`
  (`gl3_Sprite.c`) → `GL3_Draw3DPoly(TRIANGLE_FAN)` (9-float `pos3+tc2+col4`,
  `shader3D`). No heat/distort/grab code anywhere (only water refraction +
  OpenAL distortion).
- **The key reuse:** water ALREADY does GPU-Gems-style refraction in production:
  `GL3_CopySceneToRefract()` blits `fbo3D` → full-res `fboRefract` (`gl3_Shaders.c:1118`),
  `fragmentSourceWater` samples `uRefractTex` with normal-distorted projective UV
  (`:438-448`). HDR `fbo3D` is RGBA16F; world → entities → alpha → particles all
  render into it before `GL3_CompositeHDR` (`gl3_Main.c:1445-1502`).

## 2. The technique (Max Payne family)

- Max Payne 1: physical slowed bullets + distortion trails in bullet-time;
  Max Payne 2: screen-space stretched billboards skewed along screen velocity.
  F.E.A.R. slo-mo and the modern standard: **screen-space refraction** —
  render scene to texture S (minus the refractive object), then draw trail
  geometry sampling S at `screenUV + normal.xy * strength`, optionally with
  chromatic 3-tap offsets (R/G/B increasingly refracted) and an alpha mask
  (GPU Gems 2, Ch. 19 — the exact algorithm, incl. the alpha-channel leakage
  guard). NeoFPS `DistortionTrail`, Simon Trümpler's one-shader (trail tex +
  normal map + alpha erosion over lifetime) are the same family.
- For a melee swing (fast arc, ~200 ms), the analog of MP2's velocity-skewed
  billboard is a **camera-facing ribbon along the blade's recent arc** with
  distortion strongest mid-life, eroding at tip/tail.

## 3. Design for this engine

**Geometry — ribbon in Client Effects (`fx_staff.c` + `Utilities.c`):**
- Per swing, keep a ring buffer (last ~10-14 frames) of blade-tip (and base?)
  world positions, sampled in the existing `StaffLevel*Update` path.
- Each frame, build a camera-facing quad strip: for consecutive samples,
  side vector = normalize(cross(segmentDir, viewDir)) * width(age); emit
  9-float verts reusing the `pos3+tc2+col4` layout — `tc=(u along arc, v across)`,
  `col=(intensity, seed, edgeFade, 0)`. Drawn via a new `GL3_DrawRibbonPoly()`
  (same streaming-VBO pattern as `GL3_Draw3DPoly`, new `shaderTrailDistort`).
- Branch by renderer: `vid_ref` == `"gl3"` → ribbon; otherwise today's sprite
  puffs (untouched fallback, also the `r_trail_distort 0` path).

**Background capture — dedicated blit, not the water texture:**
- `fboRefract` holds scene-minus-*water*, captured pre-water — wrong background
  wherever water is behind the trail. Instead add `GL3_CopySceneForTrail()`
  (same 10-line blit into a new full-res `fboTrail` RGBA16F) called at the end
  of `R_RenderView`, immediately before the ribbon draw. One blit/frame only
  while a ribbon is alive; zero ordering headaches (true scene-minus-trail).
- Alternative (cheaper, worse): reuse `fboRefract`. Rejected for the water-
  behind-trail artifact; revisit only if the blit ever profiles hot (it won't).

**Shader — `shaderTrailDistort` (GLSL 330, like the rest):**
- Vertex: standard MVP + pass world pos, clip pos, ribbon UV, intensity.
- Fragment: `screenUV = clip.xy/clip.w*0.5+0.5`; fetch tiling distortion normal
  `N` (new small procedural noise texture generated once on CPU at init —
  no compute, no assets); `offset = (N.xy-0.5) * uStrength * edgeFade(v) *
  ageFade(u)`; `refracted = texture(uTrailBg, screenUV + offset)` (+ optional
  2 extra taps at 1.0x/1.5x offset for chromatic fringing behind
  `r_trail_chroma`); faint hot core `uCoreColor * centerBump * ageFade` added;
  alpha = mask. Blend `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, depth test on
  (LEQUAL, far-plane-ish ribbon depth comes free from real geometry),
  depth writes off. New cvars: `r_trail_distort` (master, default 1 on gl3),
  `r_trail_strength`, `r_trail_chroma`, `r_trail_core`.
- Leakage (foreground geometry refracted wrongly, GPU Gems Fig. 19-3):
  mitigated by small default strength + edge/age fade to zero; full alpha-mask
  guard only if testers report artifacts.

**Fallbacks:** `ref_gl1` (no projective UV) and `r_trail_distort 0` keep today's
sprites bit-identical. No new libs, no save-format touch, FX-only (no gameplay).

## 4. Phases

- **T1 — Ribbon mesh:** ring buffer + strip builder + `GL3_DrawRibbonPoly` +
  plain additive UV-gradient material (no refraction yet). Proves geometry.
- **T2 — Capture + distort:** `fboTrail` blit, noise texture gen, distortion
  fragment (1 tap), cvars. The Max Payne look lands here.
- **T3 — Polish:** chromatic taps, hot core, per-powerlevel tint (blue/fire/
  energy reuse today's `STAFF_TRAIL*` colors as core tint), age erosion tuning,
  reflection-pass behavior (skip ribbon in mirror? cheap enough to keep — decide
  in testing), perf check (`r_speeds`).

## 5. Files to touch (all on `R8.7_AdvShaders`, NOT Beta)

- ADD: ribbon builder (likely new `fx_trailribbon.c` in Client Effects or inside
  `fx_staff.c`), `gl3_Trail.h/.c` (FBO, noise texture, draw call) + vcxproj/CMake
  entries (CMake parses vcxproj — one edit).
- EDIT: `fx_staff.c` (history sampling hookup), `gl3_Shaders.c/h` (new program +
  uniforms), `gl3_Surface.h` maybe (pass flag if needed), `gl3_Main.c` (capture
  call order + cvars), `gl3_Sprite.c` or new draw path for `RF_DISTORT` entities.
- DOCS: this file + `PROGRESS.md` per-portion log (existing workflow).

## 6. Risks / open questions

1. Ribbon orientation at swing extremes (blade pointing at camera → degenerate
   strip): clamp width + fall back to a sprite puff for near-degenerate segments.
2. Trail-behind-trail overlap: single distortion layer compositing twice = double
   warp; acceptable (same as water-on-water today).
3. MSVC + mingw `gnu17`, Windows-only — no new deps, strings embedded like water.
4. Taste risk: distortion-only can read as "smudge" without the hot core — T3's
   core tint exists specifically so each staff level keeps its identity.
5. `vid_ref` branch in shared FX code must exactly preserve gl1 behavior.

## 7. Acceptance

- [ ] `r_trail_distort 0` / `ref_gl1` == today's sprite trail, pixel-equivalent.
- [ ] `r_trail_distort 1` on gl3: arc ribbon warps background (visible on skybox
  edges, walls, water), fades head-to-tail, no leakage complaints at default
  strength, negligible FPS delta on 7900XTX-class GPU.
- [ ] R8.7 Beta release notes entry + test checklist when it ships to testers.
