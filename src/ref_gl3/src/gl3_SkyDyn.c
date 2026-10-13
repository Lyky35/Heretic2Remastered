//
// gl3_SkyDyn.c
//
// Dynamic sky + volumetric clouds (Phase 0 scaffold + Phase 1 Tier-A).
// Port plan: DYNAMIC_SKY_PLAN.md — source: fede-vaccaro/TerrainEngine-OpenGL.
//
// Tier A: fullscreen gradient + sun disc, ported from TerrainEngine
// `shaders/sky.frag` (+ `Skybox::DefaultPreset/SunsetPreset1`). Drawn as a
// fullscreen triangle-strip at NDC depth = far plane, so it behaves like the
// legacy camera-centered sky quads (loses to any opaque geometry via LEQUAL).
//
// Phase 1 scope: Tier A only. No volumetric code yet.
//

#include "gl3_SkyDyn.h"
#include "gl3_Shaders.h"
#include "vid.h"

#include <math.h>
#include <string.h>

// Cvar storage. Externs live in gl3_Local.h; definitions live in gl3_Main.c
// (same pattern as r_reflections / r_fog_*).

void GL3_SkyDyn_RegisterCvars(void)
{
	// Master switches (default off -> no visual change).
	r_dynsky = ri.Cvar_Get("r_dynsky", "0", CVAR_ARCHIVE);
	r_volclouds = ri.Cvar_Get("r_volclouds", "0", CVAR_ARCHIVE);

	// Tier A: gradient-sky preset. 0 = day, 1 = sunset, 2 = auto (currently day; sun-cycle in Phase 6).
	r_sky_preset = ri.Cvar_Get("r_sky_preset", "2", CVAR_ARCHIVE);

	// Tier B tuning (TerrainEngine defaults; rescaled later in Phase 4).
	r_cloud_res = ri.Cvar_Get("r_cloud_res", "0.5", CVAR_ARCHIVE); // render scale
	r_cloud_coverage = ri.Cvar_Get("r_cloud_coverage", "0.45", CVAR_ARCHIVE);
	r_cloud_speed = ri.Cvar_Get("r_cloud_speed", "450.0", CVAR_ARCHIVE);
	r_cloud_density = ri.Cvar_Get("r_cloud_density", "0.02", CVAR_ARCHIVE);
	r_cloud_crisp = ri.Cvar_Get("r_cloud_crisp", "40.0", CVAR_ARCHIVE);
	r_cloud_curl = ri.Cvar_Get("r_cloud_curl", "0.1", CVAR_ARCHIVE);
	r_cloud_absorb = ri.Cvar_Get("r_cloud_absorb", "0.35", CVAR_ARCHIVE);
	r_cloud_seed = ri.Cvar_Get("r_cloud_seed", "0", CVAR_ARCHIVE);
	r_cloud_debug = ri.Cvar_Get("r_cloud_debug", "0", 0);
	r_cloud_blur = ri.Cvar_Get("r_cloud_blur", "1", CVAR_ARCHIVE);
	r_cloud_godrays = ri.Cvar_Get("r_cloud_godrays", "0", CVAR_ARCHIVE);
}

// ============================================================
// Tier-A gradient sky program (fullscreen, ported from sky.frag).
// ============================================================

// Fullscreen vertex shader: same 4-float layout as vaoFSQ (pos2 + tc2).
// z = w -> NDC depth 1.0 (far plane), so LEQUAL depth testing keeps every
// opaque surface in front of the sky, like the legacy clipdist quads.
static const char* vertexSourceSkyDyn =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"void main() {\n"
	"    gl_Position = vec4(aPos, 1.0, 1.0);\n"
	"}\n";

// Fragment shader: gradient mix(bottom, top, 1-exp(...)) by view-Y + sun disc,
// i.e. TerrainEngine colorCubeMap() + getSun(d, 350). Ray reconstructed from
// gl_FragCoord via inverse proj/view, exactly like the source.
static const char* fragmentSourceSkyDyn =
	"#version 330 core\n"
	"uniform vec2 uResolution;\n"
	"uniform mat4 uInvProj;\n"
	"uniform mat4 uInvView;\n"
	"uniform vec3 uLightDir;\n"
	"uniform vec3 uSkyTop;\n"
	"uniform vec3 uSkyBottom;\n"
	"out vec4 FragColor;\n"
	"vec3 getSun(vec3 d, float powExp) {\n"
	"    float sun = clamp(dot(normalize(uLightDir), d), 0.0, 1.0);\n"
	"    return 0.8 * vec3(1.0, 0.6, 0.1) * pow(sun, powExp);\n"
	"}\n"
	"void main() {\n"
	"    vec2 ray_nds = 2.0 * gl_FragCoord.xy / uResolution - 1.0;\n"
	"    vec4 ray_clip = vec4(ray_nds, 1.0, 1.0);\n"
	"    vec4 ray_view = uInvProj * ray_clip;\n"
	"    ray_view = vec4(ray_view.xy, -1.0, 0.0);\n"
	"    vec3 worldDir = normalize((uInvView * ray_view).xyz);\n"
	"    vec3 col = mix(uSkyBottom, uSkyTop,\n"
	"        clamp(1.0 - exp(8.5 - 17.0 * clamp(worldDir.y * 0.5 + 0.5, 0.0, 1.0)), 0.0, 1.0));\n"
	"    col += getSun(worldDir, 350.0);\n"
	"    FragColor = vec4(col, 1.0);\n"
	"}\n";

