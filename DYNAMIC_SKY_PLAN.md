# Dynamic Sky + Clouds Plan — Heretic2Remastered (`Beta`) from `fede-vaccaro/TerrainEngine-OpenGL`

Source: https://github.com/fede-vaccaro/TerrainEngine-OpenGL (`master`, OpenGL 4 + C++, ~922 stars).
Target: `Lyky35/Heretic2Remastered` branch `Beta` — Quake2-engine, `ref_gl3` (OpenGL 4.6 context, GLSL 330, SDL3+GLAD) + `ref_gl1` (fixed-function 1.3).

Goal: port **dynamic sky with clouds** — gradient sky + sun + volumetric raymarched clouds — into H2 without breaking BSP/PVS sky, fog, reflections, Silverpring aurora/stars, Darkmire particles, or `ref_gl1`.

---

## 1. What the source project does

### 1.1 Sky background (`DrawableObjects/Skybox.{h,cpp}`, `shaders/sky.frag`, `skyboxVert.vert`)
- Screen-space fullscreen pass into `skyboxFBO` (full-res). Reconstructs world ray from `inv_proj * inv_view`, intersects proxy sphere `r=0.5`, mixes `skyColorBottom -> skyColorTop` by view-Y (`mix(..., clamp(1-exp(8.5-17*...)))`), adds sun disc `0.8*vec3(1,.6,.1)*pow(dot(SUN_DIR,d),350)`.
- `colorPreset` system: `DefaultPreset()` (day: top `0.5,0.7,0.8*1.05`, bottom `0.9,0.9,0.95`) vs `SunsetPreset1()` (top `133,158,214`, bottom `241,161,161`); `update()` lerps by `sigmoid(lightDir.y)` — i.e. **sun-height-driven day/sunset blend**, also drives `lightColor`/`fogColor`.
- Sky texture is then sampled by the cloud pass as background (`uniform sampler2D sky`).

### 1.2 Volumetric clouds (`VolumetricClouds.{h,cpp}`, `CloudsModel.{h,cpp}`, `shaders/volumetric_clouds.comp` 571 lines)
- Compute shader `layout(16,16,1)`, outputs 4 `rgba32f` images: `fragColor`, `bloom`, `alphaness`, `cloudDistance` (`TextureSet(SW,SH,4)`).
- Dome model: `earthRadius=600000`, `sphereInnerRadius=5000` (cloud base), `sphereOuterRadius=17000` (top); `sphereCenter=(0,-EARTH_RADIUS,0)` with `xz` snapped to camera. Three camera cases: below / inside / above layer (`raySphereintersection*`).
- Main march `raymarchToCloud`: 64 steps, Bayer 4x4 jitter, early-out `T<=0.1`. Light march `raymarchToLight`: 6 taps cone with `noiseKernel[6]`, Beer `exp(-d)`, Henyey-Greenstein dual-lobe `mix(HG(-0.08),HG(0.08))`, powder option.
- Density `sampleCloudDensity(p)`: `perlinWorley 128^3` base (`lowFreqFBM = .625/.25/.125` of `.gba`, remap by `1-lowFreq`), height-gradient by cloud type (`STRATUS / STRATOCUMULUS / CUMULUS` vec4s, Siggraph Nubis-Decima style), `weatherTex.r * coverage` gate, optional `worley32` erosion scaled by `curliness`, `crispiness` (=`CLOUD_SCALE`) UV tiling.
- Uniforms: `iResolution/iTime/inv_proj/inv_view/cameraPosition/FOV/lightDirection/lightColor/coverage/cloudSpeed/crispiness/curliness/absorption/densityFactor/earthRadius/inner/outer/cloudColorTop/Bottom/skyColorTop/Bottom/depthMap/sky`.
- Defaults: `coverage .45, speed 450, crispiness 40, curliness .1, density .02, absorption .35`.
- Fog: `computeFogAmount(dist, .00006)` blends clouds to `bg`; early-out if `>0.965`. Sun glare `pow(sun,256)*v.a` added.
- Post (`clouds_post.frag`, `ScreenSpaceShader`): 3x3 Gaussian blur + optional 64-sample radial god-rays if `lightDotCameraFront>0` and `enableGodRays`. Toggle `postProcess` (default on).

