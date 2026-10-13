# Progress Log — Beta Branch

Workflow (per user instruction):
- For each change, write every step taken + reason in this file.
- Commit and push to git after each portion is completed.
- Current branch: `Beta` (from `main` @ d4495920).
- Remote: `origin` (SSH: git@github.com:Lyky35/Heretic2Remastered.git).

---

## 2026-10-13 — Create `Beta` branch

Steps:
1. Inspected local repo at `/home/cyklon/Work/Heretic2Remastered` — confirmed git repo with remote `https://github.com/Lyky35/Heretic2Remastered.git`, branch `main`, clean except untracked `fxstaff_debug.log`.
   - Reason: verify starting state before branching.
2. Ran `git fetch origin` — local `main` was stale (ahead by 4); after fetch, `main` == `origin/main` @ `d4495920`.
   - Reason: ensure `Beta` is cut from latest remote `main`, not a stale local.
3. Checked GitHub via `gh repo view` + `git ls-remote --heads origin` — only `main` existed on remote.
   - Reason: confirm `Beta` name is free and avoid collision.
4. Ran `git checkout -b Beta` (local).
   - Reason: create the requested branch from current `main`.
5. Direct `git push -u origin Beta` over HTTPS failed (`could not read Username`) — no credential helper, while `gh` is configured for SSH protocol.
   - Reason: documented failure to explain next step.
6. Created remote branch via `gh api repos/Lyky35/Heretic2Remastered/git/refs --method POST -f ref=refs/heads/Beta -f sha=d4495920...` then `git fetch origin` + `git branch --set-upstream-to=origin/Beta Beta`.
   - Reason: `gh api` uses stored token, bypasses broken HTTPS credential setup; then link local tracking branch.

Result: `Beta` exists locally + remotely, both @ `d4495920`, tracking configured. Verified with `git branch -vv` and `git ls-remote --heads origin`.

## 2026-10-13 — Adopt PROGRESS.md workflow + fix push auth

Steps:
1. Confirmed `PROGRESS.md` did not exist (glob search).
   - Reason: need to create it to satisfy new workflow instruction.
2. Checked SSH: `ssh -T git@github.com` succeeded as `Lyky35`; `~/.ssh/` has `id_ed25519_github`.
   - Reason: HTTPS push failed earlier; SSH is working, so switching remote to SSH fixes future `commit + push after each portion`.
3. Ran `git remote set-url origin git@github.com:Lyky35/Heretic2Remastered.git` + `git fetch origin` (OK).
   - Reason: align remote URL with working SSH auth (matches `gh` config `Git operations protocol: ssh`); preserves same repo, only changes transport.
4. Created this `PROGRESS.md` file.
   - Reason: log every step + reason per instruction, starting now.
5. Ran `git add PROGRESS.md`, `git commit -m "docs: add PROGRESS.md workflow log for Beta branch"`, `git push origin Beta` — succeeded (`d4495920..41e01e0f Beta -> Beta`).
   - Reason: complete the "commit and push after each portion" portion for this workflow-setup change.

Result: `PROGRESS.md` committed as `41e01e0f` and pushed to `origin/Beta`. Future changes will append to this file first, then commit+push per portion. Note: `fxstaff_debug.log` left untracked (not part of this change).

## 2026-10-13 — Plan dynamic sky + clouds from TerrainEngine-OpenGL (no code yet)

Steps:
1. Cloned `fede-vaccaro/TerrainEngine-OpenGL` (shallow, to `/tmp/opencode/TerrainEngine-OpenGL`) via `gh repo clone`.
   - Reason: need source sky/cloud implementation locally to read exact shaders and uniforms instead of guessing from web.
2. Read source sky path: `DrawableObjects/Skybox.{h,cpp}`, `shaders/sky.frag`, `skyboxVert.vert`.
   - Reason: understand Tier-A gradient+sun background and preset system (day/sunset lerp by sun height) that clouds composite over.
