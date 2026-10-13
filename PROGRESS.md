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
8. Next: `git add` new + modified files, commit, `git push origin Beta`.
   - Reason: complete "commit and push after each portion" for this Phase 0 portion.

Result: pending push (see next entry update after push).