### 1.3 Noise generation (one-time / on-seed-change compute)
- `perlinworley.comp` (315 lines) -> `RGBA8 128^3` `perlinTex` + `glGenerateMipmap`.
- `worley.comp` (289 lines) -> `RGBA8 32^3` `worley32` + mipmaps.
- `weather.comp` (91 lines, Perlin 4-octave) -> `RGBA8 1024^2` `weatherTex` (`r=coverage, g=cloudType`), regenerated when `scene->seed` or `perlinFrequency (.8)` changes. Dispatch `1024/8` groups.

### 1.4 Integration in source engine
Order per frame: `Skybox::draw()` -> `VolumetricClouds::draw()` (compute at `SCR_WIDTH/HEIGHT`, then optional post at full-res) -> composite with `depthMap` + `sky`. GUI (ImGui) exposes sky colors, coverage/speed/crispiness/curliness/density/absorption, dome radii, frequency, god-rays toggle.

Cost note: full-res 64-step march is heavy; source renders clouds at reduced `SW,SH` then upsamples in post.

---

## 2. What Heretic2Remastered has today (`ref_gl3` unless noted)

- **Sky**: classic Q2 6-face skybox `src/ref_gl3/src/gl3_Sky.c:1080 R_DrawSkyBox()` (mirror `src/ref_gl1/src/gl1_Sky.c:767`). `SURF_SKY` surfaces only set clip bounds (`gl3_Surface.c:752`); after opaque world, 6 quads with `pics/skies/<name>{rt,bk,lf,ft,up,dn}.m8` via `RI_SetSky(name,rotate,axis)` (`gl3_Sky.c:1237`, from `cl_view.c:112 CS_SKY`). `glDepthMask(FALSE)`, `R_GetSkyClipDist()` for far plane. No GLSL sky — fixed quads through `GL3_Draw3DPoly` streaming VBO.
- **Dead/extra sky code**: `R_DrawSkyClouds()` (sin alpha grid, never called), `R_DrawSkyStars()` + `R_DrawSkyAurora()` (only Silverpring maps `ssdocks/sswarehouse/sstown/sspalace`), swamp leaves/dust/mist (only `dmireswamp`), all CPU immediate quads/points. No day/night, no volumetrics.
- **Pipeline**: `ref_gl3` = SDL3 GL 4.6 Core context (`gl3_SDL.c:100`), GLAD 4.6 (`gl3_Glad.c`), but **all shaders `#version 330 core` embedded C strings** (`gl3_Shaders.c:21+`, `CreateProgram()` only vert+frag, 52x `glGetUniformLocation`). Streaming draw: per-poly `glBufferData(STREAM)+glDrawArrays`, 4 VAOs, no UBO/SSBO. Samplers hardcoded (diffuse 0, lightmap 1, reflect 1, refract 2, depth 3), `MAX_TEXTURE_UNITS 2` 2D-only cache (`gl3_Image.c:101`).
- **FBOs**: `fbo3D RGBA16F + DEPTH24_STENCIL8`, half-res reflect/refract, bloom ping-pong, SSAO `R16F` — all in `gl3_Shaders.c:1102+`. Depth texture reused by SSAO/post-fog (TMU3). No `TEXTURE_3D`, no `glDispatchCompute/BindImageTexture` (0 hits in `ref_gl3/src`).
- **Build**: Windows-only MSVC `src/Heretic2R.sln` + mingw-cross `CMakeLists.txt` parsing `.vcxproj`; no shader build step (edit C string + rebuild `ref_gl3.dll`).
- **`ref_gl1`**: GL 1.3 fixed-function, `glBegin` everywhere — **cannot run compute/volumetrics**. Must be left untouched with cvar-off fallback.

Key implication: **context is ready (4.6), shader/infra is not (330, no compute/3D/image paths)**. Port = add parallel compute path, not rewrite of existing sky.

---

## 3. Port strategy (keep old sky as fallback)

- **Scope to `ref_gl3` only.** `ref_gl1` keeps 6-face sky; new cvars default off so `main` behavior unchanged.
- **Two-tier feature**:
  - **Tier A (cheap, always safe): gradient + sun sky** — port of `sky.frag` as either fullscreen triangle or 6-quad replacement. No compute, no 3D textures. Gives "dynamic sky" immediately, blends with existing fog/presets.
  - **Tier B (full): volumetric clouds compute** — port of `volumetric_clouds.comp` + noise gens + half-res composite + blur. Gated by `r_dynsky` / `r_volclouds`.