3. Read source cloud path: `VolumetricClouds.{h,cpp}`, `CloudsModel.{h,cpp}`, `shaders/volumetric_clouds.comp` (571 lines, full read in 2 chunks), `weather.comp`, `clouds_post.frag`.
   - Reason: capture dome model (earth 600k / inner 5k / outer 17k), 64-step + 6-tap light march, perlinworley 128^3 + worley 32^3 + weather 1024^2, uniforms, fog/god-ray/post flow for the plan.
4. Measured shader sizes (`wc -l`) and dispatch sizes (`16x16` compute, `1024/8` weather).
   - Reason: quantify port cost (compute + 3D textures + half-res composite) vs H2's current 2D-only pipeline.
5. Delegated H2 sky research to explore subagent (Quake2 `ref_gl3`/`ref_gl1` sky, GL version, FBO/compute support, build).
   - Reason: parallelize; subagent returned file:line-grounded findings (6-face skybox `gl3_Sky.c:1080`, GL 4.6 ctx + GLSL 330 strings, no 3D/compute, `MAX_TEXTURE_UNITS 2`, MSVC+mingw build).
6. Verified key H2 facts directly: read `gl3_Sky.c:1080-1139 R_DrawSkyBox`, `grep` for `DispatchCompute/TexImage3D` (0 hits), `gl3_Shaders.c` `#version 330` + `CreateProgram`.
   - Reason: confirm subagent claims before writing plan; avoids planning against wrong GL/shader assumptions.
7. Wrote `DYNAMIC_SKY_PLAN.md` (source analysis + target analysis + 2-tier strategy + Phases 0-7 + files/risks/acceptance).
   - Reason: user asked for a plan (not code yet); 2-tier (A: gradient sky, B: volumetric compute) keeps `ref_gl1` + old sky as fallback and isolates risk.
8. Next: `git add DYNAMIC_SKY_PLAN.md PROGRESS.md`, commit, `git push origin Beta`.
   - Reason: complete "commit and push after each portion" for this planning portion.

Result: pending push (see next entry update after push).

## 2026-10-13 — Pushed sky plan (follow-up)

Steps:
1. Ran `git add DYNAMIC_SKY_PLAN.md PROGRESS.md`, `git commit -m "docs: add dynamic sky + clouds integration plan from TerrainEngine-OpenGL"`, `git push origin Beta` — succeeded (`92c937d2..f63232a0 Beta -> Beta`).
   - Reason: complete "commit and push after each portion" for the planning portion; keeps Beta remote in sync.
2. Verified with `git log --oneline -4` + `git status --short` (only untracked `fxstaff_debug.log` remains).
   - Reason: confirm push landed and working tree is clean except known untracked log.

Result: `f63232a0` pushed to `origin/Beta`. Plan is in `DYNAMIC_SKY_PLAN.md`. No code changes yet; suggested next portion is Phase 0 scaffold (`gl3_SkyDyn` stub + cvars).

## 2026-10-13 — Phase 0 scaffold: `gl3_SkyDyn` stub + cvars (no visual change)

Steps:
1. Inspected integration points: `gl3_Sky.h` (exports), `gl3_Local.h` (cvar externs + state), `gl3_Main.c` (`R_Register` cvar block ~275-318, `RI_Init` shader/FBO init ~474-489, `RI_Shutdown` ~510-532), `gl3_Shaders.{h,c}` (`CreateProgram` vert+frag only, GLSL 330), `ref_gl3.vcxproj` (`ClCompile`/`ClInclude` lists), `CMakeLists.txt` (parses vcxproj, so no CMake edit needed).
   - Reason: find exact wire-up spots so the stub follows existing patterns (cvar define-in-Main/extern-in-Local, init-after-shaders, vcxproj-only file list).
2. Created `src/ref_gl3/src/gl3_SkyDyn.h` (declares `GL3_SkyDyn_RegisterCvars/Init/Shutdown/Enabled/VolumetricEnabled`).
   - Reason: single new module for all Tier A/B work; header-only contract keeps `gl3_Main.c` diff minimal.
3. Created `src/ref_gl3/src/gl3_SkyDyn.c` (registers 14 cvars: `r_dynsky`, `r_volclouds`, `r_sky_preset`, `r_cloud_res/coverage/speed/density/crisp/curl/absorb/seed/debug/blur/godrays`; `Init` probes `GLAD_GL_VERSION_4_3` and prints status, always returns true; `Shutdown` is a no-op placeholder; gates check cvar + GL version).
   - Reason: defaults all off so behavior is pixel-identical; soft-fail on old drivers instead of crashing; TerrainEngine defaults preserved for later phases.
