# Vulkan Port Plan — Heretic2Remastered

Plan for converting the OpenGL renderer (`ref_gl3`, actually GL 4.6 core) to Vulkan.

---

## 0. Current state (facts the plan is built on)

| Item | Finding |
|---|---|
| Renderer | `src/ref_gl3/src/` — ~14.8k lines C, 27 files, loaded as `ref_gl3.dll` |
| Engine seam | Clean: `refexport_t` / `refimport_t` in `src/client/ref.h:187,277`, loaded via `GetRefAPI` in `gl3_Main.c:1192`, `src/win32/vid_dll.c:193,224`. **Zero GL calls outside `ref_gl1`/`ref_gl3`.** |
| Windowing | SDL3; window created by engine in `src/client/glimp_sdl3.c:205` using flags returned by `RI_PrepareForWindow()` (`gl3_SDL.c:22`), context created in `RI_InitContext()` (`gl3_SDL.c:78`). SDL3 vendored headers already ship `SDL_vulkan.h`. |
| Shaders | 10 programs, GLSL `#version 330 core`, embedded as C strings in `gl3_Shaders.c`; compile at `:354,388`, programs created at `:434,446,463,475,598,635,1011,1025,1244,1264`. No UBOs/SSBOs; 52 `glGetUniformLocation` sites. |
| Draw model | Immediate: `glBufferData(GL_STREAM_DRAW)` + `glDrawArrays` per polygon (`gl3_Shaders.c:795-836`), 2D quads (`gl3_Draw.c:116,143`), particles (`gl3_Main.c:1066`), fullscreen quads (`:1127,1152,1174,1412,1421`). |
| Textures | `glTexImage2D`/`glTexSubImage2D`, RGBA8/RGBA16F/RGB16F/R16F/DEPTH24; mips via `glGenerateMipmap`; anisotropy ext; gamma is CPU-side (`gl3_Image.c:19,312`) — no sRGB formats. |
| FBO targets | HDR scene `fbo3D` (+ depth/stencil tex) `gl3_Shaders.c:865`, reflection half-res `:943`, bloom extract + 2 ping-pong `:1048,1054`, SSAO + blur `:1366`. |
| State | No state cache except texture-unit binds (`gl3_Image.c:101-150`) and current program (`gl3_Shaders.c:422`). Scattered: 41 `glDisable`, 27 `glEnable`, 23 `glBlendFunc`, 22 `glDepthMask`. Special: `glDepthRange` (3), `glPolygonOffset` (2), stencil ref per-entity shadows (`gl3_Shadow.c:331-347`), `glPointSize` (`gl3_Sky.c:419,653`), `glScissor`, `glGetBooleanv(GL_CULL_FACE)` readback (`gl3_Sky.c:1146`), `glPolygonMode` (`gl3_Misc.c:56`). |
| Cinematics | PBO double-buffer + `glMapBufferRange` (`gl3_Draw.c:515-648`). |
| MSAA | Default-framebuffer MSAA via SDL GL attrs (`gl3_SDL.c:42-45`); no explicit resolve. |
| Screenshots | `glReadPixels` → `ri.Vid_WriteScreenshot` (`gl3_Misc.c:34`). |
| Threads | `gl3_Jobs.c`: up to 16 SDL3 worker threads doing CPU jobs; rendering itself is single-threaded. |
| Build | MSVC only: `src/Heretic2R.sln`, `src/ref_gl3/ref_gl3.vcxproj`. No CMake/Makefile. |
| Renderer selection | Hard-forced to GL3: `Cvar_ForceSet("vid_ref","gl3")` at `src/win32/vid_dll.c:339,372`, `FALLBACK_REFLIB "gl3"` at `:14`. |

---

## 1. Key architecture decisions (decide before Phase 1)