- **Preserve**: BSP `SURF_SKY` culling data, `SetSky` map skies (use as Tier-A tint or Tier-B `coverage` modulator), Silverpring stars/aurora (draw after clouds), Darkmire particles, water reflection (`GL3_CopySceneToRefract`, `fboReflect`) — skip cloud compute in reflection pass, reuse main-pass result.
- **Coordinate choice**: reuse H2 world units, but rescale dome: H2 maps are ~4-8k units, not km. Start `earthRadius 600000 -> keep`, `inner 5000 -> ~1500`, `outer 17000 -> ~4500`, tune per map. Camera `r_origin`, view `r_world_matrix`, proj `r_projection_matrix` (`gl3_Local.h:349`).

---

## 4. Step-by-step implementation plan

### Phase 0 — Scaffolding + cvars (no visual change)
1. New files `src/ref_gl3/src/gl3_SkyDyn.{h,c}` (+ add to `ref_gl3.vcxproj` + `CMakeLists.txt` glob so both MSVC+mingw pick it up).
2. Cvars: `r_dynsky (0/1)`, `r_volclouds (0/1)`, `r_cloud_res (0.5 scale)`, `r_cloud_coverage`, `r_cloud_speed`, `r_cloud_density`, `r_cloud_crisp`, `r_cloud_curl`, `r_cloud_absorb`, `r_cloud_sunmix`, `r_sky_preset (0 day /1 sunset /2 auto-by-sun-height)`. Register in `gl3_Main.c`, archive.
3. `GL3_SkyDyn_Init()` called from `GL3_InitShaders()`; `GL3_SkyDyn_Shutdown()` on `GL3_Shutdown()`. All init failures -> print + auto-disable Tier B, fall back to old sky (never crash on old drivers).
4. Test: build `ref_gl3.dll`, `r_dynsky 0` == current behavior pixel-identical.

### Phase 1 — Tier A: gradient sky + sun (fragment only, `#version 430` or keep 330)
1. Port `sky.frag` (`mix(bottom,top,1-exp(...))` + `getSun(d,350)`) as new embedded `fragmentSourceSkyDyn` + reuse fullscreen-triangle VAO (`vaoFSQ` in `gl3_Shaders.c:626`). Uniforms: `inv_proj/inv_view` (inverse of `r_projection_matrix`/`r_world_matrix`), `lightDirection` (from `currentlight`/sun vector), `skyColorTop/Bottom`, `resolution`.
2. Presets: port `DefaultPreset/SunsetPreset1` + `sigmoid(lightDir.y)` auto-mix; expose `r_sky_preset`. When `r_dynsky=1` and `r_volclouds=0`, `R_DrawSkyBox()` draws Tier-A fullscreen (depth-write off, depth-test off) instead of 6 quads, but still calls `R_Clear/AddSkySurface` for PVS stats. Keep `sky_images` path when `r_dynsky=0`.
3. Fog coupling: multiply Tier-A output by existing post-fog uniforms (`uFogNearFar`, `r_fog`) — do not double-fog; verify against `gl3_Shaders.c:1077` post shader.
4. Test: `map ssdocks` + `sswarehouse`, toggle `r_dynsky`, `r_sky_preset 0/1/2`, check sun disc follows `lightDirection`, stars/aurora still draw on top.

### Phase 2 — Compute + 3D-texture infrastructure
1. Add `CreateComputeProgram(const char* comp_src)` in `gl3_Shaders.c` (compile `GL_COMPUTE_SHADER`, link, log like `CompileShader`). Bump new shaders to `#version 430 core` (context is 4.6, safe). Keep old 330 shaders untouched.
2. Extend texture-unit cache beyond 2 for compute (`GL_TEXTURE_3D`, image units 0-3): add `GL3_BindImageTexture(unit, tex, access, format)` wrapper + `glMemoryBarrier` helper; do not disturb `R_Bind/R_SelectTexture` 2D path.
3. Add helpers `GL3_MakeTexture3D(w,h,d,internalFormat)` (`RGBA8` for noise, `RGBA16F` for cloud FBO if needed) + `GL3_MakeWeatherTexture(1024²)`; check `GLAD_GL_VERSION_4_3` at runtime before use.
4. Test: init-only, no draw yet; verify `glGetError` clean on target GPU + fallback message on simulated failure.

