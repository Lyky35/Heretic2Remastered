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

## 2026-10-13 — Black views persist: readback diagnostics added

Report: still black after barrier fix. Prior fix verified in code but symptom unchanged -> barrier theory wrong/incomplete; guessing ends here.
Steps:
1. Added `SkyDyn_LogTexStats()` (`gl3_SkyDyn.c`): after every noise gen, reads back level 0 (`glGetTexImage` RGBA/UBYTE) and prints R min/max + nonzero count per texture, flagging `ALL ZEROS, compute writes lost`. Called for perlin/worley/weather in `SkyDyn_GenNoise()`. Added first-draw announce in `GL3_SkyDyn_DrawDebug()` (mode, program/texture IDs, uniform locations); flag reset in Shutdown. New `#include <stdlib.h>` for malloc/free.
   - Reason: readback PROVES on the user's GPU whether dispatches wrote anything; announce PROVES the debug draw runs and with what bindings. Next round is decisive either way (zeros = compute-side, nonzero+black = sampling-side).
2. Rebuilt all targets (no errors), repackaged Beta zip (gitignored), verified `ALL ZEROS` string in staged `ref_gl3.dll`.
   - Reason: ship diagnostics to the tester.
3. Ran `git add` (gl3_SkyDyn.c, PROGRESS.md), commit `ad638520`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6650749 bytes, updated 2026-10-13T22:03:30Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `ad638520` pushed, R8.7 Beta asset refreshed. Awaiting: full `GL3_SkyDyn` console paste + legacy-sky status.

## 2026-10-13 — Black views root cause: face culling (fix shipped)

Report data: probe OK, Tier-A ready, `noise generated in 0ms (perlin=2 worley=3 weather=4)`; readback perlin R 3-248 all-nonzero, worley 80-248 all-nonzero, weather 0-106 ~all-nonzero; `debug draw mode=1` with all-valid IDs/locs; legacy textured sky fine at defaults.
Steps:
1. Diagnosed: textures FULL of valid noise + valid bindings + draw "runs" (announce is CPU-side, prints regardless) yet black. Missing piece: `GL3_SkyDyn_DrawDebug()` runs at the TOP of `R_DrawSkyBox`, before cull state is touched — and the world pass culls FRONT faces (reflection: BACK; legacy sky comment `gl3_Sky.c:1142` disables culling for exactly this). The front-facing FSQ strip was culled -> zero fragments -> cleared-black FBO shows through. Tier-A/legacy draw later with cull disabled -> unaffected. Barrier change was a red herring (kept: still spec-correct and needed for Phase 4). Matches every symptom including valid-readback-yet-black.
   - Reason: announce proved the draw call happens; cull state was the only unconsumed difference between the working Tier-A path and the debug path.
2. `gl3_SkyDyn.c` `GL3_SkyDyn_DrawDebug()`: save `GL_CULL_FACE`, `glDisable`, draw, restore — mirroring the legacy sky pattern.
   - Reason: minimal, state-safe fix; no other state (clip/depth) affects this program.
3. Rebuilt all targets (no warnings/errors), repackaged Beta zip (gitignored), staged fresh binaries.
   - Reason: ship fix to tester.
4. Ran `git add` (gl3_SkyDyn.c, PROGRESS.md), commit `7f020f90`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6650801 bytes, updated 2026-10-13T22:13:09Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `7f020f90` pushed, R8.7 Beta asset refreshed. Retest ask: `r_cloud_debug 1/2/3` should now show noise.

## 2026-10-13 — Phase 4 volumetric clouds (Tier-B raymarch + composite)

Steps:
1. Added `R_IsReflectionPass()` getter (`gl3_Surface.{h,c}`, next to the setter) for the reflection skip.
   - Reason: cloud compute must not run twice per frame (main + reflection); reflection reuses the main-pass framebuffer result.
2. Authored adapted `computeSourceClouds` (~330 GLSL lines, embedded directly): source raymarch core kept verbatim (dome intersect, 64-step march + Bayer jitter, 6-tap light march with Beer/HG/powder, height-gradient density, weather-gated coverage, worley erosion, fog, sun glare); changed: single `rgba16f` output (rgb+alpha, no bg baked in), no sky/depth inputs (bg used for lighting only, rebuilt from preset uniforms), dome via `r_cloud_base`/`r_cloud_thick`, miss/fog early-outs store alpha 0. Fixed two ordering hazards while writing (ambient defines + Bayer table before use). New `r_cloud_base` (1500) / `r_cloud_thick` (4500) cvars (extern Local.h, define Main.c, register SkyDyn.c).
   - Reason: Option-B composite (blend over already-drawn Tier-A/legacy sky) avoids an extra sky FBO; H2-scale dome; soft-fail preserved.
3. C plumbing in `gl3_SkyDyn.c`: cloud/composite program creation + ~25 uniform locations at init (`Tier-B ... ready` message); `GL3_SkyDyn_DrawClouds()` — gates (cvars, GL 4.3, programs, noise, main pass), cloud buffer (RGBA16F/LINEAR/CLAMP, follows framebuffer × `r_cloud_res`, recreated on resize), uniforms (matrices inverted, `r_newrefdef.time`, Tier-A sun/preset colors, all `r_cloud_*` knobs, absorption ×0.01 like source), samplers 0-2 + image 0 dispatch `(w+15)/16`, barrier, then composite FSQ (`vertexSourceSkyDynTextured` far-depth + blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA with save/restore, cache invalidation, one-time announce). `Shutdown` deletes all. `gl3_Sky.c` hook after sky quads (cull/clip off, depth writes off — ideal FSQ state), before aurora/stars tail.
   - Reason: far-depth composite gives free correct occlusion for high clouds via LEQUAL; documented Phase-5 limits (low-cloud overlap, stars-on-top).
4. Rebuilt all targets (zero warnings/errors), verified `volumetric clouds on` string in staged DLL, updated in-zip README (Tier-B steps + tuning + known limits), repackaged Beta zip (gitignored, R8.6-compatible layout).
   - Reason: tester needs new binaries + instructions in one drop.
5. Ran `git add` (7 sources + PROGRESS.md), commit `16be0b4c` (+633), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6656938 bytes, updated 2026-10-13T22:21:30Z); rewrote release notes for Tier-B — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `16be0b4c` pushed, R8.7 Beta asset refreshed with Tier-B. Retest ask: `r_dynsky 1` + `r_volclouds 1` cloud look, FPS at res 0.5/1.0, occlusion, stars-on-top.

## 2026-10-13 — Fix Tier-B feedback: legacy bg + vertical panning (axis bug)

Report: with `r_volclouds 1`, old textured skybox shows through; clouds look vertically panned. Volumetrics confirmed working.
Steps:
1. Legacy-bg cause (static): `dynSky = Enabled && !Volumetric && TierAReady` — leftover from the plan's "Tier B takes over" idea — forced the legacy 6-face path whenever volumetrics were on, while Phase 4 composites over the framebuffer (and lights clouds with preset colors). Changed to `Enabled && TierAReady` so Tier-A gradient is the backdrop under clouds (`gl3_Sky.c`).
   - Reason: predictable, matching backdrop; legacy+clouds can return later as an explicit mode if wanted.