static GLuint s_skyProg = 0;
static GLint s_uResolution = -1;
static GLint s_uInvProj = -1;
static GLint s_uInvView = -1;
static GLint s_uLightDir = -1;
static GLint s_uSkyTop = -1;
static GLint s_uSkyBottom = -1;

static GLuint SkyDyn_CompileShader(GLenum type, const char* source)
{
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char info_log[512];
		glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: sky shader compile error: %s\n", info_log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static qboolean SkyDyn_InitTierA(void)
{
	const GLuint vert = SkyDyn_CompileShader(GL_VERTEX_SHADER, vertexSourceSkyDyn);
	if (vert == 0)
		return false;
	const GLuint frag = SkyDyn_CompileShader(GL_FRAGMENT_SHADER, fragmentSourceSkyDyn);
	if (frag == 0)
	{
		glDeleteShader(vert);
		return false;
	}

	s_skyProg = glCreateProgram();
	glAttachShader(s_skyProg, vert);
	glAttachShader(s_skyProg, frag);
	glLinkProgram(s_skyProg);

	GLint success = 0;
	glGetProgramiv(s_skyProg, GL_LINK_STATUS, &success);
	if (!success)
	{
		char info_log[512];
		glGetProgramInfoLog(s_skyProg, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: sky program link error: %s\n", info_log);
		glDeleteProgram(s_skyProg);
		s_skyProg = 0;
		glDeleteShader(vert);
		glDeleteShader(frag);
		return false;
	}

	glDetachShader(s_skyProg, vert);
	glDetachShader(s_skyProg, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	s_uResolution = glGetUniformLocation(s_skyProg, "uResolution");
	s_uInvProj = glGetUniformLocation(s_skyProg, "uInvProj");
	s_uInvView = glGetUniformLocation(s_skyProg, "uInvView");
	s_uLightDir = glGetUniformLocation(s_skyProg, "uLightDir");
	s_uSkyTop = glGetUniformLocation(s_skyProg, "uSkyTop");
	s_uSkyBottom = glGetUniformLocation(s_skyProg, "uSkyBottom");
	return true;
}

// Column-major 4x4 inverse (GL matrices are column-major, uploaded GL_FALSE).
static qboolean SkyDyn_InvertMatrix(const float* m, float* out)
{
	float inv[16];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
		+ m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
		- m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
		+ m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
		- m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
		- m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
		+ m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
		- m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
		+ m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
		+ m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
		- m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
		+ m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
		- m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
		- m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
		+ m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
		- m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
		+ m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (det == 0.0f)
		return false;
	det = 1.0f / det;
	for (int i = 0; i < 16; i++)
		out[i] = inv[i] * det;
	return true;
}

// TerrainEngine Skybox presets (DefaultPreset / SunsetPreset1), flattened.
static void SkyDyn_Preset(int preset, float* top, float* bottom, float* sunDir)
{
	if (preset == 1) // sunset: low warm sun
	{
		top[0] = 133.0f / 255.0f; top[1] = 158.0f / 255.0f; top[2] = 214.0f / 255.0f;
		bottom[0] = 241.0f / 255.0f; bottom[1] = 161.0f / 255.0f; bottom[2] = 161.0f / 255.0f;
		sunDir[0] = 0.85f; sunDir[1] = 0.14f; sunDir[2] = 0.25f;
	}
	else // day (preset 0 and auto/2, which tracks day until Phase 6 sun-cycle)
	{
		top[0] = 0.5f * 1.05f; top[1] = 0.7f * 1.05f; top[2] = 0.8f * 1.05f;
		bottom[0] = 0.9f; bottom[1] = 0.9f; bottom[2] = 0.95f;
		sunDir[0] = 0.35f; sunDir[1] = 0.75f; sunDir[2] = 0.30f;
	}
	// Normalize sun direction.
	{
		const float len = sqrtf(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] + sunDir[2] * sunDir[2]);
		if (len > 0.0f)
		{
			sunDir[0] /= len; sunDir[1] /= len; sunDir[2] /= len;
		}
	}
}

qboolean GL3_SkyDyn_Init(void)
{
	// Probe compute-shader support. Context is 4.6 Core (gl3_SDL.c), but
	// check at runtime so old drivers fail soft instead of crashing.
	if (!GLAD_GL_VERSION_4_3)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: GL 4.3+ not available (compute missing) — volumetric clouds disabled, gradient sky still available.\n");
	}
	else
	{
		// Minimal #version 430 probe: compile + link only, no dispatch and no
		// GL state touched, so this is safe on every driver. Phase 3+ reuses
		// GL3_CreateComputeProgram for the real noise / cloud shaders.
		static const char* computeProbeSource =
			"#version 430 core\n"
			"layout(local_size_x = 1) in;\n"
			"layout(rgba8, binding = 0) uniform image2D outImg;\n"
			"void main() {\n"
			"    imageStore(outImg, ivec2(0, 0), vec4(1.0, 0.0, 1.0, 1.0));\n"
			"}\n";
		const GLuint probe = GL3_CreateComputeProgram(computeProbeSource);
		if (probe == 0)
			ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: compute probe FAILED — volumetric clouds disabled, gradient sky still available.\n");
		else
		{
			ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: compute probe OK (GL 4.3+ program path works).\n");
			glDeleteProgram(probe);
		}
	}

	if (!SkyDyn_InitTierA())
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: Tier-A gradient sky unavailable (shader failed) — falling back to legacy sky.\n");
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: Tier-A gradient sky ready. r_dynsky/r_volclouds default off — no visual change.\n");
	}

	// Phase 2+ will allocate noise textures and cloud FBOs here.
	return true;
}