### Phase 3 — Noise generation port (one-time cost)
1. Embed `perlinworley.comp` (128³), `worley.comp` (32³), `weather.comp` (1024²) as C strings (strip `seed` ImGui deps; wire `seed` to fixed `vec3(0)` + `r_cloud_seed` cvar, `perlinFrequency` to `r_cloud_crisp`-adjacent cvar).
2. `GL3_SkyDyn_GenNoise()`: `glBindImageTexture + glDispatchCompute(128/4, 32/4, 1024/8) + glMemoryBarrier + glGenerateMipmap(GL_TEXTURE_3D)`. Run once at init + on `r_cloud_seed`/frequency change (not per frame). Time it (`Con_Printf` ms like source `TIMETO`).
3. Validate by dumping a slice to a debug 2D view (`r_cloud_debug 1` draws noise slice fullscreen) before raymarching.

### Phase 4 — Volumetric raymarch port (the heavy lift)
1. Embed `volumetric_clouds.comp` adapted:
   - Inputs: `iResolution` (= cloud buffer size, e.g. `vid.width* r_cloud_res`), `iTime` (`r_newrefdef.time`), `inv_proj/inv_view` (H2 matrices), `cameraPosition` (`r_origin`), `lightDirection/lightColor` (H2 sun), `sky` (Tier-A FBO tex), `depthMap` (`fbo3D` depth — linearize with H2 near/far), `weatherTex/perlinTex/worley32`.
   - Outputs: full-res or half-res `fragColor/bloom/alphaness/cloudDistance` images (`RGBA16F` to match `fbo3D` HDR).
   - Scale fixes: replace `SPHERE_*` defaults with H2-tuned (`inner ~1500, outer ~4500`), `CLOUD_SPEED` from `r_cloud_speed` (source 450 is km-scale — retune to ~5-20 for H2 units), `densityFactor .02`, 64 steps -> start 32 steps at half-res for perf, Bayer jitter keep.
   - Depth handling: sample H2 depth, early-out where opaque (`if opaque, fragColor=bg`); reconstruct world pos with `invViewProj` already in shader.
2. New `GL3_DrawSkyCloudsCompute()`: bind images + samplers (units 0-4), `glDispatchCompute(W/16,H/16,1)`, `glMemoryBarrier(SHADER_IMAGE_ACCESS)`. Called from `R_DrawSkyBox()` tail when `r_volclouds=1` (after Tier-A sky, before stars/aurora/particles), with `GL3_UpdateModelview3D(r_world_matrix)` restore.
3. Reflection pass: skip compute when `R_SetReflectionPass` active; reuse last cloud frame (or plain Tier-A) to halve cost.

### Phase 5 — Composite + post
1. Half-res -> full-res upsample: reuse `vaoFSQ` + new `fragmentSourceCloudComposite` (bilateral-ish using `cloudDistance` + depth to avoid halo), then existing bloom chain (`shaderBloomExtract/Blur`) picks up `bloom` attachment if `r_bloom` on.
2. Port `clouds_post.frag` Gaussian 3x3 as optional `r_cloud_blur 0/1`; god-rays (`enableGodRays`) default **off** (needs `lightPos` screen projection + 64 taps — port only if Tier B stable, gate `r_cloud_godrays`).
3. Order check: `opaque -> Tier-A sky -> cloud compute -> composite -> stars/aurora/particles -> water/refract -> SSAO/bloom/tonemap/fog-post`. Ensure `glDepthMask(FALSE)` around sky/cloud, restore after.

### Phase 6 — Tuning, UI, maps
1. Console-only first (no menu): document `r_*` in `README`/cvar list. Optional later: Options menu slider for `r_cloud_coverage/speed`.
2. Per-map defaults: `CS_SKY` name -> coverage preset (e.g. `dmireswamp` higher coverage + fog coupling, Silverpring lower + keep aurora). Keep `RI_SetSky` working — map sky textures become fallback when Tier B off.
3. Time-of-day hook (future): `r_sky_preset 2` auto mode already lerps by sun height; expose `r_sun_pitch` override for testing sunsets without touching gameplay lights.