| # | Decision | Recommendation | Rationale |
|---|---|---|---|
| D1 | Port strategy | **New `ref_vulkan.dll` project cloned from `ref_gl3`; GL renderer stays intact** | Zero risk to the working renderer; `vid_ref` switch gives A/B comparison; DLL ABI already supports it. |
| D2 | Vulkan baseline | **Vulkan 1.3** (dynamic rendering, sync2, timeline semas) | Removes `VkRenderPass`/`VkFramebuffer` boilerplate for the 8 FBO targets; Deck/Win10+ drivers all support 1.3. No fallback path for 1.1/1.2. |
| D3 | Loader | **volk** (single-file) loaded via `SDL_Vulkan_GetVkGetInstanceProcAddr()` | No Vulkan SDK needed at runtime or link time; no `vulkan-1.lib` dependency; works under Proton. |
| D4 | Memory | **VMA (Vulkan Memory Allocator, MIT)** | Immediate-mode streaming + lightmap partial updates + FBO images make a custom allocator unjustified. |
| D5 | Shader pipeline | **GLSL 450 sources → `glslangValidator -V` at build time → generated `.spv.h` headers** checked into build step | No runtime shaderc dependency; vcxproj custom build step; SPIR-V blobs embedded like today's C strings, so the loader code shape barely changes. |
| D6 | Pipeline state | **Pipeline cache** keyed by `(shader, vertex layout, blend, depth, cull, polygonOffset, stencil)`; dynamic state = viewport/scissor/depthBias/stencilRef/blendConstants | Blend/depth/cull are toggled ~90 times/frame at scattered sites; making them dynamic state avoids PSO explosion while dynamic state on 1.3 is cheap. |
| D7 | Frames in flight | **2** | Standard; maps to GL's implicit pipelining; needed because `gl3_Jobs.c` threads exist. |
| D8 | Immediate-mode geometry | **Per-frame host-visible ring buffer** (persistent-mapped, `HOST_VISIBLE|HOST_COHERENT`), one `vkCmdDraw` per recorded polygon initially; later `vkCmdDraw` batching optional | Keeps per-draw call sites 1:1 during porting; batching is an optimization, not a correctness requirement. |
| D9 | MSAA | **Optional; msaa color+depth images + `VK_ATTACHMENT_STORE_OP_DONT_CARE` + explicit resolve blit** | Replaces default-framebuffer MSAA (`gl3_SDL.c:42`). |
| D10 | Screenshot/gamma/readback | GPU→staging buffer `vkCmdCopyImageToBuffer` + fence; gamma stays CPU-side (unchanged) | Direct map of `glReadPixels` path. |

---

## Phase 0 — Prerequisites & safety net (no Vulkan yet)

**Goal:** clean baseline so GL and Vulkan can be compared fairly.

1. Fix the glad inconsistency: `ref_gl3.vcxproj:22` compiles `glad-GL3.3/glad.c` while sources include `glad-GL4.6` headers and `gl3_Glad.c` exists but is not in the project. Pick one (compile `src/gl3_Glad.c`, drop the GL3.3 TU), build, run, verify GL renderer still works.
2. Add renderer logging: frame timing + draw-call counts behind `r_stats` cvar (needed for A/B perf later).
3. Capture golden screenshots: fixed demo/tick, save PNGs for a set of scenes (indoor, outdoor/sky, water, reflections, shadows, bloom-heavy, cinematics) via the existing `Vid_WriteScreenshot` path. Store under `stuff/golden/` (or any dir outside the pak pipeline).
4. Document running the game on this machine (asset paths, launch command) in the plan appendix — the port needs a repeatable test loop.

**Exit criteria:** GL renderer builds from the solution unchanged in behavior; golden screenshot set exists; stats overlay works.

---

## Phase 1 — Scaffold: `ref_vulkan` instance → swapchain → clear

**Goal:** a new DLL that opens a window, creates a Vulkan swapchain, clears to a color, and swaps. Engine unchanged except renderer selection.