4. Edited `gl3_Local.h`: added 14 `extern cvar_t*` declarations.
   - Reason: same pattern as `r_reflections`/`r_fog_*` — extern in Local, define in Main.
5. Edited `gl3_Main.c`: defined 14 `cvar_t*` globals; `#include "gl3_SkyDyn.h"`; called `GL3_SkyDyn_RegisterCvars()` at end of `R_Register()`; called `GL3_SkyDyn_Init()` after `GL3_InitShaders()` (warn-and-continue on failure); called `GL3_SkyDyn_Shutdown()` in `RI_Shutdown()` before `GL3_ShutdownShaders()`.
   - Reason: cvars available before init; init order matches shader dependency; shutdown reverses init.
6. Edited `src/ref_gl3/ref_gl3.vcxproj`: added `ClCompile src\gl3_SkyDyn.c` + `ClInclude src\gl3_SkyDyn.h`.
   - Reason: MSVC + mingw-CMake (which parses vcxproj) both pick up the new file with one edit.
7. Build-checked: `cmake -B /tmp/opencode/h2-build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake` (configure OK) + `cmake --build /tmp/opencode/h2-build --target ref_gl3 -j8` — built 100%, including `gl3_SkyDyn.c.obj`, linked `build/ref_gl3.dll` (gitignored via `/build/**`).
   - Reason: verify no compile/link errors before committing; proves vcxproj wiring works for both toolchains.
8. Ran `git add` (gl3_SkyDyn.c/h, gl3_Local.h, gl3_Main.c, ref_gl3.vcxproj, PROGRESS.md), commit `e575b6ad`, `git push origin Beta` — succeeded (`2a5f5efb..e575b6ad`).
   - Reason: complete "commit and push after each portion" for this Phase 0 portion.

Result: `e575b6ad` pushed to `origin/Beta`. Suggested next portion: Phase 1 Tier-A gradient sky + sun (`fragmentSourceSkyDyn` + FSQ + presets + `R_DrawSkyBox` hook).

## 2026-10-13 — Phase 1 Tier-A gradient + sun sky

Steps:
1. Inspected Tier-A integration points: `gl3_Shaders.c` embedded GLSL pattern (`CreateProgram` vert+frag, `#version 330`), FSQ VAO (`vaoFSQ`, pos2+tc2, `TRIANGLE_STRIP`) + `GL3_UseShader` tracker, `gl3_Sky.c:1080-1235 R_DrawSkyBox` (depthmask off, sky_mv upload, cull/clip disable, 6 quads, aurora/stars tail, modelview restore), column-major `r_world_matrix`/`r_projection_matrix` (`GL_FALSE` upload), `gl3state.fbo_width/height`, `viddef` in `vid.h`.
   - Reason: Tier-A must reuse FSQ + shader conventions, preserve aurora/stars/particles tail, and reconstruct rays from inverse matrices exactly like source `sky.frag`.
2. Rewrote `gl3_SkyDyn.h`: added `GL3_SkyDyn_TierAReady()` + `GL3_SkyDyn_DrawTierA()` declarations.
   - Reason: hook needs a readiness gate so shader-compile failure falls back to legacy sky instead of drawing nothing.