### Phase 7 — Perf + compat + cleanup
1. Perf: default `r_cloud_res 0.5` (half-res), 32 march steps, 6 light steps; measure `glFinish`-timed ms, `r_stats` line. If >4ms at 1080p, drop to 0.33 res or 24 steps.
2. Compat: if `GLAD_GL_VERSION_4_3==0` or compile fails -> `r_volclouds` forced 0 + warning; `ref_gl1` untouched. No new DLL deps (GLAD 4.6 already in tree).
3. Cleanup: remove `R_DrawSkyClouds` dead code or route behind `r_dynsky=0` legacy path; add `VULKAN_PORT_PLAN.md` note (compute maps to Vulkan compute later).

---

## 5. Files to touch (Beta branch)

- ADD: `src/ref_gl3/src/gl3_SkyDyn.h`, `gl3_SkyDyn.c` (all new sky/cloud code, ~800-1200 lines C + embedded GLSL).
- EDIT: `src/ref_gl3/src/gl3_Shaders.{h,c}` (compute program helper, FSQ composite shader, FBO for clouds), `gl3_Sky.{h,c}` (hook `R_DrawSkyBox`), `gl3_Main.c` (cvar reg + init/shutdown calls), `gl3_Local.h` (state struct), `src/ref_gl3/ref_gl3.vcxproj` + `CMakeLists.txt` (new file entries), `VULKAN_PORT_PLAN.md` (compat note).
- ADD (docs): this `DYNAMIC_SKY_PLAN.md` + `PROGRESS.md` log entries.
- DO NOT TOUCH: `src/ref_gl1/*` (fallback renderer), gameplay `src/game/*`, save format.

---

## 6. Risks / open questions

1. **Scale mismatch** (biggest): source dome is km-scale (600km earth, 5-17km clouds); H2 maps are units-scale with `r_farclipdist` fog. Wrong radii = no clouds or banding. Mitigation: Phase 4 retune + `r_cloud_debug` slice view.
2. **Depth format**: H2 `DEPTH24_STENCIL8` vs source float depth; linearization must use H2 `projParams` or clouds will clip through walls. Test in corridor map first.
3. **Perf on old GPUs**: 64x6 taps full-res too heavy for min-spec; default half-res + early-out + reflection skip is mandatory, not optional.
4. **GLSL version bump**: mixing 330 + 430 programs in one context is legal, but `CreateComputeProgram` + image bindings are new code paths — validate link logs verbosely.
5. **License**: TerrainEngine-OpenGL has `LICENSE` (check terms — likely MIT; verify before copying shader text verbatim; keep attribution header to fede-vaccaro + NadirRoGue/WFP/reinder/Rikard/Clay-John as in source `.comp` header).
6. **No live time-of-day**: H2 has no sun cycle; "dynamic" initially means sun-height presets + animated drift (`iTime * cloudSpeed`), not full day cycle — set expectations.

---

## 7. Acceptance criteria

- [ ] `r_dynsky 0` == today's sky (regression clean).
- [ ] `r_dynsky 1 + r_volclouds 0` == gradient + sun sky, presets switch, fog still correct, stars/aurora/particles intact.
- [ ] `r_volclouds 1` == animated clouds over sky, occluded by walls (depth test), tunable via cover/speed/density, no crash in reflection/water maps, fallback message if GL<4.3.
- [ ] No `ref_gl1` regression, no new libs, Beta builds via MSVC + mingw, `PROGRESS.md` updated per portion.

---

## 8. Suggested first commit after this plan

`scaffold: add gl3_SkyDyn stub + cvars (no visual change)` — proves build/plumbing before any GLSL port.

---

## 9. Phase 5 — Night + moon (added per test feedback, after Phase 4)

`r_sky_preset 3` = night. No auto cycle yet (manual select; full day/night
cycle stays a Phase 6 item) and world lightmaps are untouched (sky-only night).

- Tier-A: night gradient (near-black blue top, faint horizon glow), moon disc
  (tight core + wide halo, HDR-bright so bloom catches it), procedural
  two-octave hash star field with twinkle (`uNight`/`uTime`), sun off.
  Sunset keeps a touch of stars (`night=0.2`). Sun/moon colors are uniforms
  now (day/sunset disc colors unchanged from before).
- Tier-B: moon becomes the light — direction, cool dim `lightColor`, dark blue
  ambient cloud colors; new `glareColor` uniform replaces the hardcoded orange
  glare (day value identical to before, night = pale blue).
- Stars show through cloud alpha automatically (Tier-A is the backdrop).
- Out of scope: legacy textured sky at night (stays map art), world lighting.