1. **Project scaffold**
   - Copy `src/ref_gl3/` → `src/ref_vulkan/` (vcxproj + `src/`), output name `ref_vulkan.dll`. Prune GL-only files (`gl3_Glad.c`), keep engine-facing files (`gl3_Main.c` → `rv_Main.c` naming optional, keep names initially to minimize diff).
   - Add to `src/Heretic2R.sln`.
   - Vendor deps: `include/volk/volk.{h,c}`, `include/vk_mem_alloc.h`, `include/shaderc` not needed (D5). All header-only/single-TU — fits existing include layout.
2. **Window/context** (`gl3_SDL.c` equivalent)
   - `RI_PrepareForWindow()`: return `SDL_WINDOW_VULKAN` instead of `SDL_WINDOW_OPENGL` (`gl3_SDL.c:54`); drop GL attribute calls; keep MSAA cvar reading for Phase 6 (D9).
   - `RI_InitContext()`: `SDL_Vulkan_LoadLibrary` → `volkLoadInstance` after instance creation; no context object.
   - `RI_EndFrame()` (`gl3_SDL.c:15`): `vkQueuePresentKHR` instead of `SDL_GL_SwapWindow`.
   - `R_SetVsync()` (`gl3_SDL.c:58`): `VK_PRESENT_MODE_FIFO_KHR` (vsync=1/2) vs `MAILBOX`/`IMMEDIATE` (vsync=0). Adaptive vsync maps to mailbox.
   - Engine touch: verify `glimp_sdl3.c:205` passes window flags from `PrepareForWindow` verbatim (it does — no change needed); ensure `GLimp_InitGraphics` failure path handles a Vulkan-specific error (`ref.h:310`).
3. **Instance/device/swapchain module** — new files:
   - `rv_VkContext.c/h`: instance (validation layers + debug utils when `_DEBUG`), physical device selection (prefer discrete, present+graphics queue family, check `vkGetPhysicalDeviceSurfaceSupportKHR`), device creation with swapchain/maintenance1/dynamic-rendering 1.3 features, queues, allocator (VMA), command pools, sync objects (D7).
   - `rv_Swapchain.c/h`: surface from `SDL_Vulkan_CreateSurface`, format selection (`B8G8R8A8_SRGB` preferred — note: gamma is CPU-side so `UNORM` + manual is equivalent; prefer SRGB for correctness on post chain, decide here), extent on `SDL_EVENT_WINDOW_RESIZED`, recreate-on-outdated handling (invalidate framebuffer-sized images → Phase 5 hook).
   - Debug: `VK_EXT_debug_utils` object names for every image/buffer (golden for RenderDoc triage).
4. **Frame skeleton:** per-frame command buffer: begin → render pass via `vkCmdBeginRendering` clear to magenta → end → submit with acquire/present semaphores → fence wait.
5. **Stub out the rest of the API:** implement every `refexport_t` entry point from `src/client/ref.h:187-274` as no-ops/prints so the DLL loads and the client renders a clear color for menus. `vid_dll.c` must be able to select it: temporarily remove the `Cvar_ForceSet` at `vid_dll.c:339` (keep `:372` default) and build `ref_gl1`/`ref_gl3` alongside.

**Exit criteria:** `set vid_ref vulkan; vid_restart` → black/clear window, no validation errors, resize works, switching back to `gl3` works. Screenshot is a solid color.

**Risk:** device/queue/format selection quirks on Proton — mitigation: keep device selection conservative, log adapter name.

---

## Phase 2 — Resource layer

**Goal:** all resource types exist and can be created/updated/destroyed, with no rendering yet.

1. **`rv_Buffer.c/h`** — wrap VMA buffers with mapping:
   - `RV_CreateBuffer(size, usage, memFlags)`, `RV_DestroyBuffer`, `RV_Map/Unmap`.
   - **Ring buffer** for immediate-mode data (D8): `RV_StagingRing` — persistent-mapped host-visible, per-frame chunks aligned to `minUniformBufferOffsetAlignment`, bumped per draw; reset when the frame fence for that slot signals.
   - **Staging path** for texture upload: dedicated upload pool + one-shot command buffer + fence (or reuse ring if fits).