void GL3_SkyDyn_Shutdown(void)
{
	if (s_skyProg != 0)
	{
		glDeleteProgram(s_skyProg);
		s_skyProg = 0;
	}
	s_uResolution = s_uInvProj = s_uInvView = s_uLightDir = s_uSkyTop = s_uSkyBottom = -1;
	// Phase 2+ will delete noise textures / FBOs here.
}

qboolean GL3_SkyDyn_Enabled(void)
{
	return (r_dynsky != NULL && (int)r_dynsky->value != 0) ? true : false;
}

qboolean GL3_SkyDyn_VolumetricEnabled(void)
{
	if (r_volclouds == NULL || (int)r_volclouds->value == 0)
		return false;
	if (!GLAD_GL_VERSION_4_3)
		return false;
	return GL3_SkyDyn_Enabled();
}

qboolean GL3_SkyDyn_TierAReady(void)
{
	return (s_skyProg != 0) ? true : false;
}

void GL3_SkyDyn_DrawTierA(void)
{
	float top[3], bottom[3], sunDir[3];
	float invProj[16], invView[16];
	float width, height;
	int preset;

	if (s_skyProg == 0)
		return;

	preset = (r_sky_preset != NULL) ? (int)r_sky_preset->value : 2;
	if (preset < 0 || preset > 2)
		preset = 2;
	if (preset == 2)
		preset = 0; // auto tracks day until Phase 6 sun-cycle
	SkyDyn_Preset(preset, top, bottom, sunDir);

	if (!SkyDyn_InvertMatrix(r_projection_matrix, invProj))
		return;
	if (!SkyDyn_InvertMatrix(r_world_matrix, invView))
		return;

	// FBO size (gl_FragCoord is in framebuffer pixels during the world pass).
	width = (gl3state.fbo_width > 0) ? (float)gl3state.fbo_width : (float)viddef.width;
	height = (gl3state.fbo_height > 0) ? (float)gl3state.fbo_height : (float)viddef.height;

	GL3_UseShader(s_skyProg);
	glUniform2f(s_uResolution, width, height);
	glUniformMatrix4fv(s_uInvProj, 1, GL_FALSE, invProj);
	glUniformMatrix4fv(s_uInvView, 1, GL_FALSE, invView);
	glUniform3f(s_uLightDir, sunDir[0], sunDir[1], sunDir[2]);
	glUniform3f(s_uSkyTop, top[0], top[1], top[2]);
	glUniform3f(s_uSkyBottom, bottom[0], bottom[1], bottom[2]);

	// Caller keeps depth test on + writes off + cull/clip off; FSQ at far
	// depth loses to all opaque geometry via LEQUAL, like legacy sky quads.
	glBindVertexArray(gl3state.vaoFSQ);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