2. Vertical-pan cause: H2 world is Z-up, ported shaders are Y-up. Tier-A gradient/sun used `worldDir.y` + Y-up sun (sideways gradient, sun stuck at horizon); Tier-B dome math (center snap, height fraction, UV projection, height-case branches) ran on wrong axes (deck placed sideways, case logic driven by a horizontal coord). Fixed: Tier-A frag gradient now `worldDir.z`, sun swizzled (x,z,y) at upload; Tier-B ray dirs swizzled `worldDir.xzy` in-shader + camera passed (x,z,y); presets documented as Y-up canonical. Tier-B lighting bg already uses post-swizzle `.y` — correct as-is.
   - Reason: single conversion point per path; note Tier-A look changes too (gradient now vertical, day sun overhead — correct).
3. Rebuilt all targets (zero warnings/errors), repackaged Beta zip (gitignored), staged fresh binaries.
   - Reason: ship both fixes together (cloud placement can't be judged with wrong bg/axes).
4. Ran `git add` (gl3_Sky.c, gl3_SkyDyn.c, PROGRESS.md), commit `4c3b95e6`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6656966 bytes, updated 2026-10-13T22:28:30Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `4c3b95e6` pushed, R8.7 Beta asset refreshed. Retest ask: Tier-A gradient orientation/sun height, cloud placement/drift with `r_volclouds 1`, legacy sky gone under clouds.

## 2026-10-13 — Clouds in water reflections (remove reflection skip)

Report: volumetrics work, but water doesn't reflect clouds.
Steps:
1. Confirmed in `R_RenderReflection` (`gl3_Main.c:986-1072`): reflected `r_origin` (line 1010), mirrored `r_world_matrix` (1026-1033) + projection (1025) are current when `R_DrawWorld` -> `R_DrawSkyBox` runs for the mirror. Tier-A already reflects (same inputs), so the cloud pass needs no special-casing — the Phase-4 skip was pure cost saving, now unwanted.
   - Reason: enable instead of work around; mirrored rays reconstruct correctly for free.
2. `gl3_SkyDyn.c` `GL3_SkyDyn_DrawClouds()`: removed the `R_IsReflectionPass()` early return; header comment updated (runs in both passes). Cull/depth handling at the hook is identical in both passes, so no other change.
   - Reason: one-line behavior change; cost is a second raymarch while water reflections render (noted in README).
3. Updated in-zip README known-limits (reflections now include clouds + cost note).
   - Reason: tester-facing docs stay accurate.
4. Rebuilt all targets (zero warnings/errors), repackaged Beta zip (gitignored), staged fresh binaries.
   - Reason: ship to tester.
5. Ran `git add` (gl3_SkyDyn.c/h, PROGRESS.md), commit `80a02f85`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6656988 bytes, updated 2026-10-13T22:34:27Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `80a02f85` pushed, R8.7 Beta asset refreshed. Retest ask: clouds visible in water.

## 2026-10-13 — Phase 5 night + moon (`r_sky_preset 3`)

Request: night time with moon. Manual preset (no auto cycle; world lightmaps untouched — sky-only night).
Steps:
1. Tier-A (`gl3_SkyDyn.c` frag): night gradient (near-black blue + horizon glow), moon disc (1200-power core + 24-power halo, HDR-bright for bloom), procedural 2-octave hash star field with twinkle gated by new `uNight`/`uTime` uniforms and a Z-up horizon mask; sun/moon disc colors now uniforms (day/sunset sun identical to before). New `starLayer()` + 5 uniform locations (cached, reset in Shutdown).
   - Reason: one backdrop serves both tiers; stars show through cloud alpha for free.
2. Extended `SkyDyn_Preset()` (now also fills sunColor/moonDir/moonColor/night; Y-up canonical documented): preset 3 = night (moon high, sun parked below horizon, night=1), sunset gains faint stars (0.2). Updated `DrawTierA` (accepts 0-3, uploads moon/swizzled dirs/colors/night/time) and `DrawClouds` (accepts 0-3).
   - Reason: single preset source for both tiers, no divergence.
3. Tier-B: new `glareColor` uniform replaces hardcoded orange glare (day value bit-identical, night pale blue); night branch feeds moon dir + cool dim light + dark blue ambient cloud colors.
   - Reason: orange glare at the moon would be wrong; day look provably unchanged.
4. Docs: `DYNAMIC_SKY_PLAN.md` Phase 9 section, `r_sky_preset` comment (0/1/2/3), in-zip README (preset 3 + night notes).
   - Reason: keep plan/README accurate for the tester.
5. Rebuilt all targets (zero warnings/errors), repackaged Beta zip (gitignored), staged fresh binaries.
   - Reason: ship to tester.
6. Ran `git add` (gl3_SkyDyn.c, gl3_Local.h, plan, PROGRESS.md), commit `5826d281` (+145/-22), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6658209 bytes, updated 2026-10-13T22:38:52Z); rewrote notes — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `5826d281` pushed, R8.7 Beta asset refreshed with night + moon. Retest ask: preset 3 look, moon/stars, moonlit clouds.

## 2026-10-13 — Water: smoother waves + stronger mirror

Request: waves too blocky; water too opaque (should mirror more).
Steps:
1. Inspected `gl3_Warp.c` (SUBDIVIDE_SIZE 64, Gerstner in vertex shader with wavelengths 200/130/70/40 units) + water frag (`gl3_Shaders.c`: base = 65% refraction / 35% murky texture, then fresnel mirror with `r_reflections_intensity` 1.6) + `GL3_DrawWaterPoly` (cvar wiring; no alpha blending — water is opaque, so clarity must come from the mix factors, not alpha).
   - Reason: 64-unit quads undersample the 40-unit wave AND its analytic normals -> faceted/blocky look; the 35% murky base dulls the mirror.
2. `gl3_Warp.c`: SUBDIVIDE_SIZE 64 -> 32 (4x water tris, water-only cost; per-poly vert caps unchanged so `vbuf`/subdivider limits hold). Water frag: murky-base mix 0.35 -> 0.15. Cvars untouched (`r_water_wave_*`, `r_reflections_intensity`, `r_refractions_intensity` still tune live).
   - Reason: finer grid resolves waves + normals smoothly; clearer base lets refraction + mirror dominate. No blending-order risk taken.
3. Rebuilt all targets (zero warnings/errors), repackaged Beta zip (gitignored), staged fresh binaries.
   - Reason: ship to tester.
4. Ran `git add` (gl3_Warp.c, gl3_Shaders.c, PROGRESS.md), commit `e875c805`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6658208 bytes, updated 2026-10-13T22:48:13Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `e875c805` pushed, R8.7 Beta asset refreshed. Retest ask: wave smoothness + mirror strength.

## 2026-10-13 — Water defaults per tester: intensity 4, height 3, sharp 0