2. **`rv_Image.c/h`** — replace `gl3_Image.c` texture functions:
   - `R_LoadImage`-equivalent → `RV_CreateImage(w,h,format,mips,usage)` via `vmaCreateImage`.
   - Map formats: RGBA8→`R8G8B8A8_UNORM`, RGBA16F→`R16G16B16A16_SFLOAT`, RGB16F→`R16G16B16A16_SFLOAT` (pad), R16F→`R16_SFLOAT`, DEPTH24→`D32_SFLOAT` or `D24_UNORM_S8_UINT` (need stencil → `D24_UNORM_S8_UINT`), MSAA variants per D9.
   - Partial updates (`glTexSubImage2D` in `gl3_Surface.c:336,417`, `gl3_Lightmap.c:37`) → buffer-to-image copy with `bufferOffset` row alignment checks (`optimalBufferCopyRowPitchAlignment`).
   - Mips: `vkCmdBlitImage` chain at upload (not `vkCmdGenerateMipmap` each bind); anisotropy → sampler `maxAnisotropy`.
   - **Samplers** as a separate cache: linear/clamp, linear/repeat, nearest, anisotropic — GL combined texture+sampler becomes `VkImageView` + `VkSampler` pair; descriptor writes bind both.
   - `R_SelectTexture`/`R_Bind`/`R_MBind` state cache (`gl3_Image.c:101-150`) survives — it now stages descriptor-set bindings instead of `glBindTexture`.
3. **Descriptor infrastructure** — new `rv_Descriptors.c/h`:
   - Per-frame descriptor pool + set layout: set 0 = per-frame UBO (matrices/color/time) + samplers; set 1 = per-material sampled images; push constants for small per-draw data (color, fog, time — replaces `uColor`, `uModelview` where per-draw).
   - Because GL had no UBOs, define **one `FrameUniforms` block** (`uProjection`, `uModelview`, `uTime`, `uColor`, fog params) + **one `DrawUniforms`** push range — this is where the 52 `glGetUniformLocation` sites collapse.
4. **Cinematic path** (`gl3_Draw.c:515-648`): PBO double-buffer → two staging images/buffers + transfer fences; SMK palettized expand stays CPU (`:631-648`); `GL_SWIZZLE_A` trick (`:541`) → component swizzle on `VkImageView` (`components.a = R` etc.).

**Exit criteria:** unit-ish smoke: a test path allocates/frees every resource type; VMA stats logged at shutdown; validation clean under resource churn (load a map, change video modes).

---

## Phase 3 — Pipelines & draw-path port (world + entities)

**Goal:** the actual 3D scene renders in Vulkan: world surfaces, entities, sky, particles.

1. **Shader port (D5)**
   - Extract 10 program sources from `gl3_Shaders.c` into `src/ref_vulkan/shaders/*.vert|frag`, `#version 330` → `#version 450`, explicit `layout(set=b, binding=n)`, `push_constant` block, `layout(location=...)` inputs unchanged.
   - Add `shaders.sh` / MSBuild custom step calling `glslangValidator -V` → `#include` generated `.spv.h` (arrays of uint32). Keep `CompileShader`/`CreateProgram` call-site shape (`gl3_Shaders.c:354,388`) so `GL3_InitShaders` body ports mechanically.
   - Programs: 2D, 3D, 3D-color, 3D-lightmap, post/FSQ-shared vertex, water, bloom-extract, bloom-blur, SSAO, SSAO-blur (creation sites `:434…:1264`).
2. **`rv_Pipeline.c/h`** — pipeline cache (D6):
   - `RV_PipelineKey`: shader ids, vertex layout (`vao2D` 2-float?/`vao3DLM` 7-float/`vao3D` 9-float/`vaoFSQ`), blend mode, depth test/write, cull mode, polygon offset, stencil op (shadows), topology (`TRIANGLE_FAN` → convert fans to strip/list at upload or use `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN` — supported, but requires robustness; safer: fan→list conversion in the ring-buffer writer).
   - Dynamic state: `VK_DYNAMIC_STATE_VIEWPORT`, `SCISSOR`, `DEPTH_BIAS`, `STENCIL_REFERENCE`, `BLEND_CONSTANTS`.
   - `VK_POLYGON_MODE_FILL` default; keep `glPolygonMode` call site (`gl3_Misc.c:56`) as an assert/no-op (only used for wireframe default).