3. Rewrote `gl3_SkyDyn.c`: embedded `vertexSourceSkyDyn` (FSQ layout, `gl_Position = vec4(aPos,1,1)` -> NDC far depth so LEQUAL keeps all opaque in front, like legacy clipdist quads) + `fragmentSourceSkyDyn` (port of TerrainEngine `colorCubeMap` gradient + `getSun(d,350)`, ray via `uInvProj/uInvView` + `gl_FragCoord/uResolution`); `SkyDyn_CompileShader` + `SkyDyn_InitTierA` (link, cache 6 uniform locations); `SkyDyn_InvertMatrix` (column-major 4x4 adjugate); `SkyDyn_Preset` (day top `0.525,0.735,0.84`/bottom `0.9,0.9,0.95`/sun `0.35,0.75,0.30`; sunset top `133,158,214`/bottom `241,161,161`/sun `0.85,0.14,0.25`; auto(2)->day until Phase 6 sun-cycle); `Init` now builds Tier-A (warn-and-continue on fail), `Shutdown` deletes program, `DrawTierA` inverts matrices, picks preset, sets uniforms, draws `vaoFSQ` strip. Added `#include "vid.h"` for `viddef` fallback size.
   - Reason: keep GLSL 330 (no version bump needed for Tier-A); far-depth FSQ preserves depth behavior; presets match source `DefaultPreset/SunsetPreset1`; soft-fail keeps old sky on any error.
4. Edited `gl3_Sky.c`: `#include "gl3_SkyDyn.h"`; `R_DrawSkyBox` computes `dynSky = Enabled && !Volumetric && TierAReady`, skips the `sky_images==NULL` early-out when Tier-A (no textures needed), and branches after cull/clip disable: Tier-A FSQ vs legacy 6-quad loop. Sky_mv upload + aurora/stars + clip/cull/modelview restore + swamp tail unchanged.
   - Reason: minimal diff; aurora/stars still get sky-space modelview; `r_dynsky 0` path is byte-identical logic to before.
5. Build-checked: mingw `ref_gl3` rebuilt 100% linked; forced recompile of `gl3_SkyDyn.c` + `gl3_Sky.c` shows zero warnings/errors.
   - Reason: verify Tier-A compiles clean before committing; no runtime test available on this Linux host (Windows game).
6. Ran `git add` (gl3_SkyDyn.c/h, gl3_Sky.c, PROGRESS.md), commit `0050c1ac`, `git push origin Beta` — succeeded (`3a17c5aa..0050c1ac`).
   - Reason: complete "commit and push after each portion" for this Phase 1 portion.

Result: `0050c1ac` pushed to `origin/Beta`. Manual test on Windows still needed: `r_dynsky 1`, `r_sky_preset 0/1/2`, check gradient + sun disc, `r_dynsky 0` regression, Silverpring aurora/stars on top. Suggested next portion: Phase 2 compute + 3D-texture infrastructure.

## 2026-10-13 — R8.7 Beta test package (Windows x64, Beta branch)

Steps:
1. Checked release conventions: tags `R8_6` etc, `gh release list` (latest R8.6), `pack_release.bat` (zips LICENSE/README/build exe+DLLs), `qcommon.h` version strings still `R8_00` (not bumped per release — followed same practice, no version-code change).
   - Reason: name/package the Beta consistently with official releases without touching version logic.
2. Downloaded R8.6 asset + `unzip -l`: layout is root folder `HereticIIRemastered_R8_6/` with flat binaries + `base/` (gamex86/Client Effects/Player/Default_H2R.cfg) + `OpenAL32.dll` + hand-written `README.txt` (stale R8_1 header).
   - Reason: Beta zip must drop into a game install exactly like the official one; `OpenAL32.dll` is not in repo `lib/` (only the .lib), so reuse the redist DLL from R8.6.
3. Full Beta-branch build: `cmake --build /tmp/opencode/h2-build -j` (all targets, HEAD `930176b4`) — 100%, no errors. Verified `strings build/ref_gl3.dll` contains `r_dynsky/r_volclouds/r_sky_preset` + Tier-A messages, and `Heretic2R.exe` is a valid x64 PE.
   - Reason: prove every binary in the package is current Beta code (esp. `ref_gl3.dll` with Phase 0+1), not a stale MSVC build.