Request: exact default values.
Steps:
1. `gl3_Main.c`: `r_reflections_intensity` 1.6 -> 4.0 (full mirror at all angles: clamp(4*fresnel) = 1 everywhere), `r_water_wave_height` 0.6 -> 3.0, `r_water_wave_sharp` 0.5 -> 0.0 (pure sine rollers, no sharp crests). `gl3_Shaders.c` init-time uniforms matched (overwritten per-draw from cvars, but kept consistent).
   - Reason: as requested. Note: height 3 with summed wave amps ~3.9 gives swells up to ~11 units — watch for clipping through docks/geometry; sharp 0 removes crest bite entirely.
2. Rebuilt, repackaged, staged.
   - Reason: ship to tester. Caveat: CVAR_ARCHIVE values persist in existing vars.rc — old installs keep prior values until reset/deleted; fresh installs get the new defaults.
3. Ran `git add` (gl3_Main.c, gl3_Shaders.c, PROGRESS.md), commit `0a862ef6`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6658206 bytes, updated 2026-10-13T22:53:28Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `0a862ef6` pushed, R8.7 Beta asset refreshed with new water defaults.

## 2026-10-13 — Cloud defaults + per-level sky presets

Requests: `r_cloud_res 1`, `r_cloud_base 3500`, `r_cloud_thick 2500`; can `r_dynsky` be pre-defined per level?
Steps:
1. Defaults set exactly as asked (`gl3_SkyDyn.c` register + matching fallbacks in `DrawClouds`): res 1, base 3500, thick 2500.
   - Reason: as requested, no extras. Same archived-vars.rc caveat as water: existing installs keep old values until reset.
2. Explored level identity: renderer already keys maps off `r_worldmodel->name` substrings (`R_IsSilverpringMap`: ssdocks/sswarehouse/sstown/sspalace; `R_IsDarkmireSwampMap`: dmireswamp, `gl3_Sky.c:189-208`). Cvar API supports the mechanism: `cvar_t.modified` (`q_shared.h:244`) + `ri.Cvar_Set` (`ref.h:294`).
   - Reason: reuse the established convention instead of inventing identification.
3. Implemented `skylevels[]` table + `SkyDyn_ApplyLevelSky()` (`gl3_SkyDyn.c`), called from `GL3_SkyDyn_Update()` (runs every `R_DrawSkyBox`, so ordering vs map load doesn't matter): ss->day/0.35, dmire->sunset/0.70, default->auto/0.45. Applies once per map and only to never-touched cvars (own sets clear `modified` again, so next map re-applies; console/vars.rc touches set `modified` and are respected forever). Skipped entirely when `r_dynsky 0` or new `r_sky_levels 0` (master switch, default 1, archived). One console line per map load states what happened.
   - Reason: level art direction by default, user settings always win, kill-switch included.
4. In-zip README: per-level paragraph. Rebuilt all (zero warnings/errors), repackaged, staged.
   - Reason: ship to tester with docs.
5. Ran `git add` (gl3_SkyDyn.c, gl3_Local.h, gl3_Main.c, PROGRESS.md), commit `49f83f45` (+111), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6658947 bytes, updated 2026-10-13T23:06:41Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `49f83f45` pushed, R8.7 Beta asset refreshed. Note for tester: archived cvars keep old values — reset `r_cloud_res/base/thick` (or delete from vars.rc) to see new defaults; per-level rows apply on each map load until you touch the cvars.

## 2026-10-13 — Per-hub skies: morning/evening presets + full table

Request: Silverspring night, Darkmire morning, Andoria day, Kell evening, Hive night, Ogle day, Morcalavin day.
Steps:
1. New presets in `SkyDyn_Preset()` (`gl3_SkyDyn.c`): 4 morning (pale-gold low sun, cool blue dome, no stars), 5 evening (dusk purple-blue dome, deep-orange low sun, faint stars 0.3). Both Tier-A + Tier-B lit (morning pale-gold / evening amber light + glare tints; day/sunset/night values untouched). Clamps widened 0-5 in both draws; cvar comments updated.
   - Reason: morning/evening didn't exist; tints keep clouds coherent per preset.
2. Table extended to all hubs: ss->3, dmire->4, and->0, kellcaves/canyon->5, hive/gauntlet->3, oglemine/dungeon/cloud->0, default->2. Verified every retail BSP against the table with a script: 25/25 hit exactly one row — except `hivepriestess` also contains "ss", fixed by ordering "hive" before "ss" (commented).
   - Reason: first-match-wins tables rot silently; verified + documented.
3. README presets + per-level paragraph updated. Rebuilt (zero warnings/errors), repackaged, staged.
   - Reason: ship with accurate docs.
4. Ran `git add` (gl3_SkyDyn.c, gl3_Local.h, PROGRESS.md), commit `a90b45c3` (+68/-7), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6659813 bytes, updated 2026-10-13T23:21:32Z); extended release notes — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `a90b45c3` pushed, R8.7 Beta asset refreshed. Retest ask: per-hub moods across maps.

## 2026-10-13 — `r_sky_preset` auto follows the level live

Request: auto option takes the preset defined by the level/mapname.
Steps:
1. Added `SkyDyn_LevelPreset()` (live table lookup, no cvar writes; default row preset made concrete 0/day so menu/no-map resolves sanely). Both draw funcs now map 2 -> live lookup (shared replaceAll; old `-> 0 day` alias retired).
   - Reason: previously auto was a static day alias and the table latched the cvar (churn, order-dependent); live resolution is predictable and instant.