3. **Vertex-upload port** — `gl3_Shaders.c:795-836`, `gl3_Draw.c:116,143`, `gl3_Main.c:1066`:
   - Replace `glBufferData`+`glDrawArrays` with `memcpy` into ring chunk + `vkCmdBindVertexBuffers` + `vkCmdDraw`.
   - `vaoFSQ` (`gl3_Shaders.c:584`) becomes a static device-local buffer with 4 verts; FSQ draws (`:1127…1421`) become bind-pipeline + draw(4).
4. **State call sites** — introduce a tiny explicit state struct written by ported code, applied at draw:
   - `glEnable/glDisable(BLEND|DEPTH_TEST|CULL_FACE|SCISSOR|POLYGON_OFFSET_FILL|STENCIL_TEST)` → fields in `gl3state.rpstate`.
   - `glBlendFunc` (23 sites) → blend factor/op enum in key.
   - `glDepthMask` (22) → `depthWriteEnable` in key.
   - `glDepthRange` (3 sites: `gl3_Main.c:206`, `gl3_FlexModel.c:710,740`) → **Vulkan viewport depth range is always [0,1]** — these sites need real math fixes: either scale `z = z*(f-n)+n` in the shader or change the call sites. Flag: small behavioral port risk.
   - `glPolygonOffset` (`gl3_Shadow.c:331`) → `vkCmdSetDepthBias`.
   - `glPointSize` (`gl3_Sky.c:419,653`) → enable `robustPipelinePointSize`/`shaderViewportIndex` features or draw points with `VK_PRIMITIVE_TOPOLOGY_POINT_LIST` + `pointSize` in VS (Vulkan points are always 1px unless feature used — check driver; fallback: convert point sprites to instanced quads).
   - `glGetBooleanv(GL_CULL_FACE)` readback (`gl3_Sky.c:1146`) → **don't query GPU; mirror the state in `rpstate`** (we know what we set).
   - `glScissor` → `vkCmdSetScissor` at draw time.
5. **File-by-file port order** (dependency order, each compiles + runs):
   1. `gl3_Main.c` frame flow (`RI_BeginFrame:499`, `RI_RenderFrame:1127`, `RI_EndFrame`) — command buffer begin/end, barrier insertion points.
   2. `gl3_Shaders.c` VAO/VBO/program init → pipelines + buffers.
   3. `gl3_Draw.c` 2D + HUD (get the menu drawing first — instant visual feedback).
   4. `gl3_Image.c` texture upload (Phase 2 files land here).
   5. `gl3_Surface.c` / `gl3_Lightmap.c` world geometry + lightmap streaming.
   6. `gl3_Model.c`, `gl3_FlexModel.c`, `gl3_Sky.c`, `gl3_Warp.c` (water), `gl3_Shadow.c`.
   7. `gl3_Light.c`, particles in `gl3_Main.c:1066`, `gl3_Misc.c` screenshot.

**Exit criteria:** world + entities + sky + HUD render correctly vs golden screenshots (post-chain effects still GL-quality? No — SSAO/bloom/HDR come in Phase 5; scene should match un-post-processed expectations). Validation clean in a full map load. Draw stats within ~2x GL frame time (CPU recording overhead expected).

---

## Phase 4 — Offscreen targets & render graph

**Goal:** all 8 GL FBO equivalents exist as Vulkan images with correct load/store semantics and barriers — still without post effects math changes.