4. Staged `/tmp/opencode/R87Beta/HereticIIRemastered_R8_7_Beta/` mirroring R8.6 (fresh Beta exe+DLLs, R8.6's OpenAL32.dll, Beta `Default_H2R.cfg`) + new `README.txt` (R8.7 Beta header + Tier-A console test steps + report checklist).
   - Reason: tester needs exact install layout plus Beta-specific instructions in one file.
5. Zipped to `release/HereticIIRemastered_R8_7_Beta.zip` (15 files, same structure as R8.6; `release/` is gitignored per `.gitignore:2`, `build/` per line 1 — neither committed).
   - Reason: keep binaries out of git; package is a local artifact until a GitHub prerelease is requested.
6. Ran `git add PROGRESS.md`, commit, `git push origin Beta` — succeeded (log commit for packaging).
   - Reason: complete "commit and push after each portion" for this packaging portion.

Result: `8b3dc133` pushed to `origin/Beta`. Test package is at `release/HereticIIRemastered_R8_7_Beta.zip` (gitignored local artifact).

## 2026-10-13 — Published R8.7 Beta prerelease on GitHub

Steps:
1. Created annotated tag `R8_7_Beta` on Beta HEAD (`8b3dc133`, message "R8.7 Beta: Tier-A dynamic sky test package") + `git push origin R8_7_Beta` — succeeded (`[new tag]`).
   - Reason: tag naming follows existing convention (`R8_6`, `R8_4`...); annotated tag records what the Beta contains.
2. Ran `gh release create R8_7_Beta --target Beta --title "R8.7 Beta" --prerelease` with notes (Tier-A test steps, report checklist, r_volclouds not-yet note, install instructions) + asset `release/HereticIIRemastered_R8_7_Beta.zip` — succeeded.
   - Reason: user asked to publish; prerelease (not latest) so R8.6 stays the stable download.
3. Verified with `gh release view R8_7_Beta --json name,tagName,isPrerelease,assets`: name `R8.7 Beta`, tag `R8_7_Beta`, `isPrerelease true`, 1 asset `HereticIIRemastered_R8_7_Beta.zip`.
   - Reason: confirm the release is live with the correct file before announcing.
4. Ran `git add PROGRESS.md`, commit `46039da0`, `git push origin Beta` — succeeded (`8b3dc133..46039da0`).
   - Reason: complete "commit and push after each portion" for this publishing portion.

Result: `46039da0` pushed to `origin/Beta`. Release live at https://github.com/Lyky35/Heretic2Remastered/releases/tag/R8_7_Beta.

## 2026-10-13 — Phase 2 compute + 3D-texture infrastructure (no visual change)

Steps:
1. Added to `gl3_Shaders.h` / `gl3_Shaders.c` (new section after `CreateProgram`, reusing static `CompileShader`): `GL3_CreateComputeProgram(comp_src)` (compile `GL_COMPUTE_SHADER` + link + log, returns 0 on fail), `GL3_MakeTexture3D(w,h,d,format)` (trilinear + REPEAT on S/T/R for world-XZ tiling + mipmaps), `GL3_MakeTexture2D(w,h,format)` (same 2D, for weather map), `GL3_BindImageTexture(unit,tex,layered,access,format)` (whole-level image binding), `GL3_MemoryBarrierCompute()` (`SHADER_IMAGE_ACCESS` barrier).
   - Reason: per plan Phase 2 — new `#version 430+` programs ride the 4.6 Core context while all existing 330 programs stay untouched; wrappers centralize image-unit handling away from the 2D-only `R_Bind/R_SelectTexture` cache (`MAX_TEXTURE_UNITS 2` left alone).
2. Wired a minimal `#version 430` probe (`imageStore` to `rgba8` image, compile+link only, no dispatch, no GL state touched) into `GL3_SkyDyn_Init()` when `GLAD_GL_VERSION_4_3` is present; prints `compute probe OK` or `compute probe FAILED`, deletes the program either way.
   - Reason: init-only verification per plan — proves the real Phase 3/4 shader path works on the user's driver at startup, with soft-fail (volumetrics disabled, Tier-A unaffected) instead of a crash.
3. Build-checked: mingw `ref_gl3` rebuilt, zero warnings/errors.
   - Reason: verify helpers compile clean before committing; runtime probe output will be confirmed from the user's Windows test.
4. Ran `git add` (gl3_Shaders.c/h, gl3_SkyDyn.c, PROGRESS.md), commit `e8525e50`, `git push origin Beta` — succeeded (`46039da0..e8525e50`).
   - Reason: complete "commit and push after each portion" for this Phase 2 portion.

Result: `e8525e50` pushed to `origin/Beta`. Suggested next portion: Phase 3 noise generation (perlinworley 128^3 + worley 32^3 + weather 1024^2 + `r_cloud_debug` slice view).

## 2026-10-13 — Phase 3 noise generation + debug views

Steps:
1. Read source noise mains + dispatch setup: `perlinworley.comp`/`worley.comp` (local 4^3, `rgba8` image3D binding 0, mains use `gl_GlobalInvocationID` directly, hardcoded 128.0/32.0 divisors, no uniforms consumed), `weather.comp` (local 16x16, `rgba8` image2D binding 0, uniforms `seed`/`perlinFrequency=0.8`), `CloudsModel.cpp` dispatches. Found source over-dispatches weather (1024/8 groups of 16 = 2x coverage, OOB imageStores).
   - Reason: exact port needs dispatch math + uniform names; OOB over-dispatch is not copied (exact 1024/16 used instead).
2. Confirmed MIT license on TerrainEngine-OpenGL (attribution header added in `gl3_SkyDyn.c`), no usable ms-timer in `refimport_t` (used `SDL_GetTicks` via `<SDL3/SDL.h>`, already an include dir), texture-cache invalidation pattern (`gl_state.currenttextures[] = -1`, as in `gl3_Image.c:928`).
   - Reason: embed shaders legally; time one-time gen cost; avoid stale-cache corruption from raw sampler binds in the debug view.
3. C logic in `gl3_SkyDyn.c`: `SkyDyn_GenNoise(force)` (perlin 128^3 @32^3 groups once, worley 32^3 @8^3 groups once, weather 1024^2 @64x64 groups on `r_cloud_seed`/`r_cloud_freq` change; `GL3_UseShader` for compute programs so the program tracker never desyncs; barrier + mipmap regen; ms timing + `glGetError` report), `GL3_SkyDyn_Update()` (two-float early-out, called from `R_DrawSkyBox` top), `GL3_SkyDyn_DrawDebug()` (modes 1 weather / 2 perlin slice / 3 worley slice, FSQ, raw binds + cache invalidation + depth restore, returns drawn flag). New `r_cloud_freq` cvar (extern `gl3_Local.h`, define `gl3_Main.c`, register `gl3_SkyDyn.c`). `Init` creates 3 compute + 1 debug program, runs first gen; `Shutdown` deletes all. `gl3_Sky.c` hook: `Update()` + early `DrawDebug()` return (replaces sky, keeps depth state safe).
   - Reason: one-time cost at init, weather-only regen afterwards; debug views validate noise before Phase 4 raymarch; all failures soft (Tier-A/legacy unaffected).
4. Embedded the 3 shaders verbatim via `/tmp/opencode/embed_shaders.py` (placeholder markers replaced; only change: latin-1 `á`->`a` in a weather comment to keep the C file ASCII-clean). Verified: 696 literal lines (315+289+92), distinctive lines present, no `??`/`\`/`"` hazards.
   - Reason: hand-transcribing ~700 GLSL lines risks typos; codegen guarantees byte fidelity (script is a local tool, not committed).
5. Build-checked: mingw `ref_gl3` rebuilt 100%, linked, zero warnings/errors.
   - Reason: verify before committing; runtime output (`noise generated in Xms`, `r_cloud_debug 1/2/3` views) needs Windows test.
6. Ran `git add` (gl3_SkyDyn.c/h, gl3_Sky.c, gl3_Local.h, gl3_Main.c, PROGRESS.md), commit `2e8cbeac` (+1018 lines, mostly embedded GLSL), `git push origin Beta` — succeeded (`4b71459e..2e8cbeac`).
   - Reason: complete "commit and push after each portion" for this Phase 3 portion.

Result: `2e8cbeac` pushed to `origin/Beta`. Windows test: console should show `noise generated in Xms (...)`, then `r_cloud_debug 1/2/3` fullscreen views. Suggested next portion: Phase 4 volumetric raymarch (+ new Beta package).

## 2026-10-13 — Updated R8.7 Beta release to Phase 1-3 binaries

Steps:
1. Verified Beta clean (`git status` only untracked `fxstaff_debug.log`) and HEAD == `origin/Beta` (`53865e77`).
   - Reason: release must match pushed HEAD exactly; no stray edits.
2. Full rebuild (`cmake --build /tmp/opencode/h2-build -j`, all targets, no errors) + verified `strings build/ref_gl3.dll` contains Phase 2-3 code (`r_cloud_debug`, `noise generated in`); `ref_gl3.dll` grew 459333 -> 489152 bytes.
   - Reason: previous R8.7 Beta asset only had Phase 1; fresh binaries prove current HEAD is packaged.
3. Refreshed stage `/tmp/opencode/R87Beta/...` with new binaries, updated `README.txt` (startup-log expectations + debug-view/seed test steps + report checklist), re-zipped to `release/HereticIIRemastered_R8_7_Beta.zip` (15 files, same R8.6-compatible layout; gitignored).
   - Reason: tester gets one self-contained drop-in with instructions matching the new binaries.
4. Moved tag: `git tag -f -a R8_7_Beta` (now Phase 1-3) + `git push -f origin R8_7_Beta` (forced update `d45dbe26..45bf3901`); `gh release upload R8_7_Beta <zip> --clobber` (asset now 6649863 bytes, updated 2026-10-13T21:41:44Z); `gh release edit` notes rewritten for Phases 1-3; release stays prerelease (R8.6 remains latest stable).
   - Reason: user asked to update R8.7 Beta in place; force-moving a Beta prerelease tag is safe and keeps one download URL.
5. Ran `git add PROGRESS.md`, commit `15911df8`, `git push origin Beta` — succeeded.
   - Reason: complete "commit and push after each portion" for this release-update portion.

Result: `15911df8` pushed to `origin/Beta`. R8.7 Beta release updated to Phase 1-3 binaries (same URL).

## 2026-10-13 — Fix black noise views: full memory barriers (AMD)

Report: `r_cloud_debug 1-3` fullscreen black on 7900XTX / GL 4.6.0 (SilverSprings); no FAILED lines; Tier-A gradient + sun works; only sky area black.
Steps:
1. Diagnosed from report: Tier-A works -> FSQ/uniform/resolution path fine. No FAILED -> all programs linked, textures created (non-zero IDs). All 3 views black -> all noise samplers return zeros. Cause: after `imageStore` writes, only `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` was issued — visible to further image access but NOT to `glGenerateMipmap` (reads base as texture fetch) nor sampler reads (`GL_TEXTURE_FETCH_BARRIER_BIT`). NVIDIA coheres anyway (source project never hit it); AMD RDNA is strict -> mips/samples read stale zeros -> black. Tier-A uses no textures -> unaffected. Matches every reported symptom.
   - Reason: fix the actual spec violation instead of guessing; also required for Phase 4 (same hazard in the cloud pass).
2. `gl3_Shaders.c` `GL3_MemoryBarrierCompute()` now issues `SHADER_IMAGE_ACCESS | TEXTURE_FETCH | TEXTURE_UPDATE`; header comment updated (call again after mipmap gen). `gl3_SkyDyn.c` `SkyDyn_GenNoise()` issues the barrier after each of the 3 `glGenerateMipmap` calls.
   - Reason: image writes -> visible to mipmap gen; mip writes -> visible to samplers; one helper keeps Phase 4 correct by default.
3. Rebuilt all targets (no errors), repackaged `release/HereticIIRemastered_R8_7_Beta.zip` (gitignored, same R8.6-compatible layout), verified fresh `ref_gl3.dll` in stage.
   - Reason: tester needs new binaries.
4. Ran `git add` (gl3_Shaders.c/h, gl3_SkyDyn.c, PROGRESS.md), commit `e3c2922f`, `git push origin Beta` (`15911df8..e3c2922f`); moved tag `R8_7_Beta` (`45bf3901..3440ed16`, forced); `gh release upload --clobber` (asset 6649865 bytes, updated 2026-10-13T21:55:57Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `e3c2922f` pushed to `origin/Beta`, R8.7 Beta asset refreshed. Retest ask: `r_cloud_debug 1/2/3` views + whether `noise generated` line changed.