2. Rewrote `SkyDyn_ApplyLevelSky()`: preset is never written anymore (auto resolves live, anything else is the user's fixed choice); coverage still latches once unless touched; single console line per map (`preset auto/manual %d, coverage level/yours %.2f`). Table comments + cvar comments updated.
   - Reason: no cvar churn; semantics a tester can state in one sentence.
3. README (auto description + per-level paragraph). Rebuilt (zero warnings/errors), repackaged, staged.
   - Reason: ship with accurate docs.
4. Ran `git add` (gl3_SkyDyn.c, gl3_Local.h, PROGRESS.md), commit `d6198e89` (+64/-24), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6659669 bytes, updated 2026-10-13T23:27:32Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `d6198e89` pushed, R8.7 Beta asset refreshed. Note: previous build latched presets into vars.rc (e.g. dmire wrote 4) — set `r_sky_preset 2` once to return to live-auto.

## 2026-10-13 — Darkmire fog thinned via per-level fog scale

Request: Darkmire fog too thick, barely-there haze instead.
Steps:
1. Traced fog ownership: map `trigger_fogdensity` entities -> `ps.fog_density` -> per-frame client lerp writes `r_fog_density->value` directly (`cl_screen.c:1356-1364`), save/restore in `g_save.c`. A cvar latch would lose to that lerp within seconds — so levels scale at consumption, not storage.
   - Reason: fight the ownership and the fog snaps back; scale where the renderer reads it.
2. `skylevels[]` gained `fogScale` (dmire 0.15, everything else 1.0); `s_fogScale` assigned on every map change (never stale; follows `r_sky_levels` switch, independent of `r_dynsky`); new `GL3_SkyDyn_FogScale()` getter (header restored after an edit mishap, verified); `R_Fog()` (`gl3_Main.c`) multiplies density at the `GL3_SetFog` call. Underwater fog untouched; savegames keep raw map values.
   - Reason: preserves the map's per-area fog variation, just thinned (0.01 -> ~0.0015 effective: light haze at 500 units). ref_gl1 untouched (tester is on ref_gl3).
3. Console line now reports `fog x0.15`; README notes Darkmire haze. Rebuilt (zero warnings/errors), repackaged, staged.
   - Reason: ship with visible proof + docs.
4. Ran `git add` (gl3_SkyDyn.c/h, gl3_Main.c, PROGRESS.md), commit `1b525c95` (+59/-18), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6659857 bytes, updated 2026-10-13T23:33:06Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `1b525c95` pushed, R8.7 Beta asset refreshed. Retest ask: Darkmire fog level (console shows `fog x0.15` on map load).

## 2026-10-13 — `r_fog 0` doesn't stick: new `r_fog_scale` master knob

Report: fog set to 0 still too foggy.
Steps:
1. Root-caused in `SCR_UpdateFogDensity` (`cl_screen.c:1351`): every frame it writes `r_fog_density->value` (lerp toward server `trigger_fogdensity` value) AND forces `r_fog->value = 1`. Any manual `r_fog 0` / `r_fog_density 0` is overwritten within frames — the knobs can't work by design.
   - Reason: fix ownership, not symptoms; gating on `modified` was rejected (archived vars.rc values arrive modified, would silently kill map fog for everyone).
2. Added archived `r_fog_scale` (default 1.0, `cl_screen.c` decl/register): server target multiplied by it, `0` takes the clean-zero branch (density 0 + `r_fog` 0). Composes with the Darkmire renderer scale (effective = map x 0.15 x yours). Default behavior bit-identical.
   - Reason: one user-owned knob that sticks; map fog variation preserved at 1.0.
3. README fog-control lines. Rebuilt all (one pre-existing `input.h:21` inline warning, unrelated file untouched), repackaged, staged.
   - Reason: ship + docs.
4. Ran `git add` (cl_screen.c, PROGRESS.md), commit `8d80af2c` (+24/-2), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6660044 bytes, updated 2026-10-13T23:38:36Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `8d80af2c` pushed, R8.7 Beta asset refreshed. Use `r_fog_scale 0` for zero map fog (sticks, archived); tell me if haze remains — then it's clouds/mist, not post fog.

## 2026-10-13 — Darkmire fog row 0.15 -> 0.25

Request: `r_fog_scale` 0.25 as default for this level.
Steps:
1. Darkmire table row `fogScale` 0.15 -> 0.25 (`gl3_SkyDyn.c` one cell). Global `r_fog_scale` default stays 1.0 (that knob is the user's own; a cvar default can't be per-level, and 0.25 globally would dim every map for fresh installs).
   - Reason: level-scoped ask gets a level-scoped change; personal knob untouched.
2. Rebuilt, repackaged, staged.
   - Reason: ship to tester.
3. Ran `git add` (gl3_SkyDyn.c, PROGRESS.md), commit `12eafb60`, `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6660043 bytes, updated 2026-10-13T23:41:28Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `12eafb60` pushed, R8.7 Beta asset refreshed. Console shows `fog x0.25` on Darkmire load.

## 2026-10-13 — Darkmire water near-flat via per-level wave scale

Request: water wave height 0.15 on Darkmire.
Steps:
1. Same latch problem as fog (water reads `r_water_wave_height` live per draw), same cure: `waveScale` table column (dmire 0.05, rest 1.0) so effective = cvar x scale (3.0 x 0.05 = 0.15 on Darkmire with your default). `s_waveScale` assigned every map change (never stale, follows `r_sky_levels`); new `GL3_SkyDyn_WaveScale()` consumed in `GL3_DrawWaterPoly()`; console line extended (`waves x0.05`).
   - Reason: multiplier composes with your tuning instead of robbing it; single mechanism for all level looks.
2. Caught two edit mishaps before building: duplicated WaveScale decl in the header (removed) and a missing WaveScale definition caught by the linker; format string missing the waves specifier (extra arg silently ignored — fixed).
   - Reason: verify, don't assume — link + format string checked explicitly.
3. Rebuilt all (zero warnings/errors), repackaged, staged.
   - Reason: ship to tester.
4. Ran `git add` (gl3_SkyDyn.c/h, gl3_Shaders.c, PROGRESS.md), commit `9ac5eacd` (+51/-17), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6660184 bytes, updated 2026-10-13T23:47:26Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `9ac5eacd` pushed, R8.7 Beta asset refreshed. Darkmire water = your height x 0.05 (0.15 at default 3); console shows `waves x0.05`.

## 2026-10-13 — Auto guaranteed: migrate old latched values

Request: an auto option, auto-set; undefined levels run default, defined use definitions.
Steps:
1. Verified the steady state already matches: `r_sky_preset` default 2 (auto, live table lookup), `r_sky_levels` default 1, default table row for unmapped maps, per-map console line. The gap is upgraders only: pre-auto builds latched row values into the cvars, cleared `modified`, vars.rc archived them — now masquerading as fixed manual choices, so auto never engages.
   - Reason: genuine choices always arrive `modified` (console sets and archived loads both flag it); non-default + unmodified is provably latch residue.
2. `GL3_SkyDyn_RegisterCvars()` end: non-default unmodified preset -> reset 2, non-default unmodified coverage -> reset 0.45 (epsilon compare), each with a `migrated ...` console line. Runs once at renderer init.
   - Reason: one-time self-repair; real user settings (modified) untouched by construction.
3. Rebuilt (zero warnings/errors), repackaged, staged.
   - Reason: ship to tester.
4. Ran `git add` (gl3_SkyDyn.c, PROGRESS.md), commit `b2e3219c` (+33), `git push origin Beta`; moved tag `R8_7_Beta`; `gh release upload --clobber` (6660252 bytes, updated 2026-10-13T23:51:59Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `b2e3219c` pushed, R8.7 Beta asset refreshed. Auto is now genuinely automatic, including for upgraders.

## 2026-10-13 — Explored staff-trail distortion (Max Payne refraction over ribbon)

Request: explore replacing staff trails with bullet-trail-style visual distortion.
Steps:
1. Mapped current trail via explore subagent (grounded in-tree): sprite-puff chain only (`fx_staff.c` `StaffLevel*Update`, additive `.sp2` billboards along pivot→tip, 500-2000 ms lifetimes); blade sampling already fixed (baked `refsForFrame`, `CORVUS_BLADE=5`); `FX_STAFF_SLASH`/debug log are removed history + untracked artifact; zero distortion code anywhere (only water refraction).
   - Reason: know exactly what gets replaced and what feeds the replacement.
2. Researched the technique (web): Max Payne 1 physical slowed bullets, MP2 screen-space velocity-skewed billboards, F.E.A.R./modern standard = screen-space refraction (scene texture S + normal-perturbed UVs + alpha mask; GPU Gems 2 Ch. 19 exact algorithm, chromatic taps, leakage guard); NeoFPS DistortionTrail / Trümpler one-shader same family.
   - Reason: pick the proven algorithm, not a guess.
3. Verified engine fit directly: `GL3_CopySceneToRefract()` (blit `fbo3D`→`fboRefract`, `gl3_Shaders.c:1118`) + water frag distorted projective UV (`:438-448`) = the needed primitive already in production; frame order world→entities→alpha→particles into HDR `fbo3D` before composite (`gl3_Main.c:1445-1502`). Chose dedicated `fboTrail` blit over reusing `fboRefract` (which lacks water = wrong backdrop where water is behind the trail).
   - Reason: proposal grounded in actual pipeline facts, not subagent claims alone.
4. Wrote `TRAIL_DISTORTION_PLAN.md` (ribbon builder, capture point, 330 shader w/ noise texture + cvars, `vid_ref`-gated gl1 fallback, T1-T3 phases, risks incl. degenerate segments + leakage + taste) and committed it on `R8.7_AdvShaders` (`4684d12a`, pushed) — implementation scoped there, not Beta. Back on Beta.
   - Reason: keep Beta to shipped/tested work; advanced-shader experiments live on the new branch.
5. Next: `git add PROGRESS.md`, commit, `git push origin Beta`.
   - Reason: complete "commit and push after each portion" for this research portion.

Result: pending push (see next entry update after push).

## 2026-10-13 — Created branch `R8.7_AdvShaders` from Beta

Steps:
1. Verified Beta clean (only untracked `fxstaff_debug.log`) at `e54e8130` (== origin/Beta).
   - Reason: branch must snapshot pushed HEAD exactly, no stray edits.
2. Ran `git branch "R8.7 AdvShaders"` as requested — rejected: `git check-ref-format` (git 2.55) disallows spaces in ref names.
   - Reason: document why the literal name couldn't be used.
3. Asked user; chose `R8.7_AdvShaders` (underscore, matches `R8_7_Beta` tag style).
   - Reason: closest valid spelling, user's call.
4. Ran `git branch R8.7_AdvShaders` + `git push -u origin R8.7_AdvShaders` — succeeded (`[new branch]`); stayed on Beta (verified `git branch --show-current`).
   - Reason: snapshot the full sky line (Phases 0-5 + fixes) for advanced-shader work without disturbing the Beta working branch.
5. Ran `git add PROGRESS.md`, commit `12729ebe`, `git push origin Beta` — succeeded (branch-creation log; the R8.7_AdvShaders branch push itself also done).
   - Reason: complete "commit and push after each portion" for this branch-creation portion.

Result: `12729ebe` pushed (branch-creation entry closed retroactively; exploration logged separately as `2d39995a`).

## 2026-10-13 — Released full R8.7 (stable) from Beta

Request: release as-is as full R8.7 (trail work stays on R8.7_AdvShaders, unmerged).
Steps:
1. On Beta at `2d39995a` (== origin, clean), full rebuild from Beta sources. Verified Beta purity of binaries (`volumetric clouds on` present, `r_trail_distort` absent in Client Effects) — the repo `build/` dir previously held AdvShaders binaries, so this check was load-bearing, not ceremonial.
   - Reason: a stable release must contain exactly Beta HEAD, nothing from the experimental branch.
2. Fresh stage `HereticIIRemastered_R8_7/` (R8.6-compatible 15-file layout) + final-toned README.txt (feature summary + console quickstart, no beta language).
   - Reason: stable packaging, self-documenting.
3. Tag `R8_7` on Beta HEAD (pushed) + `gh release create R8_7` targeting Beta, NOT prerelease (now latest; R8.6 superseded, R8.7 Beta prerelease left as history). Verified live with asset.
   - Reason: complete the release per request.
4. Ran `git add PROGRESS.md`, commit `295b46b1`, `git push origin Beta` — succeeded. Tag `R8_7` deliberately stays on `2d39995a` (the exact code tree the binaries were built from; HEAD adds only this log).
   - Reason: complete "commit and push after each portion" for this release portion.

Result: `295b46b1` pushed. Live: https://github.com/Lyky35/Heretic2Remastered/releases/tag/R8_7 (stable, latest). AdvShaders trail work untouched on its branch.

## 2026-10-13 — Removed "Check for Updates" + "Heretic II Online" menu items

Request: remove both (follow-up to inspection finding wrong-repo URLs + link-only Online).
Steps:
1. `menu_main.h`: dropped the two cvar externs. `menu_main.c`: removed both actions, handlers (`MainCheckUpdatesFunc` WinInet updater, `MainOnlineFunc` browser link), URL defines, `name_updates` buffer, WinInet/windows/SDL includes + pragma; re-packed quit 224 -> 160 (no gap); dropped both `Menu_AddItem`s. `menu.c`: dropped the two label cvar registrations.
   - Reason: full removal, not hiding — also kills the wininet dependency from this TU.
2. Verified zero remaining references (`spacefarergames`, both handlers, both cvars) in source AND in shipped `quake2.dll` strings (0 hits).
   - Reason: prove the dead URLs are really gone from the binary.
3. Rebuilt all (quake2 clean; only pre-existing unrelated warnings), repackaged R8.7 (15 files), staged.
   - Reason: ship to tester per standing release rhythm.
4. Ran `git add` (menu_main.c/h, menu.c, PROGRESS.md), commit `c96a7c11` (+17/-127), `git push origin Beta`; moved tag `R8_7`; `gh release upload --clobber` (6662179 bytes, updated 2026-10-15T04:42:00Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `c96a7c11` pushed, R8.7 asset refreshed. Main menu is now Game/Options/Video/Sound/Maps-Objectives/Quit.

## 2026-10-13 — Merged trail line into R8.7 stable

Request: merge AdvShaders (TrailT1 build) into R8.7.
Steps:
1. `git merge --no-ff R8.7_AdvShaders` — code merged with zero hand-merging (Beta touched only PROGRESS.md since the branch point); single PROGRESS.md conflict resolved by keeping both histories + this entry. Committed as `f452f007`, pushed to Beta.
   - Reason: no-ff preserves the experimental line for review.
2. Rebuilt from merge (no errors); verified merged binaries carry BOTH lines (`volumetric clouds on` + `r_trail_distort` present); repackaged R8.7 (md5-verified stage, 15 files) + README trail section.
   - Reason: prove the stable asset is exactly the merge, not a stale mix.
3. Moved tag `R8_7` to merge (`6ea4a91f..c8b3ad74`, forced); `gh release upload --clobber` (6663626 bytes, updated 2026-10-15T04:28:50Z); rewrote notes with trail paragraph — verified live. R8.7 Beta/TrailT1 prereleases + AdvShaders branch left alone.
   - Reason: "into R8.7" = same release updated in place, per established pattern.
4. Ran `git add PROGRESS.md`, commit `c60c8d29`, `git push origin Beta` — succeeded.
   - Reason: complete "commit and push after each portion" for this merge portion.

Result: `c60c8d29` pushed. Live: https://github.com/Lyky35/Heretic2Remastered/releases/tag/R8_7 (stable with trails). Note: tag `R8_7` sits on the merge `f452f007`; HEAD adds only this log.

## 2026-10-13 — Inspected "Check for Updates" + "Heretic II Online" (no code change)

Locations: `src/client/menus/menu_main.c` (items + handlers), labels via `m_banner_updates`/`m_banner_online` (`src/client/menu.c:593-594`). Both main-menu-only (grayed in-game).
Findings:
1. Check for Updates (`MainCheckUpdatesFunc`, :54-125): WinInet fetch of VERSIONDATE, int-compare vs local `VERSION_DATE`, opens releases page if newer, else prints "Up to date!" and grays the item. Logic sound (empty/parse-fail paths handled); blocks UI thread while fetching (minor).
2. WRONG REPO hardcoded: `UPDATES_URL` + `VERSIONDATE_URL` point to `spacefarergames/Heretic2Remastered`, but the project lives at `Lyky35/Heretic2Remastered` — the updater checks someone else's repo (or a dead one). Same stale owner on the Online URL (`spacefarergames.com/heretic2remastered/`).
3. Heretic II Online (`MainOnlineFunc`, :49-52) is just a website link in the browser — no server browser/multiplayer function.
4. Process gap: the updater only ever fires if `VERSIONDATE` on main is bumped per release (currently "1404" both sides) — needs a release-step convention.
   - Reason: logged for the pending URL/behavior decision; no code touched until owner confirms targets.
## 2026-10-13 — T1 staff ribbon on `R8.7_AdvShaders` (plain additive, no distortion yet)

Plan: `TRAIL_DISTORTION_PLAN.md` (`4684d12a` on this branch). Goal here: geometry only.
Steps:
1. Verified the cheap path exists: `SPRITE_LINE` semi-oriented strips in both renderers (`gl3_Sprite.c:91`, `gl1_Sprite.c:98` — gl1 fallback safe), established FX pattern (`fx_StaffHit.c` streaks), `OffsetLinkedEntityUpdatePlacement` transforms only `origin` (so ribbon segments go world-space static, `AddEffect(NULL)`), `CEF_NOMOVE` in `qcommon/EffectFlags.h`, free `RF_` bit `0x00800000` (CONTENTS_* reuse of the value is a separate domain).
   - Reason: zero renderer changes for T1; distortion flag honored in T2.
2. `src/game/q_shared.h`: `RF_DISTORT 0x00800000` (gl3 refracts later; others draw normal line).
   - Reason: the only cross-DLL contract T1+T2 need.
3. `src/client effects/fx_staff.c`: file-static ribbon state (single owner = player staff), `StaffTipToWorld` (same pivot+27*dir tip formula, owner-angles transform like L3), `StaffRibbonSegment` (prev→cur world chord, 0.5-unit hold-anchor, `SPRITE_LINE` + `RF_DISTORT`, midpoint culling, 300 ms fade, per-level model/color/width/alpha mirroring L1/L2/L3), gate in `FXStaff` (per-swing `Cvar_Get("r_trail_distort","0",0)` read — ref_gl3 registers default 1 first at vid init, so gl1 absence correctly yields puffs), branches in all three `StaffLevel*Update` after the movement gate (+ `q_Sprite.h` include; fixed the one compile error it caused).
   - Reason: puff loops byte-untouched for the fallback path; ribbon reuses proven field patterns.
4. `ref_gl3` `gl3_Local.h`/`gl3_Main.c`: `r_trail_distort` default 1 archived. gl1 untouched.
   - Reason: renderer-side master switch lives where T2 will read it too.
5. Built ALL targets mingw (only pre-existing warnings, e.g. `input.h` inline note); fixed the missing include from the single error.
   - Reason: `q_shared.h` edit rebuilds the world — verify everything links.
6. Ran `git add` (q_shared.h, fx_staff.c, gl3_Local.h, gl3_Main.c, PROGRESS.md), commit `a9a03599` (+124/-1), `git push origin R8.7_AdvShaders` — succeeded.
   - Reason: complete "commit and push after each portion" for T1.

Result: `a9a03599` pushed to `origin/R8.7_AdvShaders`. T2 next (dedicated blit + distortion shader + noise + cvars). No Beta release from this branch.

## 2026-10-13 — T2 trail distortion on `R8.7_AdvShaders` (blit + shader, no CE changes)

Plan: `TRAIL_DISTORTION_PLAN.md` T2, dedicated-blit variant per approval.
Steps:
1. Read integration points directly: `R_HandleTransparency/Cleanup` (`gl3_Misc.c:241`, normal-blend restore reusable), `GL3_Draw3DPoly` (streaming-VBO pattern), `GL3_CopySceneToRefract` blit (`gl3_Shaders.c:1118`), `GL3_InitRefract` FBO pattern + RI lifecycle (`gl3_Main.c:518/553`), `r_framecount` once-per-frame (`:674`), state struct layout.
   - Reason: mirror proven patterns instead of inventing plumbing.
2. `gl3_Shaders.c/h`: `vertexSourceTrail`/`fragmentSourceTrail` (330; projective screenUV + noise-perturbed sample + optional chromatic 3-tap + hot core + edge/age alpha), program + 9 uniform locations in `GL3_InitShaders` (samplers pinned 0/1), `GL3_InitTrail/ShutdownTrail` (full-res RGBA16F tex + color-only FBO + CPU tileable 2-octave value-noise texture via deterministic LCG), `GL3_CopySceneForTrail()` (blits CURRENT draw target viewport→trail tex, guarded once-per-frame-per-pass via `(r_framecount, R_IsReflectionPass)` so main + mirror each capture correctly), `GL3_TrailDistortReady()` gate. `gl3_Local.h` state + 4 cvar externs; `gl3_Main.c` defines + `r_trail_strength 0.035` / `r_trail_chroma 1` / `r_trail_core 0.6` + init/shutdown calls.
   - Reason: everything the draw needs, lifecycle-matched to refract.
3. `gl3_Sprite.c`: `R_DrawDistortLineSprite()` (same strip geometry as line sprite, tint from entity color, own normal-blend/bind/cull/depth handling, degenerate-direction guard, cache invalidation per established pattern) + branch in `R_DrawSpriteModel` before transparency setup (non-LINE or not-ready falls through to the T1 plain path). gl1 untouched (ignores the flag).
   - Reason: distortion composes in normal alpha order; T1 look is the automatic fallback everywhere.
4. Built ALL targets mingw, zero warnings/errors.
   - Reason: no runtime here — compile-clean plus review is the verification available.
5. Ran `git add` (6 files), commit `1d7022a1` (+401), `git push origin R8.7_AdvShaders` — succeeded.
   - Reason: complete "commit and push after each portion" for T2.

Result: `1d7022a1` pushed to `origin/R8.7_AdvShaders`. T3 next (chromatic/core polish, taper, perf) or Windows smoke test of T1+T2 first — untested at runtime.

## 2026-10-13 — 2nd prerelease: `R8_7_TrailT1` trail-distortion test build

Request: test build as a 2nd prerelease (R8.7 Beta untouched).
Steps:
1. Full AdvShaders HEAD build (all targets, no errors) + verified trail code in binaries (`trail FBO initialized`/`r_trail_strength` in ref_gl3.dll, `r_trail_distort` in Client Effects.dll).
   - Reason: never ship unchecked-in code; strings prove T1+T2 are in.
2. Staged `HereticIIRemastered_R87_TrailT1/` mirroring the R8.6 layout + trail-focused README.txt (startup line, swing checklist, knobs, fallback checks). Caught a missing SDL3.dll before zipping (14 vs 15 files — game wouldn't start); fixed and re-verified listing.
   - Reason: a test build that doesn't launch wastes a whole test round.
3. Created tag `R8_7_TrailT1` (target `R8.7_AdvShaders`, no clash with branch names) + prerelease `R8.7 TrailT1 (test)` with asset (first `gh release create` failed outside the repo — `GIT_DISCOVERY_ACROSS_FILESYSTEM`; reran from repo with `--repo`). Verified live with asset attached.
   - Reason: same URL pattern as R8.7 Beta; prerelease keeps R8.6 latest.
4. Ran `git add PROGRESS.md`, commit `2199ed3f`, `git push origin R8.7_AdvShaders` — succeeded.
   - Reason: complete "commit and push after each portion" for this release portion.

Result: `2199ed3f` pushed. Live: https://github.com/Lyky35/Heretic2Remastered/releases/tag/R8_7_TrailT1 (tag `R8_7_TrailT1`, 1 asset). Awaiting smoke test.

## 2026-10-13 — Trail feedback: stronger warp + connected bends

Report: distort effect too small; direction changes spawn a disconnected second trail instead of bending as one.
Steps:
1. Diagnosed disconnect: segments are independent quads sharing no edge verts, so the outside of every bend gaps open (reads as a separate trail). Not an anchor bug (sub-threshold hold keeps position continuous) — a joint-coverage bug.
   - Reason: fix coverage, not tracking.
2. `fx_staff.c` `StaffRibbonSegment`: extend each segment half a width past both ends along its axis (overlap lens over the joint; both sides sample the same clean background so double-blend is nearly invisible). Per-frame bends are gentle (fast sampling; reversals pass through the sub-threshold hold), so half-width overlap covers them.
   - Reason: connected-bend read without multi-point ribbon surgery (T3 candidate if this proves insufficient).
3. Presence: `r_trail_strength` default 0.035 -> 0.09; widths 6/8/10 -> 9/12/14 world units (L1/L2/L3). All still live-tunable.
   - Reason: "too small" addressed on both axes (warp amount + ribbon body).
4. Rebuilt all (no errors), refreshed TrailT1 stage + README default, repackaged (15 files verified).
   - Reason: ship to tester.
5. Ran `git add` (fx_staff.c, gl3_Main.c, PROGRESS.md), commit `1305dec7` (+33/-6), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6662828 bytes, updated 2026-10-15T03:06:55Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `1305dec7` pushed, TrailT1 asset refreshed. Retest ask: warp presence + bends reading as one trail.

## 2026-10-13 — Trail feedback 2: L2 double size, L3 flames restored

Report: base (L1) good; powered-up (L2) should be twice the original size; L3 should still spawn flames, with heat haze.
Steps:
1. L2 width 12 -> 18 (2x the L1 base width of 9). Nothing else touched.
   - Reason: exact ask; color/alpha/fade already approved implicitly.
2. L3 ribbon branch changed from early-return to emit-and-fall-through: the ribbon segment goes out, then the original flame/smoke/dlight loop runs unchanged beneath it (classic fire look + distortion haze over the same arc).
   - Reason: flames are the L3 identity; ribbon adds haze without removing anything. Flames stay non-distorting (fire should read as fire).
3. Rebuilt all (no errors), repackaged TrailT1 (15 files), staged.
   - Reason: ship to tester.
4. Ran `git add` (fx_staff.c, PROGRESS.md), commit `0ec1f870` (+20/-3), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6662829 bytes, updated 2026-10-15T03:12:07Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `0ec1f870` pushed, TrailT1 asset refreshed. Retest ask: L2 size, L3 flames + haze.

## 2026-10-13 — Trail feedback 3: smooth flow, no puff blocks, dimmer fire

Report: L2 not smooth; big ugly smoke-like puffs; fire too bright/flame-sprite look; wants larger flame haze.
Steps:
1. Diagnosed: wide (18u) independent quads with hard strip ends read as discrete puffs — geometry, not colors. Fix is shorter pieces + faded ends, not new shapes.
   - Reason: treat the disease (blocky segments), not symptoms.
2. `fx_staff.c`: each frame chord now emits 2 half-length sub-segments (ends[] midpoint split); L2 width 18 -> 22 for a larger haze body.
   - Reason: shorter quads corner-cut less and kink gentler; overlap + end fades keep joints seamless.
3. Trail fragment (`gl3_Shaders.c`): added along-strip end fade (`smoothstep` both t ends, works with the overlap extensions), softened across-edge to 0.40/0.60; `r_trail_core` default 0.6 -> 0.35 (core was blowing out to white on bright L2 tints).
   - Reason: haze needs soft volume, not hard quads or blown core.
4. Rebuilt all (no errors), repackaged TrailT1 (15 files), staged.
   - Reason: ship to tester.
5. Ran `git add` (fx_staff.c, gl3_Shaders.c, gl3_Main.c, PROGRESS.md), commit `050fd880` (+62/-29), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6663030 bytes, updated 2026-10-15T03:19:32Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `050fd880` pushed, TrailT1 asset refreshed. Retest ask: smooth L2 flow, fire brightness, haze size.

## 2026-10-13 — Trail feedback 4: L2 -30%, L3 smokeless, hellstaff haze

Requests: L2 trail+haze 30% smaller; L3 smoke removed; proportional fire haze on all fire effects.
Steps:
1. L2 width 22 -> 15.4 (exactly -30%; haze follows width via proportional edge fade). L1/L3 untouched.
   - Reason: exact ask.
2. L3 smoke block now `if (!ribbon_mode && ...)` — flames/smoke/dlight legacy loop otherwise intact, so non-ribbon paths pixel-identical.
   - Reason: smallest possible removal, fallback preserved.
3. Audited LINE fire emitters: hellstaff bolt (sustained fire -> haze sibling), mork fire streaks (transient debris bursts -> excluded), StaffHit sparks (impact, not fire -> excluded), flamethrower/phoenix/fire sprites (billboard-based -> needs STANDARD distortion path, deferred). Added hellstaff haze sibling (same endpoints, 2.2x width, warm tint, tile 1.0 — caught that tiled streak UVs would break the distortion end-fade, so haze uses unit tiling).
   - Reason: haze where fire sustains; debris/sparks would just add noise; billboard fire is a separate feature, stated not silently dropped.
4. Rebuilt all (no errors), verified fresh DLL timestamps + zip md5 match, repackaged (15 files), staged.
   - Reason: ship to tester with proof the binaries are current.
5. Ran `git add` (fx_staff.c, fx_HellStaff.c, PROGRESS.md), commit `aeb5fc3d` (+40/-3), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6663176 bytes, updated 2026-10-15T03:31:56Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `aeb5fc3d` pushed, TrailT1 asset refreshed. Retest ask: L2 size, smokeless L3, hellbolt haze.

## 2026-10-13 — Trail feedback 5: L2 legacy, L3 coreless, select ribbon

Requests: L2 back to original (no haze); L3 ribbon out, flames + haze stay; L1-select white sprites become L1 trail.
Steps:
1. L2: deleted the ribbon branch — pure legacy puff loop again (puffs were never the complaint there; size handled last round).
   - Reason: exact revert, no behavior delta possible.
2. New `RF_NOCORE` (0x01000000, RF-domain verified free): `StaffRibbonSegment` takes `noCore`, L3 passes true (L1/hell false); `R_DrawDistortLineSprite` packs it into vertex alpha, fragment multiplies the hot-core term by it (mask/alpha path untouched).
   - Reason: per-entity core kill — warp stays, bright body goes, flames underneath carry the look.
3. Mapped select path: `FXStaffCreate` controller + shaft-walk `StaffCreateOrRemoveUpdate` (white sprites, white default color) + endpoint `FXStaffCreatePoof` halo flashes (poof is only a materialize handler — safe to gate). Ribbon mode now emits one world-space shaft streak (L1 width, staff-type color) instead of the sprite walk, skips poofs, gate re-read in `FXStaffCreate`; deselect path shares the function so it matches.
   - Reason: the white flash IS the select sprites; shaft streak is the motionless equivalent of the swing ribbon.
4. Rebuilt all (no errors), repackaged TrailT1 (15 files), staged.
   - Reason: ship to tester.
5. Ran `git add` (fx_staff.c, q_shared.h, gl3_Sprite.c, gl3_Shaders.c, PROGRESS.md), commit `e058bc9b` (+70/-26), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6663656 bytes, updated 2026-10-15T03:43:15Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `e058bc9b` pushed, TrailT1 asset refreshed. Retest ask: L2 legacy, L3 fire+haze-without-body, select flash.

## 2026-10-13 — Trail feedback 6: select ribbon L1-only, L1 color

Report: materialize change hit L2/L3 too (L1 only, please); L1 flash shows green, should match L1 trail.
Steps:
1. Shaft-ribbon branch now requires `self->classID == STAFF_TRAIL` (normal/L1; blue/flames types keep legacy sprites); poof skip requires neither `CEF_FLAG7` nor `CEF_FLAG8` (same normal-only rule, flags already used in-file).
   - Reason: exact scoping, no behavior delta for L2/L3.
2. Shaft ribbon color: was the per-type `color` (green 0xff20ff20 for sword) — now hardcoded `0x02201010`, the actual L1 trail color set in `FXStaff` case 1 (commented at use).
   - Reason: "same as L1 trail" means the trail's color, not the staff-type flash color.
3. Rebuilt (no errors), verified fresh DLL timestamp + zip md5 match (zip size coincidence double-checked, not assumed), repackaged (15 files).
   - Reason: ship to tester with proof of freshness.
4. Ran `git add` (fx_staff.c, PROGRESS.md), commit `34b35389` (+25/-7), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6663664 bytes, updated 2026-10-15T03:49:48Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `34b35389` pushed, TrailT1 asset refreshed. Retest ask: L1 select flash color/scope.

## 2026-10-13 — Weapon heat haze: hellstaff charge, firewall linger, phoenix

Requests: hellstaff haze grows with sustained fire; firewall (base+powered) large lingering haze; phoenix arrow wake + sized explosion haze (powered: no flight haze, big 5s linger).
Steps:
1. Renderer first (all three need it): `uRadial` uniform + `R_DrawDistortPoly` shared tail extracted from the line path + `R_DrawDistortBillboard` (copies `R_DrawDynamicSprite` math for `RE_SetupRollSprite` world-size quads, radial mask); branch accepts LINE + DYNAMIC (others fall through plain). Warp-only via `RF_NOCORE`; masks need no texture reads.
   - Reason: billboards can't reuse strip UV logic; one program, one flag more.
2. Hellstaff (`fx_HellStaff.c`): file-static heat 0..1 (+0.12/shot, ~2s cool, capped) bumped in both `FXHellbolt` and `FXHellstaffPower`. Muzzle warp-only billboard per bolt (16u x growth, 700ms); power-beam haze width x growth (up to 2.5x sustained).
   - Reason: no persistent entities/thinks — heat derived on demand from shot timing; works for tap and hold alike.
3. Firewall (`fx_Wall.c`): one warp-only billboard per `FireWaveUpdate` at wall center sized `min(radius*2.5, 350)` (2s life outlives flames = linger) + one per `FireBurstUpdate` (110u, 1.5s). Covers powered wall + base burst proportionally by construction.
   - Reason: lingering falls out of lifespan, no expiry special-casing; ~120-180 extra quads worst case.
4. Phoenix: base `PhoenixMissileUpdate` wake billboard (48u, 400ms) — power update untouched per spec; base explode haze 160u/2s; powered explode haze 220u/5s per spec.
   - Reason: exact spec compliance (powered arrow clean in flight, big long linger on blast).
5. Excluded with reason: mork/StaffHit debris streaks + impact sparks (transient, haze would add noise), flamethrower/phoenix-trail sprite fire (needs fire-visible+mixed billboards — bigger feature, offered next).
   - Reason: haze where fire sustains; stated, not silently dropped.
6. Rebuilt all (no errors), repackaged TrailT1 (15 files), staged + README weapon section.
   - Reason: ship to tester.
7. Ran `git add` (4 FX files, gl3_Sprite.c/Shaders.c/Local.h, PROGRESS.md), commit `42427483` (+223/-58), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6664669 bytes, updated 2026-10-15T04:09:15Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: `42427483` pushed, TrailT1 asset refreshed. Retest ask: hellstaff growth, firewall linger, phoenix wake/blast per spec.

## 2026-10-13 — Reverted weapon heat haze (`bd8bbaa1`)

Report: not good — revert to prior commit.
Steps:
1. `git revert --no-commit 42427483` (forward-history revert, no rewrite: branch + tag stay clean). Resolved the one conflict in `PROGRESS.md` by keeping HEAD (full log history) — the revert commit touches only the 7 code files.
   - Reason: never rewrite pushed history; keep the attempt documented.
2. Rebuilt all (no errors), repackaged TrailT1 (15 files), staged.
   - Reason: ship the reverted binaries so TrailT1 matches pre-haze behavior + all accepted trail work.
3. Ran `git add PROGRESS.md`, commits `bd8bbaa1` (revert) + `56d42115` (log), `git push origin R8.7_AdvShaders`; moved tag `R8_7_TrailT1`; `gh release upload --clobber` (6663775 bytes, updated 2026-10-15T04:20:40Z) — all succeeded.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: revert live on branch + TrailT1 asset refreshed (back to staff-ribbon behavior + hellstaff beam haze only).

## 2026-10-13 — Merged `R8.7_AdvShaders` into Beta (trail line joins R8.7)

Request: merge the TrailT1 trail build into R8.7.
Steps:
1. `git merge --no-ff R8.7_AdvShaders` — code merged cleanly (Beta touched only PROGRESS.md since the branch point); resolved the single PROGRESS.md conflict by keeping both histories (Beta entries first, branch entries after, this note last).
   - Reason: no-ff preserves the experimental line for review; no code needed hand-merging.
2. Next: rebuild from merge, verify trail strings in binaries, repackage, move `R8_7` tag, re-upload asset, rewrite notes.
   - Reason: complete "commit and push after each portion" plus release refresh.

Result: pending build/release (see next entry update after push).