1. `rv_Attachments.c/h` replacing FBO creation in `gl3_Shaders.c`:
   - HDR scene color (RGBA16F) + `D24_UNORM_S8_UINT` depth/stencil (`:865-888`).
   - Reflection half-res color + renderbuffer depth (`:943-945`, `gl3state.rboReflectDepth` `gl3_Local.h:326`).
   - Bloom extract + 2× ping-pong RGB16F (`:1048,1054`).
   - SSAO + blur R16F (`:1366`).
   - All created/destroyed with swapchain (tie into Phase 1 resize handling).
2. **Barriers:** since we're single-queue, use `VK_IMAGE_LAYOUT_GENERAL`? **No** — use proper layouts (`COLOR_ATTACHMENT_OPTIMAL`, `SHADER_READ_ONLY_OPTIMAL`) with explicit `vkCmdPipelineBarrier2` transitions between: scene pass → SSAO read depth/color → blur → composite; scene → reflection sample; bloom chain. Encapsulate in `RV_ImageBarrier()` helpers so call sites read like the GL sequence.
3. MSAA (D9): msaa color+depth pair, resolve into `fbo3D` color at end of scene pass, gated on `r_antialiasing`.
4. Port `R_RenderReflection` (`gl3_Main.c:1097`) as a second `vkCmdBeginRendering` inside the same command buffer.

**Exit criteria:** full frame graph recorded and submitted with correct sync; RenderDoc capture shows the same pass order as GL; validation + synchronization validation layer clean.

---

## Phase 5 — Post-processing chain

**Goal:** SSAO, bloom, HDR composite/tonemap, depth fog — pixel-match golden screenshots.

1. Port `GL3_RenderSSAO` (`gl3_Shaders.c:1387`), `GL3_RenderBloom` (`:1134`), `GL3_CompositeHDR` (`:1082`) — logic translates directly since they are already FSQ passes over textures; the FSQ vertex shader is shared (`vertexSourcePost`).
2. Uniform differences: SSAO kernel array + noise → UBO/push constants; `u_dlightPosRad[8]` dynamic light array → push constants (fits in 128-byte push limit? 8×vec4=128 → use the frame UBO instead).
3. Exposure/HDR: confirm output swapchain format choice from Phase 1 (if SRGB swapchain, final composite writes linear; if UNORM, keep GL's behavior). Decide once, document in code.
4. Gamma: stays CPU-side (`gl3_Image.c`), no change — but double-check no double-gamma if SRGB framebuffer chosen (D10 ties to this).

**Exit criteria:** golden screenshot diff ≤ threshold (perceptual; allow minor driver-level filtering differences); A/B toggle `r_ssao/r_bloom` works in both renderers.

---

## Phase 6 — Remaining features & polish

1. **Shadows** (`gl3_Shadow.c`): stencil-ref-per-entity (`:331-347`) → `vkCmdSetStencilReference` (dynamic) + per-key stencil state; polygon offset dynamic.
2. **Planar water reflections** (`gl3_Warp.c`, `shaderWater`): sampling `fboTexReflect` — already covered by Phase 4 layout/barrier; verify UV flip conventions (`GL` vs `Vulkan` Y-flip — **set `VK_KHR_maintenance1` negative viewport height or flip in shader; decide in Phase 1 and apply consistently**; this is the #1 source of upside-down ports).
3. **Cinematics** completion (Phase 2 path): SMK + MP4 decode unchanged; verify overlay blending (`gl3_Draw.c`).
4. **Screenshot**: `RV_Readback` — copy to linear host buffer, wait fence, `ri.Vid_WriteScreenshot` (`gl3_Misc.c:34`); verify PNG matches screen.
5. **Vsync/mode changes**: `R_SetVsync` live-switch (queue present mode change or recreate swapchain), `vid_restart`, fullscreen↔windowed.
6. **Renderer selection UX**: remove remaining force-set at `vid_dll.c:372` (keep default `gl3`), update `FALLBACK_REFLIB` only if Vulkan proves as stable; `VID_InitReflibInfos` (`:300`) already enumerates `ref_*.dll` — verify it picks up `ref_vulkan.dll`.
7. **Packaging**: add `ref_vulkan.dll` to `pack_release.bat`; note Volkan runtime requirements in README.

**Exit criteria:** full game playable start→finish (single player campaign smoke test) on Vulkan; all golden scenes match; video mode switches stable.

---

## Phase 7 — Performance & hardening

1. **Profiling:** timestamp queries (`VK_KHR_calibrated_timestamps` / `vkCmdWriteTimestamp`) around passes; compare with Phase 0 stats vs GL.
2. **CPU recording:** if `vkCmdBindVertexBuffers` per draw dominates, batch contiguous same-pipeline draws (water polys, lightmap polys) — the immediate ring (D8) makes this a memcpy-side merge.
3. **gl3_Jobs.c interaction:** verify no job thread touches Vulkan objects (today they don't — CPU only); document the invariant; if future multithreaded recording, secondary command buffers per job thread.
4. **Sync validation:** enable `VK_LAYER_KHRONOS_sync_validation` full-time in `_DEBUG`; fix findings.
5. **Robustness:** `VK_EXT_graphics_pipeline_library` optional; handle `VK_ERROR_DEVICE_LOST`/out-of-date gracefully (recreate vs `Sys_Error`).
6. **Wine/Proton test pass** (README claims GL3 works there — Vulkan should too, but verify loader discovery via volk/SDL).
7. Delete dead GL scaffolding from `ref_vulkan` (glad includes, `gl3_Glad.c` remnants), rename files `gl3_*` → `rv_*` if desired (pure rename commit, no logic).

**Exit criteria:** Vulkan frame time ≤ GL on reference hardware; zero validation/sync errors in a 30-min play session; Proton smoke test.

---

## Testing strategy (applies throughout)

| Level | Tool | When |
|---|---|---|
| Validation | `VK_LAYER_KHRONOS_validation` + sync validation, `_DEBUG` builds auto-enable | every phase |
| Frame inspection | RenderDoc (capture GL frame in Phase 0 as reference) | Phases 3–5 |
| Visual regression | golden screenshot diff script (Phase 0 set) after each phase | Phases 3–7 |
| A/B | `vid_ref gl3` / `vid_ref vulkan` + `vid_restart`, same demo tick | always |
| Soak | scripted demo playback ×100 loops watching VMA/validation stats | Phase 7 |

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Y-flip (`VK_KHR_maintenance1`) mishandling | upside-down/mirrored output | Decide in Phase 1; test with 2D menu first (text orientation is an obvious tell). |
| `glDepthRange` semantics (3 sites) | entity depth precision off | Port-time math fix + visual check on `gl3_FlexModel.c` entities. |
| No state cache in GL code → missed state | hard-to-trace artifacts | Validation layers + per-draw state asserts in `_DEBUG`. |
| Shader fan topology / point size features | sky/point rendering differs | Convert fans in uploader; quad-instanced points fallback. |
| MSVC-only build + glslang tool dependency | dev friction | `glslangValidator` path configurable in vcxproj; commit generated `.spv.h` as fallback. |
| Swapchain recreate mid-frame resource invalidation | crashes on resize | Phase 1 owns recreate; Phase 4 images hook to same event. |
| Perf regression from per-draw `vkCmdDraw` | CPU-bound frames | Phase 7 batching; ring-buffer design (D8) keeps it cheap. |

## Suggested execution order (summary)

```
Phase 0  baseline + goldens            (small)
Phase 1  scaffold + swapchain + clear   (medium) ← first Vulkan milestone
Phase 2  buffers/images/descriptors     (medium)
Phase 3  pipelines + scene rendering    (large)  ← second milestone: game visible
Phase 4  offscreen targets + barriers   (medium)
Phase 5  post chain (SSAO/bloom/HDR)    (medium) ← third milestone: pixel parity
Phase 6  shadows/reflections/cinema/UI  (medium)
Phase 7  perf + hardening + release     (small-medium)
```

Do not start Phase 3 before Phase 0 goldens and Phase 1 validation-clean swapchain exist — every later phase depends on being able to diff against GL.
