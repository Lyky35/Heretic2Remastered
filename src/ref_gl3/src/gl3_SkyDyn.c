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
#include "gl3_Surface.h"
#include "vid.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <string.h>

// Cvar storage. Externs live in gl3_Local.h; definitions live in gl3_Main.c
// (same pattern as r_reflections / r_fog_*).

void GL3_SkyDyn_RegisterCvars(void)
{
	// Master switches (default off -> no visual change).
	r_dynsky = ri.Cvar_Get("r_dynsky", "0", CVAR_ARCHIVE);
	r_volclouds = ri.Cvar_Get("r_volclouds", "0", CVAR_ARCHIVE);

	// Tier A: gradient-sky preset. 0 day, 1 sunset, 2 auto (day), 3 night, 4 morning, 5 evening.
	r_sky_preset = ri.Cvar_Get("r_sky_preset", "2", CVAR_ARCHIVE);
	// Per-level auto look (table in SkyDyn_ApplyLevelSky, applied on map change).
	r_sky_levels = ri.Cvar_Get("r_sky_levels", "1", CVAR_ARCHIVE);

	// Tier B tuning (TerrainEngine defaults; rescaled later in Phase 4).
	r_cloud_res = ri.Cvar_Get("r_cloud_res", "1", CVAR_ARCHIVE); // render scale
	r_cloud_coverage = ri.Cvar_Get("r_cloud_coverage", "0.45", CVAR_ARCHIVE);
	r_cloud_speed = ri.Cvar_Get("r_cloud_speed", "450.0", CVAR_ARCHIVE);
	r_cloud_density = ri.Cvar_Get("r_cloud_density", "0.02", CVAR_ARCHIVE);
	r_cloud_crisp = ri.Cvar_Get("r_cloud_crisp", "40.0", CVAR_ARCHIVE);
	r_cloud_curl = ri.Cvar_Get("r_cloud_curl", "0.1", CVAR_ARCHIVE);
	r_cloud_absorb = ri.Cvar_Get("r_cloud_absorb", "0.35", CVAR_ARCHIVE);
	r_cloud_seed = ri.Cvar_Get("r_cloud_seed", "0", CVAR_ARCHIVE);
	r_cloud_freq = ri.Cvar_Get("r_cloud_freq", "0.8", CVAR_ARCHIVE);
	r_cloud_base = ri.Cvar_Get("r_cloud_base", "3500.0", CVAR_ARCHIVE);
	r_cloud_thick = ri.Cvar_Get("r_cloud_thick", "2500.0", CVAR_ARCHIVE);
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

// Fragment shader: gradient mix(bottom, top, 1-exp(...)) by view-Z (H2 is Z-up)
// + sun disc, i.e. TerrainEngine colorCubeMap() + getSun(d, 350) adapted from
// Y-up to Z-up. Ray reconstructed from gl_FragCoord via inverse proj/view,
// exactly like the source.
static const char* fragmentSourceSkyDyn =
	"#version 330 core\n"
	"uniform vec2 uResolution;\n"
	"uniform mat4 uInvProj;\n"
	"uniform mat4 uInvView;\n"
	"uniform vec3 uLightDir;\n"
	"uniform vec3 uSkyTop;\n"
	"uniform vec3 uSkyBottom;\n"
	"uniform vec3 uSunColor;\n"   // pre-scaled sun disc color (black = off)
	"uniform vec3 uMoonDir;\n"    // H2 Z-up moon direction
	"uniform vec3 uMoonColor;\n"  // pre-scaled moon color (black = off)
	"uniform float uNight;\n"     // 0..1 star-field visibility
	"uniform float uTime;\n"      // seconds, star twinkle
	"out vec4 FragColor;\n"
	"float starLayer(vec3 d, float scale, float t) {\n"
	"    vec3 p = d * scale;\n"
	"    vec3 id = floor(p);\n"
	"    vec3 f = fract(p) - 0.5;\n"
	"    float h = fract(sin(dot(id, vec3(12.9898, 78.233, 45.164))) * 43758.5453);\n"
	"    float star = step(0.78, h) * smoothstep(0.22, 0.04, length(f));\n"
	"    float twinkle = 0.7 + 0.3 * sin(t * (2.0 + h * 4.0) + h * 40.0);\n"
	"    return star * twinkle * (0.4 + 0.6 * h);\n"
	"}\n"
	"void main() {\n"
	"    vec2 ray_nds = 2.0 * gl_FragCoord.xy / uResolution - 1.0;\n"
	"    vec4 ray_clip = vec4(ray_nds, 1.0, 1.0);\n"
	"    vec4 ray_view = uInvProj * ray_clip;\n"
	"    ray_view = vec4(ray_view.xy, -1.0, 0.0);\n"
	"    vec3 worldDir = normalize((uInvView * ray_view).xyz);\n"
	"    vec3 col = mix(uSkyBottom, uSkyTop,\n"
	"        clamp(1.0 - exp(8.5 - 17.0 * clamp(worldDir.z * 0.5 + 0.5, 0.0, 1.0)), 0.0, 1.0));\n"
	"    vec3 d = normalize(worldDir);\n"
	"    col += uSunColor * pow(clamp(dot(normalize(uLightDir), d), 0.0, 1.0), 350.0);\n"
	"    float mdot = clamp(dot(normalize(uMoonDir), d), 0.0, 1.0);\n"
	"    col += uMoonColor * pow(mdot, 1200.0);\n"
	"    col += uMoonColor * 0.25 * pow(mdot, 24.0);\n"
	"    if (uNight > 0.001) {\n"
	"        float horiz = smoothstep(-0.05, 0.25, d.z);\n"
	"        float stars = starLayer(d, 200.0, uTime) + 0.6 * starLayer(d + 11.7, 90.0, uTime);\n"
	"        col += uNight * horiz * vec3(0.9, 0.95, 1.0) * stars;\n"
	"    }\n"
	"    FragColor = vec4(col, 1.0);\n"
	"}\n";

static GLuint s_skyProg = 0;
static GLint s_uResolution = -1;
static GLint s_uInvProj = -1;
static GLint s_uInvView = -1;
static GLint s_uLightDir = -1;
static GLint s_uSkyTop = -1;
static GLint s_uSkyBottom = -1;
static GLint s_uSunColor = -1;
static GLint s_uMoonDir = -1;
static GLint s_uMoonColor = -1;
static GLint s_uNight = -1;
static GLint s_uTime = -1;

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
	s_uSunColor = glGetUniformLocation(s_skyProg, "uSunColor");
	s_uMoonDir = glGetUniformLocation(s_skyProg, "uMoonDir");
	s_uMoonColor = glGetUniformLocation(s_skyProg, "uMoonColor");
	s_uNight = glGetUniformLocation(s_skyProg, "uNight");
	s_uTime = glGetUniformLocation(s_skyProg, "uTime");
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

// TerrainEngine Skybox presets (DefaultPreset / SunsetPreset1) + night,
// flattened. Canonical form is Y-up (high sunDir.y = overhead): Tier-B
// consumes it as-is (its shader converts rays to Y-up); Tier-A swizzles
// sun/moon dirs to H2 Z-up at upload.
static void SkyDyn_Preset(int preset, float* top, float* bottom, float* sunDir,
	float* sunColor, float* moonDir, float* moonColor, float* night)
{
	if (preset == 3) // night: dark sky, moon disc + stars, dim blue clouds
	{
		top[0] = 0.012f; top[1] = 0.025f; top[2] = 0.07f;
		bottom[0] = 0.045f; bottom[1] = 0.07f; bottom[2] = 0.14f;
		sunDir[0] = 0.35f; sunDir[1] = -0.5f; sunDir[2] = 0.30f; // below horizon
		sunColor[0] = 0.0f; sunColor[1] = 0.0f; sunColor[2] = 0.0f;
		moonDir[0] = -0.45f; moonDir[1] = 0.62f; moonDir[2] = -0.30f;
		moonColor[0] = 0.95f; moonColor[1] = 1.0f; moonColor[2] = 1.15f;
		*night = 1.0f;
	}
	else if (preset == 1) // sunset: low warm sun
	{
		top[0] = 133.0f / 255.0f; top[1] = 158.0f / 255.0f; top[2] = 214.0f / 255.0f;
		bottom[0] = 241.0f / 255.0f; bottom[1] = 161.0f / 255.0f; bottom[2] = 161.0f / 255.0f;
		sunDir[0] = 0.85f; sunDir[1] = 0.14f; sunDir[2] = 0.25f;
		sunColor[0] = 1.0f; sunColor[1] = 0.5f; sunColor[2] = 0.15f;
		moonDir[0] = 0.0f; moonDir[1] = -1.0f; moonDir[2] = 0.0f;
		moonColor[0] = 0.0f; moonColor[1] = 0.0f; moonColor[2] = 0.0f;
		*night = 0.2f;
	}
	else if (preset == 4) // morning: low pale-gold sun, cool blue dome
	{
		top[0] = 0.52f; top[1] = 0.68f; top[2] = 0.86f;
		bottom[0] = 1.0f; bottom[1] = 0.88f; bottom[2] = 0.72f;
		sunDir[0] = 0.9f; sunDir[1] = 0.22f; sunDir[2] = 0.15f;
		sunColor[0] = 0.95f; sunColor[1] = 0.8f; sunColor[2] = 0.6f;
		moonDir[0] = 0.0f; moonDir[1] = -1.0f; moonDir[2] = 0.0f;
		moonColor[0] = 0.0f; moonColor[1] = 0.0f; moonColor[2] = 0.0f;
		*night = 0.0f;
	}
	else if (preset == 5) // evening: dusk purple-blue dome, deep orange low sun
	{
		top[0] = 0.30f; top[1] = 0.26f; top[2] = 0.48f;
		bottom[0] = 0.95f; bottom[1] = 0.50f; bottom[2] = 0.30f;
		sunDir[0] = -0.85f; sunDir[1] = 0.12f; sunDir[2] = 0.20f;
		sunColor[0] = 1.0f; sunColor[1] = 0.55f; sunColor[2] = 0.25f;
		moonDir[0] = 0.0f; moonDir[1] = -1.0f; moonDir[2] = 0.0f;
		moonColor[0] = 0.0f; moonColor[1] = 0.0f; moonColor[2] = 0.0f;
		*night = 0.3f;
	}
	else // day (preset 0 and auto/2, which tracks day until Phase 6 sun-cycle)
	{
		top[0] = 0.5f * 1.05f; top[1] = 0.7f * 1.05f; top[2] = 0.8f * 1.05f;
		bottom[0] = 0.9f; bottom[1] = 0.9f; bottom[2] = 0.95f;
		sunDir[0] = 0.35f; sunDir[1] = 0.75f; sunDir[2] = 0.30f;
		sunColor[0] = 0.8f; sunColor[1] = 0.48f; sunColor[2] = 0.08f;
		moonDir[0] = 0.0f; moonDir[1] = -1.0f; moonDir[2] = 0.0f;
		moonColor[0] = 0.0f; moonColor[1] = 0.0f; moonColor[2] = 0.0f;
		*night = 0.0f;
	}
	// Normalize directions.
	{
		float len = sqrtf(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] + sunDir[2] * sunDir[2]);
		if (len > 0.0f)
		{
			sunDir[0] /= len; sunDir[1] /= len; sunDir[2] /= len;
		}
		len = sqrtf(moonDir[0] * moonDir[0] + moonDir[1] * moonDir[1] + moonDir[2] * moonDir[2]);
		if (len > 0.0f)
		{
			moonDir[0] /= len; moonDir[1] /= len; moonDir[2] /= len;
		}
	}
}

// ============================================================
// Phase 3: cloud noise generation (one-time cost) + debug view.
//
// Sources (MIT, (c) 2018 Federico Vaccaro — fede-vaccaro/TerrainEngine-OpenGL):
// perlinworley.comp (128^3 base shapes), worley.comp (32^3 erosion detail),
// weather.comp (1024^2 coverage/type). The 3D shaders also incorporate Sebastien
// Hillarie's tileable volume noise and GLM 4D Perlin (see original headers),
// plus NadirRoGue's weather-map code. Embedded verbatim; uniform/image names
// unchanged (outVolTex / outWeatherTex binding 0, seed, perlinFrequency).
// ============================================================

#define SKYDYN_PERLIN_SIZE 128
#define SKYDYN_WORLEY_SIZE 32
#define SKYDYN_WEATHER_SIZE 1024

static const char* computeSourcePerlinWorley = 
"#version 430\n"
"\n"
"/*\n"
"	Fills a 3D texture with PERLIN-WORLEY and WORLEY noises to be used\n"
"	in the volumetric clouds to gather the base shape\n"
"*/\n"
"\n"
"layout (local_size_x = 4, local_size_y = 4, local_size_z = 4) in;\n"
"\n"
"layout (rgba8, binding = 0) uniform image3D outVolTex;\n"
"\n"
"// =====================================================================================\n"
"// Code from Sebastien Hillarie 3d noise generator https://github.com/sebh/TileableVolumeNoise\n"
"uniform float frequenceMul[6u] = float[]( 2.0,8.0,14.0,20.0,26.0,32.0 );\n"
"\n"
"//Code from https://github.com/NadirRoGue\n"
"//Special thanks https://github.com/NadirRoGue\n"
"\n"
"float hash(int n)\n"
"{\n"
"	return fract(sin(float(n) + 1.951) * 43758.5453123);\n"
"}\n"
"\n"
"float noise(vec3 x)\n"
"{\n"
"	vec3 p = floor(x);\n"
"	vec3 f = fract(x);\n"
"\n"
"	f = f*f*(vec3(3.0) - vec3(2.0) * f);\n"
"	float n = p.x + p.y*57.0 + 113.0*p.z;\n"
"	return mix(\n"
"		mix(\n"
"			mix(hash(int(n + 0.0)), hash(int(n + 1.0)), f.x),\n"
"			mix(hash(int(n + 57.0)), hash(int(n + 58.0)), f.x),\n"
"			f.y),\n"
"		mix(\n"
"			mix(hash(int(n + 113.0)), hash(int(n + 114.0)), f.x),\n"
"			mix(hash(int(n + 170.0)), hash(int(n + 171.0)), f.x),\n"
"			f.y),\n"
"		f.z);\n"
"}\n"
"\n"
"float cells(vec3 p, float cellCount)\n"
"{\n"
"	vec3 pCell = p * cellCount;\n"
"	float d = 1.0e10;\n"
"	for (int xo = -1; xo <= 1; xo++)\n"
"	{\n"
"		for (int yo = -1; yo <= 1; yo++)\n"
"		{\n"
"			for (int zo = -1; zo <= 1; zo++)\n"
"			{\n"
"				vec3 tp = floor(pCell) + vec3(xo, yo, zo);\n"
"\n"
"				tp = pCell - tp - noise(mod(tp, cellCount / 1.0));\n"
"\n"
"				d = min(d, dot(tp, tp));\n"
"			}\n"
"		}\n"
"	}\n"
"	d = min(d, 1.0);\n"
"	d = max(d, 0.0f);\n"
"\n"
"	return d;\n"
"}\n"
"\n"
"\n"
"// From GLM (gtc/noise.hpp & detail/_noise.hpp)\n"
"vec4 mod289(vec4 x)\n"
"{\n"
"	return x - floor(x * vec4(1.0) / vec4(289.0)) * vec4(289.0);\n"
"}\n"
"\n"
"vec4 permute(vec4 x)\n"
"{\n"
"	return mod289(((x * 34.0) + 1.0) * x);\n"
"}\n"
"\n"
"vec4 taylorInvSqrt(vec4 r)\n"
"{\n"
"	return vec4(1.79284291400159) - vec4(0.85373472095314) * r;\n"
"}\n"
"\n"
"vec4 fade(vec4 t)\n"
"{\n"
"	return (t * t * t) * (t * (t * vec4(6) - vec4(15)) + vec4(10));\n"
"}\n"
"\n"
"float glmPerlin4D(vec4 Position, vec4 rep)\n"
"{\n"
"		vec4 Pi0 = mod(floor(Position), rep);	// Integer part for indexing\n"
"		vec4 Pi1 = mod(Pi0 + vec4(1), rep);		// Integer part + 1\n"
"		//Pi0 = mod(Pi0, vec4(289));\n"
"		//Pi1 = mod(Pi1, vec4(289));\n"
"		vec4 Pf0 = fract(Position);	// Fractional part for interpolation\n"
"		vec4 Pf1 = Pf0 - vec4(1);		// Fractional part - 1.0\n"
"		vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);\n"
"		vec4 iy = vec4(Pi0.y, Pi0.y, Pi1.y, Pi1.y);\n"
"		vec4 iz0 = vec4(Pi0.z);\n"
"		vec4 iz1 = vec4(Pi1.z);\n"
"		vec4 iw0 = vec4(Pi0.w);\n"
"		vec4 iw1 = vec4(Pi1.w);\n"
"\n"
"		vec4 ixy = permute(permute(ix) + iy);\n"
"		vec4 ixy0 = permute(ixy + iz0);\n"
"		vec4 ixy1 = permute(ixy + iz1);\n"
"		vec4 ixy00 = permute(ixy0 + iw0);\n"
"		vec4 ixy01 = permute(ixy0 + iw1);\n"
"		vec4 ixy10 = permute(ixy1 + iw0);\n"
"		vec4 ixy11 = permute(ixy1 + iw1);\n"
"\n"
"		vec4 gx00 = ixy00 / vec4(7);\n"
"		vec4 gy00 = floor(gx00) / vec4(7);\n"
"		vec4 gz00 = floor(gy00) / vec4(6);\n"
"		gx00 = fract(gx00) - vec4(0.5);\n"
"		gy00 = fract(gy00) - vec4(0.5);\n"
"		gz00 = fract(gz00) - vec4(0.5);\n"
"		vec4 gw00 = vec4(0.75) - abs(gx00) - abs(gy00) - abs(gz00);\n"
"		vec4 sw00 = step(gw00, vec4(0.0));\n"
"		gx00 -= sw00 * (step(vec4(0), gx00) - vec4(0.5));\n"
"		gy00 -= sw00 * (step(vec4(0), gy00) - vec4(0.5));\n"
"\n"
"		vec4 gx01 = ixy01 / vec4(7);\n"
"		vec4 gy01 = floor(gx01) / vec4(7);\n"
"		vec4 gz01 = floor(gy01) / vec4(6);\n"
"		gx01 = fract(gx01) - vec4(0.5);\n"
"		gy01 = fract(gy01) - vec4(0.5);\n"
"		gz01 = fract(gz01) - vec4(0.5);\n"
"		vec4 gw01 = vec4(0.75) - abs(gx01) - abs(gy01) - abs(gz01);\n"
"		vec4 sw01 = step(gw01, vec4(0.0));\n"
"		gx01 -= sw01 * (step(vec4(0), gx01) - vec4(0.5));\n"
"		gy01 -= sw01 * (step(vec4(0), gy01) - vec4(0.5));\n"
"\n"
"		vec4 gx10 = ixy10 / vec4(7);\n"
"		vec4 gy10 = floor(gx10) / vec4(7);\n"
"		vec4 gz10 = floor(gy10) / vec4(6);\n"
"		gx10 = fract(gx10) - vec4(0.5);\n"
"		gy10 = fract(gy10) - vec4(0.5);\n"
"		gz10 = fract(gz10) - vec4(0.5);\n"
"		vec4 gw10 = vec4(0.75) - abs(gx10) - abs(gy10) - abs(gz10);\n"
"		vec4 sw10 = step(gw10, vec4(0));\n"
"		gx10 -= sw10 * (step(vec4(0), gx10) - vec4(0.5));\n"
"		gy10 -= sw10 * (step(vec4(0), gy10) - vec4(0.5));\n"
"\n"
"		vec4 gx11 = ixy11 / vec4(7);\n"
"		vec4 gy11 = floor(gx11) / vec4(7);\n"
"		vec4 gz11 = floor(gy11) / vec4(6);\n"
"		gx11 = fract(gx11) - vec4(0.5);\n"
"		gy11 = fract(gy11) - vec4(0.5);\n"
"		gz11 = fract(gz11) - vec4(0.5);\n"
"		vec4 gw11 = vec4(0.75) - abs(gx11) - abs(gy11) - abs(gz11);\n"
"		vec4 sw11 = step(gw11, vec4(0.0));\n"
"		gx11 -= sw11 * (step(vec4(0), gx11) - vec4(0.5));\n"
"		gy11 -= sw11 * (step(vec4(0), gy11) - vec4(0.5));\n"
"\n"
"		vec4 g0000 = vec4(gx00.x, gy00.x, gz00.x, gw00.x);\n"
"		vec4 g1000 = vec4(gx00.y, gy00.y, gz00.y, gw00.y);\n"
"		vec4 g0100 = vec4(gx00.z, gy00.z, gz00.z, gw00.z);\n"
"		vec4 g1100 = vec4(gx00.w, gy00.w, gz00.w, gw00.w);\n"
"		vec4 g0010 = vec4(gx10.x, gy10.x, gz10.x, gw10.x);\n"
"		vec4 g1010 = vec4(gx10.y, gy10.y, gz10.y, gw10.y);\n"
"		vec4 g0110 = vec4(gx10.z, gy10.z, gz10.z, gw10.z);\n"
"		vec4 g1110 = vec4(gx10.w, gy10.w, gz10.w, gw10.w);\n"
"		vec4 g0001 = vec4(gx01.x, gy01.x, gz01.x, gw01.x);\n"
"		vec4 g1001 = vec4(gx01.y, gy01.y, gz01.y, gw01.y);\n"
"		vec4 g0101 = vec4(gx01.z, gy01.z, gz01.z, gw01.z);\n"
"		vec4 g1101 = vec4(gx01.w, gy01.w, gz01.w, gw01.w);\n"
"		vec4 g0011 = vec4(gx11.x, gy11.x, gz11.x, gw11.x);\n"
"		vec4 g1011 = vec4(gx11.y, gy11.y, gz11.y, gw11.y);\n"
"		vec4 g0111 = vec4(gx11.z, gy11.z, gz11.z, gw11.z);\n"
"		vec4 g1111 = vec4(gx11.w, gy11.w, gz11.w, gw11.w);\n"
"\n"
"		vec4 norm00 = taylorInvSqrt(vec4(dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)));\n"
"		g0000 *= norm00.x;\n"
"		g0100 *= norm00.y;\n"
"		g1000 *= norm00.z;\n"
"		g1100 *= norm00.w;\n"
"\n"
"		vec4 norm01 = taylorInvSqrt(vec4(dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)));\n"
"		g0001 *= norm01.x;\n"
"		g0101 *= norm01.y;\n"
"		g1001 *= norm01.z;\n"
"		g1101 *= norm01.w;\n"
"\n"
"		vec4 norm10 = taylorInvSqrt(vec4(dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)));\n"
"		g0010 *= norm10.x;\n"
"		g0110 *= norm10.y;\n"
"		g1010 *= norm10.z;\n"
"		g1110 *= norm10.w;\n"
"\n"
"		vec4 norm11 = taylorInvSqrt(vec4(dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)));\n"
"		g0011 *= norm11.x;\n"
"		g0111 *= norm11.y;\n"
"		g1011 *= norm11.z;\n"
"		g1111 *= norm11.w;\n"
"\n"
"		float n0000 = dot(g0000, Pf0);\n"
"		float n1000 = dot(g1000, vec4(Pf1.x, Pf0.y, Pf0.z, Pf0.w));\n"
"		float n0100 = dot(g0100, vec4(Pf0.x, Pf1.y, Pf0.z, Pf0.w));\n"
"		float n1100 = dot(g1100, vec4(Pf1.x, Pf1.y, Pf0.z, Pf0.w));\n"
"		float n0010 = dot(g0010, vec4(Pf0.x, Pf0.y, Pf1.z, Pf0.w));\n"
"		float n1010 = dot(g1010, vec4(Pf1.x, Pf0.y, Pf1.z, Pf0.w));\n"
"		float n0110 = dot(g0110, vec4(Pf0.x, Pf1.y, Pf1.z, Pf0.w));\n"
"		float n1110 = dot(g1110, vec4(Pf1.x, Pf1.y, Pf1.z, Pf0.w));\n"
"		float n0001 = dot(g0001, vec4(Pf0.x, Pf0.y, Pf0.z, Pf1.w));\n"
"		float n1001 = dot(g1001, vec4(Pf1.x, Pf0.y, Pf0.z, Pf1.w));\n"
"		float n0101 = dot(g0101, vec4(Pf0.x, Pf1.y, Pf0.z, Pf1.w));\n"
"		float n1101 = dot(g1101, vec4(Pf1.x, Pf1.y, Pf0.z, Pf1.w));\n"
"		float n0011 = dot(g0011, vec4(Pf0.x, Pf0.y, Pf1.z, Pf1.w));\n"
"		float n1011 = dot(g1011, vec4(Pf1.x, Pf0.y, Pf1.z, Pf1.w));\n"
"		float n0111 = dot(g0111, vec4(Pf0.x, Pf1.y, Pf1.z, Pf1.w));\n"
"		float n1111 = dot(g1111, Pf1);\n"
"\n"
"		vec4 fade_xyzw = fade(Pf0);\n"
"		vec4 n_0w = mix(vec4(n0000, n1000, n0100, n1100), vec4(n0001, n1001, n0101, n1101), fade_xyzw.w);\n"
"		vec4 n_1w = mix(vec4(n0010, n1010, n0110, n1110), vec4(n0011, n1011, n0111, n1111), fade_xyzw.w);\n"
"		vec4 n_zw = mix(n_0w, n_1w, fade_xyzw.z);\n"
"		vec2 n_yzw = mix(vec2(n_zw.x, n_zw.y), vec2(n_zw.z, n_zw.w), fade_xyzw.y);\n"
"		float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);\n"
"		return float(2.2) * n_xyzw;\n"
"}\n"
"\n"
"float remap(float originalValue, float originalMin, float originalMax, float newMin, float newMax)\n"
"{\n"
"	return newMin + (((originalValue - originalMin) / (originalMax - originalMin)) * (newMax - newMin));\n"
"}\n"
"\n"
"// ======================================================================\n"
"\n"
"float worleyNoise3D(vec3 p, float cellCount)\n"
"{\n"
"	return cells(p, cellCount);\n"
"}\n"
"\n"
"float perlinNoise3D(vec3 pIn, float frequency, int octaveCount)\n"
"{\n"
"	float octaveFrenquencyFactor = 2.0;			// noise frequency factor between octave, forced to 2\n"
"\n"
"	// Compute the sum for each octave\n"
"	float sum = 0.0f;\n"
"	float weightSum = 0.0f;\n"
"	float weight = 0.5f;\n"
"	for (int oct = 0; oct < octaveCount; oct++)\n"
"	{\n"
"		// Perlin vec3 is bugged in GLM on the Z axis :(, black stripes are visible\n"
"		// So instead we use 4d Perlin and only use xyz...\n"
"		//glm::vec3 p(x * freq, y * freq, z * freq);\n"
"		//float val = glm::perlin(p, glm::vec3(freq)) *0.5 + 0.5;\n"
"\n"
"		vec4 p = vec4(pIn.x, pIn.y, pIn.z, 0.0) * vec4(frequency);\n"
"		float val = glmPerlin4D(p, vec4(frequency));\n"
"\n"
"		sum += val * weight;\n"
"		weightSum += weight;\n"
"\n"
"		weight *= weight;\n"
"		frequency *= octaveFrenquencyFactor;\n"
"	}\n"
"\n"
"	float noise = (sum / weightSum);// *0.5 + 0.5;;\n"
"	noise = min(noise, 1.0f);\n"
"	noise = max(noise, 0.0f);\n"
"	return noise;\n"
"}\n"
"\n"
"vec4 stackable3DNoise(ivec3 pixel)\n"
"{\n"
"	vec3 coord = vec3(float(pixel.x) / 128.0, float(pixel.y) / 128.0, float(pixel.z) / 128.0);\n"
"\n"
"	// Perlin FBM noise\n"
"	int octaveCount = 3;\n"
"	float frequency = 8.0;\n"
"	float perlinNoise = perlinNoise3D(coord, frequency, octaveCount);\n"
"\n"
"	float PerlinWorleyNoise = 0.0f;\n"
"	{\n"
"		float cellCount = 4.0;\n"
"		float worleyNoise0 = (1.0 - worleyNoise3D(coord, cellCount * frequenceMul[0]));\n"
"		float worleyNoise1 = (1.0 - worleyNoise3D(coord, cellCount * frequenceMul[1]));\n"
"		float worleyNoise2 = (1.0 - worleyNoise3D(coord, cellCount * frequenceMul[2]));\n"
"		float worleyNoise3 = (1.0 - worleyNoise3D(coord, cellCount * frequenceMul[3]));\n"
"		float worleyNoise4 = (1.0 - worleyNoise3D(coord, cellCount * frequenceMul[4]));\n"
"		float worleyNoise5 = (1.0 - worleyNoise3D(coord, cellCount * frequenceMul[5]));	// half the frequency of texel, we should not go further (with cellCount = 32 and texture size = 64)\n"
"\n"
"		// PerlinWorley noise as described p.101 of GPU Pro 7\n"
"		float worleyFBM = worleyNoise0*0.625f + worleyNoise1*0.25f + worleyNoise2*0.125f;\n"
"\n"
"		PerlinWorleyNoise = remap(perlinNoise, 0.0, 1.0, worleyFBM, 1.0);\n"
"	}\n"
"\n"
"	float cellCount = 4.0;\n"
"	float worleyNoise0 = (1.0 - worleyNoise3D(coord, cellCount * 1.0));\n"
"	float worleyNoise1 = (1.0 - worleyNoise3D(coord, cellCount * 2.0));\n"
"	float worleyNoise2 = (1.0 - worleyNoise3D(coord, cellCount * 4.0));\n"
"	float worleyNoise3 = (1.0 - worleyNoise3D(coord, cellCount * 8.0));\n"
"	float worleyNoise4 = (1.0 - worleyNoise3D(coord, cellCount * 16.0));\n"
"	//float worleyNoise5 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 32));	\n"
"	//cellCount=2 -> half the frequency of texel, we should not go further (with cellCount = 32 and texture size = 64)\n"
"\n"
"	// Three frequency of Worley FBM noise\n"
"	float worleyFBM0 = worleyNoise1*0.625f + worleyNoise2*0.25f + worleyNoise3*0.125f;\n"
"	float worleyFBM1 = worleyNoise2*0.625f + worleyNoise3*0.25f + worleyNoise4*0.125f;\n"
"	//float worleyFBM2 = worleyNoise3*0.625f + worleyNoise4*0.25f + worleyNoise5*0.125f;\n"
"	float worleyFBM2 = worleyNoise3*0.75f + worleyNoise4*0.25f; \n"
"	// cellCount=4 -> worleyNoise5 is just noise due to sampling frequency=texel frequency. So only take into account 2 frequencies for FBM\n"
"\n"
"	return vec4(PerlinWorleyNoise * PerlinWorleyNoise, worleyFBM0, worleyFBM1, worleyFBM2);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    ivec3 pixel = ivec3(gl_GlobalInvocationID.xyz);\n"
"\n"
"	imageStore (outVolTex, pixel, stackable3DNoise(pixel));\n"
"}\n";
static const char* computeSourceWorley = 
"#version 430\n"
"\n"
"/*\n"
"	Fills a 3D texture with WORLEY noise to be used in the volumetric\n"
"	clouds as shape eroder\n"
"*/\n"
"\n"
"//Code from https://github.com/NadirRoGue\n"
"//Special thanks https://github.com/NadirRoGue\n"
"\n"
"layout (local_size_x = 4, local_size_y = 4, local_size_z = 4) in;\n"
"\n"
"layout (rgba8, binding = 0) uniform image3D outVolTex;\n"
"\n"
"// =====================================================================================\n"
"// Code from Sebastien Hillarie 3d noise generator https://github.com/sebh/TileableVolumeNoise\n"
"uniform float frequenceMul[6u] = float[]( 2.0,8.0,14.0,20.0,26.0,32.0 );\n"
"\n"
"float hash(int n)\n"
"{\n"
"	return fract(sin(float(n) + 1.951) * 43758.5453123);\n"
"}\n"
"\n"
"float noise(vec3 x)\n"
"{\n"
"	vec3 p = floor(x);\n"
"	vec3 f = fract(x);\n"
"\n"
"	f = f*f*(vec3(3.0) - vec3(2.0) * f);\n"
"	float n = p.x + p.y*57.0 + 113.0*p.z;\n"
"	return mix(\n"
"		mix(\n"
"			mix(hash(int(n + 0.0)), hash(int(n + 1.0)), f.x),\n"
"			mix(hash(int(n + 57.0)), hash(int(n + 58.0)), f.x),\n"
"			f.y),\n"
"		mix(\n"
"			mix(hash(int(n + 113.0)), hash(int(n + 114.0)), f.x),\n"
"			mix(hash(int(n + 170.0)), hash(int(n + 171.0)), f.x),\n"
"			f.y),\n"
"		f.z);\n"
"}\n"
"\n"
"float cells(vec3 p, float cellCount)\n"
"{\n"
"	vec3 pCell = p * cellCount;\n"
"	float d = 1.0e10;\n"
"	for (int xo = -1; xo <= 1; xo++)\n"
"	{\n"
"		for (int yo = -1; yo <= 1; yo++)\n"
"		{\n"
"			for (int zo = -1; zo <= 1; zo++)\n"
"			{\n"
"				vec3 tp = floor(pCell) + vec3(xo, yo, zo);\n"
"\n"
"				tp = pCell - tp - noise(mod(tp, cellCount / 1.0));\n"
"\n"
"				d = min(d, dot(tp, tp));\n"
"			}\n"
"		}\n"
"	}\n"
"	d = min(d, 1.0);\n"
"	d = max(d, 0.0f);\n"
"\n"
"	return d;\n"
"}\n"
"\n"
"\n"
"// From GLM (gtc/noise.hpp & detail/_noise.hpp)\n"
"vec4 mod289(vec4 x)\n"
"{\n"
"	return x - floor(x * vec4(1.0) / vec4(289.0)) * vec4(289.0);\n"
"}\n"
"\n"
"vec4 permute(vec4 x)\n"
"{\n"
"	return mod289(((x * 34.0) + 1.0) * x);\n"
"}\n"
"\n"
"vec4 taylorInvSqrt(vec4 r)\n"
"{\n"
"	return vec4(1.79284291400159) - vec4(0.85373472095314) * r;\n"
"}\n"
"\n"
"vec4 fade(vec4 t)\n"
"{\n"
"	return (t * t * t) * (t * (t * vec4(6) - vec4(15)) + vec4(10));\n"
"}\n"
"\n"
"float glmPerlin4D(vec4 Position, vec4 rep)\n"
"{\n"
"		vec4 Pi0 = mod(floor(Position), rep);	// Integer part for indexing\n"
"		vec4 Pi1 = mod(Pi0 + vec4(1), rep);		// Integer part + 1\n"
"		//Pi0 = mod(Pi0, vec4(289));\n"
"		//Pi1 = mod(Pi1, vec4(289));\n"
"		vec4 Pf0 = fract(Position);	// Fractional part for interpolation\n"
"		vec4 Pf1 = Pf0 - vec4(1);		// Fractional part - 1.0\n"
"		vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);\n"
"		vec4 iy = vec4(Pi0.y, Pi0.y, Pi1.y, Pi1.y);\n"
"		vec4 iz0 = vec4(Pi0.z);\n"
"		vec4 iz1 = vec4(Pi1.z);\n"
"		vec4 iw0 = vec4(Pi0.w);\n"
"		vec4 iw1 = vec4(Pi1.w);\n"
"\n"
"		vec4 ixy = permute(permute(ix) + iy);\n"
"		vec4 ixy0 = permute(ixy + iz0);\n"
"		vec4 ixy1 = permute(ixy + iz1);\n"
"		vec4 ixy00 = permute(ixy0 + iw0);\n"
"		vec4 ixy01 = permute(ixy0 + iw1);\n"
"		vec4 ixy10 = permute(ixy1 + iw0);\n"
"		vec4 ixy11 = permute(ixy1 + iw1);\n"
"\n"
"		vec4 gx00 = ixy00 / vec4(7);\n"
"		vec4 gy00 = floor(gx00) / vec4(7);\n"
"		vec4 gz00 = floor(gy00) / vec4(6);\n"
"		gx00 = fract(gx00) - vec4(0.5);\n"
"		gy00 = fract(gy00) - vec4(0.5);\n"
"		gz00 = fract(gz00) - vec4(0.5);\n"
"		vec4 gw00 = vec4(0.75) - abs(gx00) - abs(gy00) - abs(gz00);\n"
"		vec4 sw00 = step(gw00, vec4(0.0));\n"
"		gx00 -= sw00 * (step(vec4(0), gx00) - vec4(0.5));\n"
"		gy00 -= sw00 * (step(vec4(0), gy00) - vec4(0.5));\n"
"\n"
"		vec4 gx01 = ixy01 / vec4(7);\n"
"		vec4 gy01 = floor(gx01) / vec4(7);\n"
"		vec4 gz01 = floor(gy01) / vec4(6);\n"
"		gx01 = fract(gx01) - vec4(0.5);\n"
"		gy01 = fract(gy01) - vec4(0.5);\n"
"		gz01 = fract(gz01) - vec4(0.5);\n"
"		vec4 gw01 = vec4(0.75) - abs(gx01) - abs(gy01) - abs(gz01);\n"
"		vec4 sw01 = step(gw01, vec4(0.0));\n"
"		gx01 -= sw01 * (step(vec4(0), gx01) - vec4(0.5));\n"
"		gy01 -= sw01 * (step(vec4(0), gy01) - vec4(0.5));\n"
"\n"
"		vec4 gx10 = ixy10 / vec4(7);\n"
"		vec4 gy10 = floor(gx10) / vec4(7);\n"
"		vec4 gz10 = floor(gy10) / vec4(6);\n"
"		gx10 = fract(gx10) - vec4(0.5);\n"
"		gy10 = fract(gy10) - vec4(0.5);\n"
"		gz10 = fract(gz10) - vec4(0.5);\n"
"		vec4 gw10 = vec4(0.75) - abs(gx10) - abs(gy10) - abs(gz10);\n"
"		vec4 sw10 = step(gw10, vec4(0));\n"
"		gx10 -= sw10 * (step(vec4(0), gx10) - vec4(0.5));\n"
"		gy10 -= sw10 * (step(vec4(0), gy10) - vec4(0.5));\n"
"\n"
"		vec4 gx11 = ixy11 / vec4(7);\n"
"		vec4 gy11 = floor(gx11) / vec4(7);\n"
"		vec4 gz11 = floor(gy11) / vec4(6);\n"
"		gx11 = fract(gx11) - vec4(0.5);\n"
"		gy11 = fract(gy11) - vec4(0.5);\n"
"		gz11 = fract(gz11) - vec4(0.5);\n"
"		vec4 gw11 = vec4(0.75) - abs(gx11) - abs(gy11) - abs(gz11);\n"
"		vec4 sw11 = step(gw11, vec4(0.0));\n"
"		gx11 -= sw11 * (step(vec4(0), gx11) - vec4(0.5));\n"
"		gy11 -= sw11 * (step(vec4(0), gy11) - vec4(0.5));\n"
"\n"
"		vec4 g0000 = vec4(gx00.x, gy00.x, gz00.x, gw00.x);\n"
"		vec4 g1000 = vec4(gx00.y, gy00.y, gz00.y, gw00.y);\n"
"		vec4 g0100 = vec4(gx00.z, gy00.z, gz00.z, gw00.z);\n"
"		vec4 g1100 = vec4(gx00.w, gy00.w, gz00.w, gw00.w);\n"
"		vec4 g0010 = vec4(gx10.x, gy10.x, gz10.x, gw10.x);\n"
"		vec4 g1010 = vec4(gx10.y, gy10.y, gz10.y, gw10.y);\n"
"		vec4 g0110 = vec4(gx10.z, gy10.z, gz10.z, gw10.z);\n"
"		vec4 g1110 = vec4(gx10.w, gy10.w, gz10.w, gw10.w);\n"
"		vec4 g0001 = vec4(gx01.x, gy01.x, gz01.x, gw01.x);\n"
"		vec4 g1001 = vec4(gx01.y, gy01.y, gz01.y, gw01.y);\n"
"		vec4 g0101 = vec4(gx01.z, gy01.z, gz01.z, gw01.z);\n"
"		vec4 g1101 = vec4(gx01.w, gy01.w, gz01.w, gw01.w);\n"
"		vec4 g0011 = vec4(gx11.x, gy11.x, gz11.x, gw11.x);\n"
"		vec4 g1011 = vec4(gx11.y, gy11.y, gz11.y, gw11.y);\n"
"		vec4 g0111 = vec4(gx11.z, gy11.z, gz11.z, gw11.z);\n"
"		vec4 g1111 = vec4(gx11.w, gy11.w, gz11.w, gw11.w);\n"
"\n"
"		vec4 norm00 = taylorInvSqrt(vec4(dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)));\n"
"		g0000 *= norm00.x;\n"
"		g0100 *= norm00.y;\n"
"		g1000 *= norm00.z;\n"
"		g1100 *= norm00.w;\n"
"\n"
"		vec4 norm01 = taylorInvSqrt(vec4(dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)));\n"
"		g0001 *= norm01.x;\n"
"		g0101 *= norm01.y;\n"
"		g1001 *= norm01.z;\n"
"		g1101 *= norm01.w;\n"
"\n"
"		vec4 norm10 = taylorInvSqrt(vec4(dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)));\n"
"		g0010 *= norm10.x;\n"
"		g0110 *= norm10.y;\n"
"		g1010 *= norm10.z;\n"
"		g1110 *= norm10.w;\n"
"\n"
"		vec4 norm11 = taylorInvSqrt(vec4(dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)));\n"
"		g0011 *= norm11.x;\n"
"		g0111 *= norm11.y;\n"
"		g1011 *= norm11.z;\n"
"		g1111 *= norm11.w;\n"
"\n"
"		float n0000 = dot(g0000, Pf0);\n"
"		float n1000 = dot(g1000, vec4(Pf1.x, Pf0.y, Pf0.z, Pf0.w));\n"
"		float n0100 = dot(g0100, vec4(Pf0.x, Pf1.y, Pf0.z, Pf0.w));\n"
"		float n1100 = dot(g1100, vec4(Pf1.x, Pf1.y, Pf0.z, Pf0.w));\n"
"		float n0010 = dot(g0010, vec4(Pf0.x, Pf0.y, Pf1.z, Pf0.w));\n"
"		float n1010 = dot(g1010, vec4(Pf1.x, Pf0.y, Pf1.z, Pf0.w));\n"
"		float n0110 = dot(g0110, vec4(Pf0.x, Pf1.y, Pf1.z, Pf0.w));\n"
"		float n1110 = dot(g1110, vec4(Pf1.x, Pf1.y, Pf1.z, Pf0.w));\n"
"		float n0001 = dot(g0001, vec4(Pf0.x, Pf0.y, Pf0.z, Pf1.w));\n"
"		float n1001 = dot(g1001, vec4(Pf1.x, Pf0.y, Pf0.z, Pf1.w));\n"
"		float n0101 = dot(g0101, vec4(Pf0.x, Pf1.y, Pf0.z, Pf1.w));\n"
"		float n1101 = dot(g1101, vec4(Pf1.x, Pf1.y, Pf0.z, Pf1.w));\n"
"		float n0011 = dot(g0011, vec4(Pf0.x, Pf0.y, Pf1.z, Pf1.w));\n"
"		float n1011 = dot(g1011, vec4(Pf1.x, Pf0.y, Pf1.z, Pf1.w));\n"
"		float n0111 = dot(g0111, vec4(Pf0.x, Pf1.y, Pf1.z, Pf1.w));\n"
"		float n1111 = dot(g1111, Pf1);\n"
"\n"
"		vec4 fade_xyzw = fade(Pf0);\n"
"		vec4 n_0w = mix(vec4(n0000, n1000, n0100, n1100), vec4(n0001, n1001, n0101, n1101), fade_xyzw.w);\n"
"		vec4 n_1w = mix(vec4(n0010, n1010, n0110, n1110), vec4(n0011, n1011, n0111, n1111), fade_xyzw.w);\n"
"		vec4 n_zw = mix(n_0w, n_1w, fade_xyzw.z);\n"
"		vec2 n_yzw = mix(vec2(n_zw.x, n_zw.y), vec2(n_zw.z, n_zw.w), fade_xyzw.y);\n"
"		float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);\n"
"		return float(2.2) * n_xyzw;\n"
"}\n"
"\n"
"float remap(float originalValue, float originalMin, float originalMax, float newMin, float newMax)\n"
"{\n"
"	return newMin + (((originalValue - originalMin) / (originalMax - originalMin)) * (newMax - newMin));\n"
"}\n"
"\n"
"// ======================================================================\n"
"\n"
"float worleyNoise3D(vec3 p, float cellCount)\n"
"{\n"
"	return cells(p, cellCount);\n"
"}\n"
"\n"
"float perlinNoise3D(vec3 pIn, float frequency, int octaveCount)\n"
"{\n"
"	float octaveFrenquencyFactor = 2.0;			// noise frequency factor between octave, forced to 2\n"
"\n"
"	// Compute the sum for each octave\n"
"	float sum = 0.0f;\n"
"	float weightSum = 0.0f;\n"
"	float weight = 0.5f;\n"
"	for (int oct = 0; oct < octaveCount; oct++)\n"
"	{\n"
"		// Perlin vec3 is bugged in GLM on the Z axis :(, black stripes are visible\n"
"		// So instead we use 4d Perlin and only use xyz...\n"
"		//glm::vec3 p(x * freq, y * freq, z * freq);\n"
"		//float val = glm::perlin(p, glm::vec3(freq)) *0.5 + 0.5;\n"
"\n"
"		vec4 p = vec4(pIn.x, pIn.y, pIn.z, 0.0) * vec4(frequency);\n"
"		float val = glmPerlin4D(p, vec4(frequency));\n"
"\n"
"		sum += val * weight;\n"
"		weightSum += weight;\n"
"\n"
"		weight *= weight;\n"
"		frequency *= octaveFrenquencyFactor;\n"
"	}\n"
"\n"
"	float noise = (sum / weightSum) *0.5 + 0.5;;\n"
"	noise = min(noise, 1.0f);\n"
"	noise = max(noise, 0.0f);\n"
"	return noise;\n"
"}\n"
"\n"
"vec4 stackable3DNoise(ivec3 pixel)\n"
"{\n"
"	vec3 coord = vec3(float(pixel.x) / 32.0, float(pixel.y) / 32.0, float(pixel.z) / 32.0);\n"
"\n"
"	// 3 octaves\n"
"	float cellCount = 2.0;\n"
"	float worleyNoise0 = (1.0f - worleyNoise3D(coord, cellCount * 1.0));\n"
"	float worleyNoise1 = (1.0f - worleyNoise3D(coord, cellCount * 2.0));\n"
"	float worleyNoise2 = (1.0f - worleyNoise3D(coord, cellCount * 4.0));\n"
"	float worleyNoise3 = (1.0f - worleyNoise3D(coord, cellCount * 8.0));\n"
"	float worleyFBM0 = worleyNoise0*0.625f + worleyNoise1*0.25f + worleyNoise2*0.125f;\n"
"	float worleyFBM1 = worleyNoise1*0.625f + worleyNoise2*0.25f + worleyNoise3*0.125f;\n"
"	float worleyFBM2 = worleyNoise2*0.75f + worleyNoise3*0.25f; \n"
"	// cellCount=4 -> worleyNoise4 is just noise due to sampling frequency=texel freque. So only take into account 2 frequencies for FBM\n"
"\n"
"	return vec4(worleyFBM0, worleyFBM1, worleyFBM2, 1.0);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    ivec3 pixel = ivec3(gl_GlobalInvocationID.xyz);\n"
"\n"
"	imageStore (outVolTex, pixel, stackable3DNoise(pixel));\n"
"}\n";
static const char* computeSourceWeather = 
"#version 430\n"
"\n"
"// Code from https://github.com/NadirRoGue\n"
"// Special thanks to Nadir Roman Guerrero\n"
"\n"
"\n"
"/*\n"
"	Fills a 2D texture with a low octave perlin noise to be used\n"
"	in the volumetric clouds as weather map\n"
"*/\n"
"\n"
"layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
"\n"
"layout (rgba8, binding = 0) uniform image2D outWeatherTex;\n"
"\n"
"uniform vec3 seed;\n"
"\n"
"// =====================================================================================\n"
"// COMMON\n"
"float random2D( in vec2 st ) \n"
"{\n"
"    return fract( sin(dot( st.xy, vec2(12.9898,78.233 ) + seed.xy ) ) * 43758.5453123);\n"
"}\n"
"\n"
"// =====================================================================================\n"
"// PERLIN NOISE SPECIFIC\n"
"\n"
"uniform float perlinAmplitude = 0.5;\n"
"uniform float perlinFrequency = 0.8;\n"
"uniform float perlinScale = 100.0;\n"
"uniform int perlinOctaves = 4;\n"
"\n"
"float noiseInterpolation(in vec2 i_coord, in float i_size)\n"
"{\n"
"	vec2 grid = i_coord * i_size;\n"
"    \n"
"    vec2 randomInput = floor( grid );\n"
"    vec2 weights     = fract( grid );\n"
"    \n"
"    \n"
"    float p0 = random2D( randomInput );\n"
"    float p1 = random2D( randomInput + vec2( 1.0, 0.0  ) );\n"
"    float p2 = random2D( randomInput + vec2( 0.0, 1.0 ) );\n"
"    float p3 = random2D( randomInput + vec2( 1.0, 1.0 ) );\n"
"    \n"
"    weights = smoothstep( vec2( 0.0, 0.0 ), vec2( 1.0, 1.0 ), weights ); \n"
"    \n"
"    return p0 +\n"
"           ( p1 - p0 ) * ( weights.x ) +\n"
"           ( p2 - p0 ) * ( weights.y ) * ( 1.0 - weights.x ) +\n"
"           ( p3 - p1 ) * ( weights.y * weights.x );    \n"
"}\n"
"\n"
"float perlinNoise(vec2 uv, float sc, float f, float a, int o)\n"
"{\n"
"    float noiseValue = 0.0;\n"
"    \n"
"    float localAplitude  = a;\n"
"    float localFrecuency = f;\n"
"\n"
"    for( int index = 0; index < o; index++ )\n"
"    {\n"
"     	       \n"
"        noiseValue += noiseInterpolation( uv, sc * localFrecuency ) * localAplitude;\n"
"    \n"
"        localAplitude   *= 0.25;\n"
"        localFrecuency  *= 3.0;\n"
"    }    \n"
"\n"
"	return noiseValue * noiseValue;\n"
"}\n"
"\n"
"// =====================================================================================\n"
"\n"
"\n"
"void main()\n"
"{\n"
"    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);\n"
"	\n"
"	vec2 uv = vec2(float(pixel.x + 2.0) / 1024.0, float(pixel.y) / 1024.0);\n"
"	vec2 suv = vec2(uv.x + 5.5, uv.y + 5.5);\n"
"\n"
"	float cloudType = clamp(perlinNoise(suv, perlinScale*3.0, 0.3, 0.7,10), 0.0, 1.0);\n"
"	//float cloudType_ = clamp(perlinNoise(suv.xy, perlinScale/ 8., 0.3, 0.7,10), 0.0, 1.0);\n"
"\n"
"	//cloudType = cloudType*0.5 + cloudType_*0.5;\n"
"\n"
"	float coverage = perlinNoise(uv, perlinScale*0.95, perlinFrequency, perlinAmplitude, 4);\n"
"	vec4 weather = vec4( clamp(coverage, 0, 1), cloudType, 0, 1);\n"
"\n"
"	imageStore (outWeatherTex, pixel, weather);\n"
"}\n";

// Fullscreen noise debug view (reuses the FSQ vertex layout).
static const char* fragmentSourceSkyDynDebug =
	"#version 330 core\n"
	"uniform sampler2D uWeather;\n"
	"uniform sampler3D uNoise;\n"
	"uniform sampler3D uErode;\n"
	"uniform int uMode;\n"
	"uniform float uSlice;\n"
	"uniform vec2 uResolution;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / uResolution;\n"
	"    vec3 c;\n"
	"    if (uMode == 2) { c = texture(uNoise, vec3(uv, uSlice)).rgb; }\n"
	"    else if (uMode == 3) { c = texture(uErode, vec3(uv, uSlice)).rgb; }\n"
	"    else { c = texture(uWeather, uv).rgb; }\n"
	"    FragColor = vec4(c, 1.0);\n"
	"}\n";

static GLuint s_progPerlin = 0;
static GLuint s_progWorley = 0;
static GLuint s_progWeather = 0;
static GLint s_uWeatherSeed = -1;
static GLint s_uWeatherFreq = -1;

static GLuint s_perlinTex = 0;
static GLuint s_worleyTex = 0;
static GLuint s_weatherTex = 0;
static float s_lastSeed = -1.0e30f;
static float s_lastFreq = -1.0e30f;

static GLuint s_dbgProg = 0;
static GLint s_dbgWeather = -1;
static GLint s_dbgNoise = -1;
static GLint s_dbgErode = -1;
static GLint s_dbgMode = -1;
static GLint s_dbgSlice = -1;
static GLint s_dbgRes = -1;
static qboolean s_dbgAnnounced = false;

// One-time diagnostic: read back level 0 and report R-channel min/max over a
// stride. Proves on the user's machine whether a compute dispatch actually
// wrote anything (zeros here + black debug views = lost/never-run writes).
static void SkyDyn_LogTexStats(const char* name, GLenum target, GLuint tex, int w, int h, int d)
{
	const size_t count = (size_t)w * (size_t)h * (size_t)d;
	unsigned char* pixels;
	size_t i;
	unsigned rmin = 255, rmax = 0;
	size_t nonzero = 0;

	if (tex == 0 || count == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: %s: no texture to verify (id=%u)\n", name, tex);
		return;
	}

	pixels = (unsigned char*)malloc(count * 4);
	if (pixels == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: %s: out of memory for readback\n", name);
		return;
	}

	if (target == GL_TEXTURE_3D)
		glBindTexture(GL_TEXTURE_3D, tex);
	else
		glBindTexture(GL_TEXTURE_2D, tex);
	glGetTexImage(target, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	if (target == GL_TEXTURE_3D)
		glBindTexture(GL_TEXTURE_3D, 0);
	else
		glBindTexture(GL_TEXTURE_2D, 0);

	for (i = 0; i < count; i += 16)
	{
		const unsigned r = pixels[i * 4];
		if (r < rmin)
			rmin = r;
		if (r > rmax)
			rmax = r;
		if (r != 0)
			nonzero++;
	}

	ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: %s R min=%u max=%u nonzero=%u/%u id=%u%s\n",
		name, rmin, rmax, (unsigned)nonzero, (unsigned)(count / 16), tex,
		(rmax == 0) ? "  <-- ALL ZEROS, compute writes lost" : "");
	free(pixels);
}

// Generates perlin (once) + worley (once) + weather (on seed/freq change).
// Safe to call every frame: early-outs on two float compares when current.
static void SkyDyn_GenNoise(qboolean force)
{
	float seed;
	float freq;
	Uint64 t0;
	GLenum err;

	if (!GLAD_GL_VERSION_4_3 || s_progPerlin == 0 || s_progWorley == 0 || s_progWeather == 0)
		return;

	seed = (r_cloud_seed != NULL) ? r_cloud_seed->value : 0.0f;
	freq = (r_cloud_freq != NULL) ? r_cloud_freq->value : 0.8f;

	if (!force && seed == s_lastSeed && freq == s_lastFreq &&
		s_perlinTex != 0 && s_worleyTex != 0 && s_weatherTex != 0)
		return;

	t0 = SDL_GetTicks();

	// Base shapes: 128^3 RGBA8, local 4^3 -> 32^3 groups (exact coverage).
	if (s_perlinTex == 0)
	{
		s_perlinTex = GL3_MakeTexture3D(SKYDYN_PERLIN_SIZE, SKYDYN_PERLIN_SIZE, SKYDYN_PERLIN_SIZE, GL_RGBA8);
		if (s_perlinTex != 0)
		{
			GL3_UseShader(s_progPerlin);
			GL3_BindImageTexture(0, s_perlinTex, GL_TRUE, GL_WRITE_ONLY, GL_RGBA8);
			glDispatchCompute(SKYDYN_PERLIN_SIZE / 4, SKYDYN_PERLIN_SIZE / 4, SKYDYN_PERLIN_SIZE / 4);
			GL3_MemoryBarrierCompute();
			glBindTexture(GL_TEXTURE_3D, s_perlinTex);
			glGenerateMipmap(GL_TEXTURE_3D);
			glBindTexture(GL_TEXTURE_3D, 0);
			GL3_MemoryBarrierCompute(); // mip writes -> visible to samplers
		}
	}

	// Erosion detail: 32^3 RGBA8, local 4^3 -> 8^3 groups (exact coverage).
	if (s_worleyTex == 0)
	{
		s_worleyTex = GL3_MakeTexture3D(SKYDYN_WORLEY_SIZE, SKYDYN_WORLEY_SIZE, SKYDYN_WORLEY_SIZE, GL_RGBA8);
		if (s_worleyTex != 0)
		{
			GL3_UseShader(s_progWorley);
			GL3_BindImageTexture(0, s_worleyTex, GL_TRUE, GL_WRITE_ONLY, GL_RGBA8);
			glDispatchCompute(SKYDYN_WORLEY_SIZE / 4, SKYDYN_WORLEY_SIZE / 4, SKYDYN_WORLEY_SIZE / 4);
			GL3_MemoryBarrierCompute();
			glBindTexture(GL_TEXTURE_3D, s_worleyTex);
			glGenerateMipmap(GL_TEXTURE_3D);
			glBindTexture(GL_TEXTURE_3D, 0);
			GL3_MemoryBarrierCompute(); // mip writes -> visible to samplers
		}
	}

	// Coverage/type map: 1024^2 RGBA8, local 16x16 -> 64x64 groups (exact coverage).
	// NOTE: source dispatches 1024/8 groups against a local size of 16 (2x over-dispatch,
	// out-of-bounds imageStores); exact 1024/16 coverage is used here instead.
	if (s_weatherTex == 0)
		s_weatherTex = GL3_MakeTexture2D(SKYDYN_WEATHER_SIZE, SKYDYN_WEATHER_SIZE, GL_RGBA8);
	if (s_weatherTex != 0)
	{
		GL3_UseShader(s_progWeather);
		GL3_BindImageTexture(0, s_weatherTex, GL_FALSE, GL_WRITE_ONLY, GL_RGBA8);
		glUniform3f(s_uWeatherSeed, seed, seed * 0.5f + 3.0f, seed * 0.25f + 9.0f);
		glUniform1f(s_uWeatherFreq, freq);
		glDispatchCompute(SKYDYN_WEATHER_SIZE / 16, SKYDYN_WEATHER_SIZE / 16, 1);
		GL3_MemoryBarrierCompute();
		glBindTexture(GL_TEXTURE_2D, s_weatherTex);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
		GL3_MemoryBarrierCompute(); // mip writes -> visible to samplers
	}

	s_lastSeed = seed;
	s_lastFreq = freq;

	err = glGetError();
	ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: noise generated in %ums (perlin=%u worley=%u weather=%u)%s\n",
		(unsigned)(SDL_GetTicks() - t0), s_perlinTex, s_worleyTex, s_weatherTex,
		(err == GL_NO_ERROR) ? "" : " — glGetError() != 0, check driver log");

	// Readback proof of what the dispatches actually wrote (one-time cost).
	SkyDyn_LogTexStats("perlin", GL_TEXTURE_3D, s_perlinTex, SKYDYN_PERLIN_SIZE, SKYDYN_PERLIN_SIZE, SKYDYN_PERLIN_SIZE);
	SkyDyn_LogTexStats("worley", GL_TEXTURE_3D, s_worleyTex, SKYDYN_WORLEY_SIZE, SKYDYN_WORLEY_SIZE, SKYDYN_WORLEY_SIZE);
	SkyDyn_LogTexStats("weather", GL_TEXTURE_2D, s_weatherTex, SKYDYN_WEATHER_SIZE, SKYDYN_WEATHER_SIZE, 1);
}

// Per-level dynamic-sky look (r_sky_levels master switch).
// Level identity = r_worldmodel->name substring, same convention as
// R_IsSilverpringMap / R_IsDarkmireSwampMap in gl3_Sky.c. Applied once per
// map, and only to cvars the user never touched (cvar->modified): user tweaks
// (console or archived vars.rc) always win; untouched cvars follow the table.
typedef struct
{
	const char* match; // strstr on the map name; NULL = default row (must be last)
	int preset;        // r_sky_preset
	float coverage;    // r_cloud_coverage
} skylevel_t;

static const skylevel_t skylevels[] = {
	// NOTE: first match wins — "hive" precedes "ss" because hivepriestess
	// contains "ss" ("...tess"). Both are night today, order keeps it robust.
	{ "hive", 3, 0.55f },      // K'Chekrik hive 1/2, trial pit, priestess: night
	{ "gauntlet", 3, 0.55f },  // The gauntlet: night
	{ "ss", 3, 0.40f },        // Silverspring (docks/warehouse/town/palace): night
	{ "dmire", 4, 0.55f },     // Darkmire swamp: morning mist
	{ "and", 0, 0.35f },       // Andoria (healer/plaza/slums/academic): day
	{ "kellcaves", 5, 0.50f }, // Kell caves: evening
	{ "canyon", 5, 0.50f },    // Katlit'k canyon: evening
	{ "oglemine", 0, 0.30f },  // Ogle mines 1/2: day
	{ "dungeon", 0, 0.30f },   // Morcalavin's dungeon: day
	{ "cloud", 0, 0.40f },     // Cloud fortress (hub/quarters/labs/sanctum): day
	{ NULL, 2, 0.45f },        // tutorial + anything else: auto day
};

static char s_appliedMap[64] = { 0 };

static void SkyDyn_ApplyLevelSky(void)
{
	const char* mapname;
	const skylevel_t* row;
	char val[32];
	qboolean userTouched;

	if (r_sky_levels == NULL || (int)r_sky_levels->value == 0)
		return;
	if (r_dynsky == NULL || (int)r_dynsky->value == 0)
		return; // dynamic sky off: leave every cvar alone
	if (r_worldmodel == NULL || r_worldmodel->name[0] == '\0')
	{
		s_appliedMap[0] = '\0';
		return;
	}

	mapname = r_worldmodel->name;
	if (strcmp(mapname, s_appliedMap) == 0)
		return;

	row = &skylevels[sizeof(skylevels) / sizeof(skylevels[0]) - 1];
	for (size_t i = 0; i < sizeof(skylevels) / sizeof(skylevels[0]); i++)
	{
		if (skylevels[i].match != NULL && strstr(mapname, skylevels[i].match) != NULL)
		{
			row = &skylevels[i];
			break;
		}
	}

	strncpy(s_appliedMap, mapname, sizeof(s_appliedMap) - 1);
	s_appliedMap[sizeof(s_appliedMap) - 1] = '\0';

	userTouched = false;
	if (r_sky_preset != NULL && !r_sky_preset->modified)
	{
		snprintf(val, sizeof(val), "%d", row->preset);
		ri.Cvar_Set("r_sky_preset", val);
		r_sky_preset->modified = false; // ours, not the user's: re-apply next map
	}
	else
		userTouched = true;
	if (r_cloud_coverage != NULL && !r_cloud_coverage->modified)
	{
		snprintf(val, sizeof(val), "%.2f", (double)row->coverage);
		ri.Cvar_Set("r_cloud_coverage", val);
		r_cloud_coverage->modified = false;
	}
	else
		userTouched = true;

	if (userTouched)
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: level sky %s keeps your settings (preset=%d coverage=%.2f)\n",
			mapname, (r_sky_preset != NULL) ? (int)r_sky_preset->value : 2,
			(r_cloud_coverage != NULL) ? r_cloud_coverage->value : 0.45f);
	else
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: level sky %s -> preset %d, coverage %.2f\n",
			mapname, row->preset, row->coverage);
}

void GL3_SkyDyn_Update(void)
{
	// Regen check is two float compares when nothing changed.
	SkyDyn_GenNoise(false);
	// Per-level look, applied once per map (cheap name compare afterwards).
	SkyDyn_ApplyLevelSky();
}

qboolean GL3_SkyDyn_DrawDebug(void)
{
	int mode;
	float width, height;

	mode = (r_cloud_debug != NULL) ? (int)r_cloud_debug->value : 0;
	if (mode < 1 || mode > 3)
		return false;
	if (s_dbgProg == 0 || s_weatherTex == 0 || s_perlinTex == 0 || s_worleyTex == 0)
		return false;

	if (!s_dbgAnnounced)
	{
		s_dbgAnnounced = true;
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: debug draw mode=%d (dbgProg=%u weather=%u perlin=%u worley=%u locs w=%d n=%d e=%d m=%d s=%d r=%d)\n",
			mode, s_dbgProg, s_weatherTex, s_perlinTex, s_worleyTex,
			s_dbgWeather, s_dbgNoise, s_dbgErode, s_dbgMode, s_dbgSlice, s_dbgRes);
	}

	glDepthMask(GL_FALSE);

	width = (gl3state.fbo_width > 0) ? (float)gl3state.fbo_width : (float)viddef.width;
	height = (gl3state.fbo_height > 0) ? (float)gl3state.fbo_height : (float)viddef.height;

	GL3_UseShader(s_dbgProg);
	glUniform1i(s_dbgMode, mode);
	glUniform1f(s_dbgSlice, 0.5f);
	glUniform2f(s_dbgRes, width, height);

	// Raw sampler binds (game textures use the R_Bind cache, not raw units).
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_weatherTex);
	glUniform1i(s_dbgWeather, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_3D, s_perlinTex);
	glUniform1i(s_dbgNoise, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_3D, s_worleyTex);
	glUniform1i(s_dbgErode, 2);

	glBindVertexArray(gl3state.vaoFSQ);

	// The world pass culls FRONT faces (reflection pass: BACK) — the legacy sky
	// path disables culling before drawing for exactly this reason. The debug
	// view runs earlier (top of R_DrawSkyBox), so disable culling here too or
	// the front-facing fullscreen strip is culled and nothing is drawn (black).
	{
		GLboolean cull_was_enabled;
		glGetBooleanv(GL_CULL_FACE, &cull_was_enabled);
		glDisable(GL_CULL_FACE);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (cull_was_enabled)
			glEnable(GL_CULL_FACE);
	}

	// Restore: unit 0 active, game-texture cache invalidated so the next
	// R_Bind/R_BindImage re-binds (it would otherwise skip on stale texnums).
	glActiveTexture(GL_TEXTURE0);
	gl_state.currenttextures[0] = -1;
	gl_state.currenttextures[1] = -1;

	glDepthMask(GL_TRUE);
	return true;
}

// ============================================================
// Phase 4: Tier-B volumetric clouds.
//
// Adapted from TerrainEngine-OpenGL shaders/volumetric_clouds.comp (MIT,
// (c) 2018 Federico Vaccaro; raymarch approach via NadirRoGue/WFP/reinder/
// rikardolajos/clayjohn — see DYNAMIC_SKY_PLAN.md). Differences from source:
// - Single rgba16f output (cloud rgb + alpha). The background stays in the
//   framebuffer (Tier-A/legacy sky is already drawn) and the composite
//   alpha-blends over it — no sky/depth texture inputs needed.
// - Occlusion comes free from the far-depth composite (opaque geometry wins
//   via LEQUAL). Low clouds correctly covering nearby terrain needs per-pixel
//   distance (Phase 5); high clouds — the common case — are already right.
// - Dome rescaled to H2 units: r_cloud_base (inner height) + r_cloud_thick.
// ============================================================

static const char* computeSourceClouds =
	"#version 430 core\n"
	"layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
	"layout(rgba16f, binding = 0) uniform image2D outClouds;\n"
	"uniform vec2 iResolution;\n"
	"uniform float iTime;\n"
	"uniform mat4 inv_view;\n"
	"uniform mat4 inv_proj;\n"
	"uniform vec3 lightColor;\n"
	"uniform sampler3D cloud;\n"
	"uniform sampler3D worley32;\n"
	"uniform sampler2D weatherTex;\n"
	"uniform vec3 lightDirection;\n"
	"uniform float coverage_multiplier;\n"
	"uniform float cloudSpeed;\n"
	"uniform float crispiness;\n"
	"uniform vec3 cameraPosition;\n"
	"uniform vec3 cloudColorTop;\n"
	"uniform vec3 cloudColorBottom;\n"
	"uniform vec3 skyColorTop;\n"
	"uniform vec3 skyColorBottom;\n"
	"uniform vec3 glareColor;\n" // sun/moon glare tint (preset-driven, replaces hardcoded orange)
	"uniform vec3 noiseKernel[6u] = vec3[](\n"
	"	vec3(0.38051305, 0.92453449, -0.02111345),\n"
	"	vec3(-0.50625799, -0.03590792, -0.86163418),\n"
	"	vec3(-0.32509218, -0.94557439, 0.01428793),\n"
	"	vec3(0.09026238, -0.27376545, 0.95755165),\n"
	"	vec3(0.28128598, 0.42443639, -0.86065785),\n"
	"	vec3(-0.16852403, 0.14748697, 0.97460106));\n"
	"#define STRATUS_GRADIENT vec4(0.0, 0.1, 0.2, 0.3)\n"
	"#define STRATOCUMULUS_GRADIENT vec4(0.02, 0.2, 0.48, 0.625)\n"
	"#define CUMULUS_GRADIENT vec4(0.00, 0.1625, 0.88, 0.98)\n"
	"uniform float earthRadius;\n"
	"uniform float sphereInnerRadius;\n"
	"uniform float sphereOuterRadius;\n"
	"#define EARTH_RADIUS earthRadius\n"
	"#define SPHERE_INNER_RADIUS (EARTH_RADIUS + sphereInnerRadius)\n"
	"#define SPHERE_OUTER_RADIUS (SPHERE_INNER_RADIUS + sphereOuterRadius)\n"
	"#define SPHERE_DELTA float(SPHERE_OUTER_RADIUS - SPHERE_INNER_RADIUS)\n"
	"#define CLOUDS_MIN_TRANSMITTANCE 1e-1\n"
	"#define CLOUDS_TRANSMITTANCE_THRESHOLD 1.0 - CLOUDS_MIN_TRANSMITTANCE\n"
	"#define SUN_DIR lightDirection\n"
	"#define SUN_COLOR lightColor*vec3(1.1,1.1,0.95)\n"
	"#define CLOUDS_AMBIENT_COLOR_TOP cloudColorTop\n"
	"#define CLOUDS_AMBIENT_COLOR_BOTTOM cloudColorBottom\n"
	"vec3 sphereCenter = vec3(0.0, -EARTH_RADIUS, 0.0);\n"
	"float HG(float sundotrd, float g) {\n"
	"	float gg = g * g;\n"
	"	return (1. - gg) / pow(1. + gg - 2. * g * sundotrd, 1.5);\n"
	"}\n"
	"bool raySphereintersectionSkyMap(vec3 rd, float radius, out vec3 startPos) {\n"
	"	float t;\n"
	"	vec3 sphereCenter_ = vec3(0.0);\n"
	"	float radius2 = radius*radius;\n"
	"	vec3 L = -sphereCenter_;\n"
	"	float a = dot(rd, rd);\n"
	"	float b = 2.0 * dot(rd, L);\n"
	"	float c = dot(L,L) - radius2;\n"
	"	float discr = b*b - 4.0 * a * c;\n"
	"	t = max(0.0, (-b + sqrt(discr))/2);\n"
	"	startPos = rd*t;\n"
	"	return true;\n"
	"}\n"
	"bool raySphereintersection(vec3 ro, vec3 rd, float radius, out vec3 startPos) {\n"
	"	float t;\n"
	"	sphereCenter.xz = cameraPosition.xz;\n"
	"	float radius2 = radius*radius;\n"
	"	vec3 L = ro - sphereCenter;\n"
	"	float a = dot(rd, rd);\n"
	"	float b = 2.0 * dot(rd, L);\n"
	"	float c = dot(L,L) - radius2;\n"
	"	float discr = b*b - 4.0 * a * c;\n"
	"	if (discr < 0.0) return false;\n"
	"	t = max(0.0, (-b + sqrt(discr))/2);\n"
	"	if (t == 0.0) return false;\n"
	"	startPos = ro + rd*t;\n"
	"	return true;\n"
	"}\n"
	"bool raySphereintersection2(vec3 ro, vec3 rd, float radius, out vec3 startPos) {\n"
	"	float t;\n"
	"	sphereCenter.xz = cameraPosition.xz;\n"
	"	float radius2 = radius*radius;\n"
	"	vec3 L = ro - sphereCenter;\n"
	"	float a = dot(rd, rd);\n"
	"	float b = 2.0 * dot(rd, L);\n"
	"	float c = dot(L,L) - radius2;\n"
	"	float discr = b*b - 4.0 * a * c;\n"
	"	if (discr < 0.0) return false;\n"
	"	t = max(0.0, (-b + sqrt(discr))/2);\n"
	"	if (t == 0.0) return false;\n"
	"	startPos = ro + rd*t;\n"
	"	return true;\n"
	"}\n"
	"vec3 getSun(const vec3 d, float powExp) {\n"
	"	float sun = clamp(dot(SUN_DIR, d), 0.0, 1.0);\n"
	"	vec3 col = 0.8*vec3(1.0,.6,0.1)*pow(sun, powExp);\n"
	"	return col;\n"
	"}\n"
	"float getHeightFraction(vec3 inPos) {\n"
	"	return (length(inPos - sphereCenter) - SPHERE_INNER_RADIUS)/(SPHERE_OUTER_RADIUS - SPHERE_INNER_RADIUS);\n"
	"}\n"
	"float remap(float originalValue, float originalMin, float originalMax, float newMin, float newMax) {\n"
	"	return newMin + (((originalValue - originalMin) / (originalMax - originalMin)) * (newMax - newMin));\n"
	"}\n"
	"float getDensityForCloud(float heightFraction, float cloudType) {\n"
	"	float stratusFactor = 1.0 - clamp(cloudType * 2.0, 0.0, 1.0);\n"
	"	float stratoCumulusFactor = 1.0 - abs(cloudType - 0.5) * 2.0;\n"
	"	float cumulusFactor = clamp(cloudType - 0.5, 0.0, 1.0) * 2.0;\n"
	"	vec4 baseGradient = stratusFactor * STRATUS_GRADIENT + stratoCumulusFactor * STRATOCUMULUS_GRADIENT + cumulusFactor * CUMULUS_GRADIENT;\n"
	"	return smoothstep(baseGradient.x, baseGradient.y, heightFraction) - smoothstep(baseGradient.z, baseGradient.w, heightFraction);\n"
	"}\n"
	"float threshold(const float v, const float t) {\n"
	"	return v > t ? v : 0.0;\n"
	"}\n"
	"const vec3 windDirection = normalize(vec3(0.5, 0.0, 0.1));\n"
	"vec2 getUVProjection(vec3 p) {\n"
	"	return p.xz/SPHERE_INNER_RADIUS + 0.5;\n"
	"}\n"
	"#define CLOUD_TOP_OFFSET 750.0\n"
	"#define SATURATE(x) clamp(x, 0.0, 1.0)\n"
	"#define CLOUD_SCALE crispiness\n"
	"#define CLOUD_SPEED cloudSpeed\n"
	"uniform float curliness;\n"
	"float sampleCloudDensity(vec3 p, bool expensive, float lod) {\n"
	"	float heightFraction = getHeightFraction(p);\n"
	"	vec3 animation = heightFraction * windDirection * CLOUD_TOP_OFFSET + windDirection * iTime * CLOUD_SPEED;\n"
	"	vec2 uv = getUVProjection(p);\n"
	"	vec2 moving_uv = getUVProjection(p + animation);\n"
	"	if (heightFraction < 0.0 || heightFraction > 1.0) return 0.0;\n"
	"	vec4 low_frequency_noise = textureLod(cloud, vec3(uv*CLOUD_SCALE, heightFraction), lod);\n"
	"	float lowFreqFBM = dot(low_frequency_noise.gba, vec3(0.625, 0.25, 0.125));\n"
	"	float base_cloud = remap(low_frequency_noise.r, -(1.0 - lowFreqFBM), 1., 0.0, 1.0);\n"
	"	float density = getDensityForCloud(heightFraction, 1.0);\n"
	"	base_cloud *= (density/heightFraction);\n"
	"	vec3 weather_data = texture(weatherTex, moving_uv).rgb;\n"
	"	float cloud_coverage = weather_data.r*coverage_multiplier;\n"
	"	float base_cloud_with_coverage = remap(base_cloud, cloud_coverage, 1.0, 0.0, 1.0);\n"
	"	base_cloud_with_coverage *= cloud_coverage;\n"
	"	if (expensive) {\n"
	"		vec3 erodeCloudNoise = textureLod(worley32, vec3(moving_uv*CLOUD_SCALE, heightFraction)*curliness, lod).rgb;\n"
	"		float highFreqFBM = dot(erodeCloudNoise.rgb, vec3(0.625, 0.25, 0.125));\n"
	"		float highFreqNoiseModifier = mix(highFreqFBM, 1.0 - highFreqFBM, clamp(heightFraction * 10.0, 0.0, 1.0));\n"
	"		base_cloud_with_coverage = base_cloud_with_coverage - highFreqNoiseModifier * (1.0 - base_cloud_with_coverage);\n"
	"		base_cloud_with_coverage = remap(base_cloud_with_coverage*2.0, highFreqNoiseModifier * 0.2, 1.0, 0.0, 1.0);\n"
	"	}\n"
	"	return clamp(base_cloud_with_coverage, 0.0, 1.0);\n"
	"}\n"
	"float beer(float d) {\n"
	"	return exp(-d);\n"
	"}\n"
	"float powder(float d) {\n"
	"	return (1. - exp(-2.*d));\n"
	"}\n"
	"float phase(vec3 inLightVec, vec3 inViewVec, float g) {\n"
	"	float costheta = dot(inLightVec, inViewVec) / length(inLightVec) / length(inViewVec);\n"
	"	return HG(costheta, g);\n"
	"}\n"
	"uniform float absorption = 0.0035;\n"
	"float raymarchToLight(vec3 o, float stepSize, vec3 lightDir, float originalDensity, float lightDotEye) {\n"
	"	vec3 startPos = o;\n"
	"	float ds = stepSize * 6.0;\n"
	"	vec3 rayStep = lightDir * ds;\n"
	"	const float CONE_STEP = 1.0/6.0;\n"
	"	float coneRadius = 1.0;\n"
	"	float density = 0.0;\n"
	"	float coneDensity = 0.0;\n"
	"	float invDepth = 1.0/ds;\n"
	"	float sigma_ds = -ds*absorption;\n"
	"	vec3 pos;\n"
	"	float T = 1.0;\n"
	"	for (int i = 0; i < 6; i++) {\n"
	"		pos = startPos + coneRadius*noiseKernel[i]*float(i);\n"
	"		float heightFraction = getHeightFraction(pos);\n"
	"		if (heightFraction >= 0.0) {\n"
	"			float cloudDensity = sampleCloudDensity(pos, density > 0.3, i/16);\n"
	"			if (cloudDensity > 0.0) {\n"
	"				float Ti = exp(cloudDensity*sigma_ds);\n"
	"				T *= Ti;\n"
	"				density += cloudDensity;\n"
	"			}\n"
	"		}\n"
	"		startPos += rayStep;\n"
	"		coneRadius += CONE_STEP;\n"
	"	}\n"
	"	return T;\n"
	"}\n"
	"uniform bool enablePowder = false;\n"
	"uniform float densityFactor = 0.02;\n"
	"#define BAYER_FACTOR 1.0/16.0\n"
	"uniform float bayerFilter[16u] = float[](\n"
	"	0.0*BAYER_FACTOR, 8.0*BAYER_FACTOR, 2.0*BAYER_FACTOR, 10.0*BAYER_FACTOR,\n"
	"	12.0*BAYER_FACTOR, 4.0*BAYER_FACTOR, 14.0*BAYER_FACTOR, 6.0*BAYER_FACTOR,\n"
	"	3.0*BAYER_FACTOR, 11.0*BAYER_FACTOR, 1.0*BAYER_FACTOR, 9.0*BAYER_FACTOR,\n"
	"	15.0*BAYER_FACTOR, 7.0*BAYER_FACTOR, 13.0*BAYER_FACTOR, 5.0*BAYER_FACTOR);\n"
	"vec4 raymarchToCloud(vec3 startPos, vec3 endPos, vec3 bg, out vec4 cloudPos) {\n"
	"	vec3 path = endPos - startPos;\n"
	"	float len = length(path);\n"
	"	const int nSteps = 64;\n"
	"	float ds = len/nSteps;\n"
	"	vec3 dir = path/len;\n"
	"	dir *= ds;\n"
	"	vec4 col = vec4(0.0);\n"
	"	uvec2 fragCoord = gl_GlobalInvocationID.xy;\n"
	"	int a = int(fragCoord.x) % 4;\n"
	"	int b = int(fragCoord.y) % 4;\n"
	"	startPos += dir * bayerFilter[a * 4 + b];\n"
	"	vec3 pos = startPos;\n"
	"	float density = 0.0;\n"
	"	float lightDotEye = dot(normalize(SUN_DIR), normalize(dir));\n"
	"	float T = 1.0;\n"
	"	float sigma_ds = -ds*densityFactor;\n"
	"	bool expensive = true;\n"
	"	bool entered = false;\n"
	"	for (int i = 0; i < nSteps; ++i) {\n"
	"		float density_sample = sampleCloudDensity(pos, true, i/16);\n"
	"		if (density_sample > 0.0) {\n"
	"			if (!entered) { cloudPos = vec4(pos, 1.0); entered = true; }\n"
	"			float height = getHeightFraction(pos);\n"
	"			vec3 ambientLight = CLOUDS_AMBIENT_COLOR_BOTTOM;\n"
	"			float light_density = raymarchToLight(pos, ds*0.1, SUN_DIR, density_sample, lightDotEye);\n"
	"			float scattering = mix(HG(lightDotEye, -0.08), HG(lightDotEye, 0.08), clamp(lightDotEye*0.5 + 0.5, 0.0, 1.0));\n"
	"			scattering = max(scattering, 1.0);\n"
	"			float powderTerm = powder(density_sample);\n"
	"			if (!enablePowder) powderTerm = 1.0;\n"
	"			vec3 S = 0.6*(mix(mix(ambientLight*1.8, bg, 0.2), scattering*SUN_COLOR, powderTerm*light_density)) * density_sample;\n"
	"			float dTrans = exp(density_sample*sigma_ds);\n"
	"			vec3 Sint = (S - S * dTrans) * (1. / density_sample);\n"
	"			col.rgb += T * Sint;\n"
	"			T *= dTrans;\n"
	"		}\n"
	"		if (T <= CLOUDS_MIN_TRANSMITTANCE) break;\n"
	"		pos += dir;\n"
	"	}\n"
	"	col.a = 1.0 - T;\n"
	"	return col;\n"
	"}\n"
	"vec3 computeClipSpaceCoord(uvec2 fragCoord) {\n"
	"	vec2 ray_nds = 2.0*vec2(fragCoord.xy)/iResolution.xy - 1.0;\n"
	"	return vec3(ray_nds, 1.0);\n"
	"}\n"
	"float computeFogAmount(in vec3 startPos, in float factor) {\n"
	"	float dist = length(startPos - cameraPosition);\n"
	"	float radius = (cameraPosition.y - sphereCenter.y) * 0.3;\n"
	"	float alpha = (dist / radius);\n"
	"	return (1.-exp(-dist*alpha*factor));\n"
	"}\n"
	"void main() {\n"
	"	ivec2 fragCoord = ivec2(gl_GlobalInvocationID.xy);\n"
	"	if (fragCoord.x >= int(iResolution.x) || fragCoord.y >= int(iResolution.y)) return;\n"
	"	vec4 ray_clip = vec4(computeClipSpaceCoord(fragCoord), 1.0);\n"
	"	vec4 ray_view = inv_proj * ray_clip;\n"
	"	ray_view = vec4(ray_view.xy, -1.0, 0.0);\n"
	"	vec3 worldDir = normalize((inv_view * ray_view).xyz);\n"
	"	worldDir = worldDir.xzy; // H2 world is Z-up; dome math below is Y-up.\n"
	"	vec3 startPos, endPos;\n"
	"	vec3 cubeMapEndPos;\n"
	"	raySphereintersectionSkyMap(worldDir, 0.5, cubeMapEndPos);\n"
	"	vec4 bg = vec4(mix(skyColorBottom, skyColorTop, clamp(1.0 - exp(8.5 - 17.0*clamp(worldDir.y*0.5 + 0.5, 0.0, 1.0)), 0.0, 1.0)), 1.0);\n"
	"	vec3 red = vec3(1.0);\n"
	"	bg = mix(mix(red.rgbr, vec4(1.0), SUN_DIR.y), bg, pow(max(cubeMapEndPos.y+0.1, .0), 0.2));\n"
	"	vec3 fogRay;\n"
	"	bool hitInner, hitOuter;\n"
	"	if (cameraPosition.y < SPHERE_INNER_RADIUS - EARTH_RADIUS) {\n"
	"		hitInner = raySphereintersection(cameraPosition, worldDir, SPHERE_INNER_RADIUS, startPos);\n"
	"		hitOuter = raySphereintersection(cameraPosition, worldDir, SPHERE_OUTER_RADIUS, endPos);\n"
	"		if (!hitInner || !hitOuter) { imageStore(outClouds, fragCoord, vec4(0.0)); return; }\n"
	"		fogRay = startPos;\n"
	"	} else if (cameraPosition.y > SPHERE_INNER_RADIUS - EARTH_RADIUS && cameraPosition.y < SPHERE_OUTER_RADIUS - EARTH_RADIUS) {\n"
	"		startPos = cameraPosition;\n"
	"		hitOuter = raySphereintersection(cameraPosition, worldDir, SPHERE_OUTER_RADIUS, endPos);\n"
	"		if (!hitOuter) { imageStore(outClouds, fragCoord, vec4(0.0)); return; }\n"
	"		bool h = raySphereintersection(cameraPosition, worldDir, SPHERE_INNER_RADIUS, fogRay);\n"
	"		if (!h) fogRay = startPos;\n"
	"	} else {\n"
	"		hitOuter = raySphereintersection2(cameraPosition, worldDir, SPHERE_OUTER_RADIUS, startPos);\n"
	"		bool h2 = raySphereintersection2(cameraPosition, worldDir, SPHERE_INNER_RADIUS, endPos);\n"
	"		hitInner = h2;\n"
	"		if (!hitOuter || !hitInner) { imageStore(outClouds, fragCoord, vec4(0.0)); return; }\n"
	"		hitOuter = raySphereintersection(cameraPosition, worldDir, SPHERE_OUTER_RADIUS, fogRay);\n"
	"	}\n"
	"	float fogAmount = computeFogAmount(fogRay, 0.00006);\n"
	"	if (fogAmount > 0.965) { imageStore(outClouds, fragCoord, vec4(0.0)); return; }\n"
	"	vec4 cloudPos;\n"
	"	vec4 v = raymarchToCloud(startPos, endPos, bg.rgb, cloudPos);\n"
	"	float cloudAlphaness = threshold(v.a, 0.2);\n"
	"	v.rgb = v.rgb*1.8 - 0.1;\n"
	"	v.rgb = mix(v.rgb, bg.rgb*v.a, clamp(fogAmount, 0., 1.));\n"
	"	float sun = clamp(dot(SUN_DIR, normalize(endPos - startPos)), 0.0, 1.0);\n"
	"	vec3 s = glareColor*pow(sun, 256.0);\n"
	"	v.rgb += s*v.a;\n"
	"	imageStore(outClouds, fragCoord, vec4(v.rgb, cloudAlphaness));\n"
	"}\n";

// Fullscreen textured vertex shader (far depth, like Tier-A) for the composite.
static const char* vertexSourceSkyDynTextured =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"out vec2 vTexCoord;\n"
	"void main() {\n"
	"    gl_Position = vec4(aPos, 1.0, 1.0);\n"
	"    vTexCoord = aTexCoord;\n"
	"}\n";

// Composite: cloud buffer (rgb + alpha) over the framebuffer sky.
static const char* fragmentSourceSkyDynComposite =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uClouds;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 c = texture(uClouds, vTexCoord);\n"
	"    FragColor = vec4(c.rgb, c.a);\n"
	"}\n";

static GLuint s_progClouds = 0;
static GLint s_clRes = -1;
static GLint s_clTime = -1;
static GLint s_clInvProj = -1;
static GLint s_clInvView = -1;
static GLint s_clCamPos = -1;
static GLint s_clLightDir = -1;
static GLint s_clLightColor = -1;
static GLint s_clSkyTop = -1;
static GLint s_clSkyBottom = -1;
static GLint s_clCloudTop = -1;
static GLint s_clCloudBottom = -1;
static GLint s_clSampCloud = -1;
static GLint s_clSampWorley = -1;
static GLint s_clSampWeather = -1;
static GLint s_clCoverage = -1;
static GLint s_clSpeed = -1;
static GLint s_clCrisp = -1;
static GLint s_clCurl = -1;
static GLint s_clAbsorb = -1;
static GLint s_clDensity = -1;
static GLint s_clEarthR = -1;
static GLint s_clInnerR = -1;
static GLint s_clOuterR = -1;

static GLuint s_compProg = 0;
static GLint s_cpClouds = -1;
static GLint s_clGlare = -1;

static GLuint s_cloudTex = 0;
static int s_cloudW = 0;
static int s_cloudH = 0;
static qboolean s_cloudAnnounced = false;

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

	// Phase 3: noise programs + debug view + one-time generation.
	if (GLAD_GL_VERSION_4_3)
	{
		s_progPerlin = GL3_CreateComputeProgram(computeSourcePerlinWorley);
		s_progWorley = GL3_CreateComputeProgram(computeSourceWorley);
		s_progWeather = GL3_CreateComputeProgram(computeSourceWeather);
		if (s_progPerlin == 0 || s_progWorley == 0 || s_progWeather == 0)
			ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: noise programs failed — debug views unavailable.\n");
		else
		{
			s_uWeatherSeed = glGetUniformLocation(s_progWeather, "seed");
			s_uWeatherFreq = glGetUniformLocation(s_progWeather, "perlinFrequency");

			// Debug view program (graphics, FSQ layout).
			{
				const GLuint vert = SkyDyn_CompileShader(GL_VERTEX_SHADER, vertexSourceSkyDyn);
				const GLuint frag = SkyDyn_CompileShader(GL_FRAGMENT_SHADER, fragmentSourceSkyDynDebug);
				if (vert != 0 && frag != 0)
				{
					s_dbgProg = glCreateProgram();
					glAttachShader(s_dbgProg, vert);
					glAttachShader(s_dbgProg, frag);
					glLinkProgram(s_dbgProg);
					{
						GLint ok = 0;
						glGetProgramiv(s_dbgProg, GL_LINK_STATUS, &ok);
						if (!ok)
						{
							char info_log[512];
							glGetProgramInfoLog(s_dbgProg, sizeof(info_log), NULL, info_log);
							ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: debug program link error: %s\n", info_log);
							glDeleteProgram(s_dbgProg);
							s_dbgProg = 0;
						}
					}
					glDetachShader(s_dbgProg, vert);
					glDetachShader(s_dbgProg, frag);
				}
				if (vert != 0)
					glDeleteShader(vert);
				if (frag != 0)
					glDeleteShader(frag);
			}

			if (s_dbgProg != 0)
			{
				s_dbgWeather = glGetUniformLocation(s_dbgProg, "uWeather");
				s_dbgNoise = glGetUniformLocation(s_dbgProg, "uNoise");
				s_dbgErode = glGetUniformLocation(s_dbgProg, "uErode");
				s_dbgMode = glGetUniformLocation(s_dbgProg, "uMode");
				s_dbgSlice = glGetUniformLocation(s_dbgProg, "uSlice");
				s_dbgRes = glGetUniformLocation(s_dbgProg, "uResolution");
			}

			SkyDyn_GenNoise(true);
		}
	}

	// Phase 4: volumetric cloud raymarch + composite programs.
	if (GLAD_GL_VERSION_4_3)
	{
		s_progClouds = GL3_CreateComputeProgram(computeSourceClouds);
		if (s_progClouds == 0)
		{
			ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: cloud raymarch program failed — r_volclouds unavailable.\n");
		}
		else
		{
			s_clRes = glGetUniformLocation(s_progClouds, "iResolution");
			s_clTime = glGetUniformLocation(s_progClouds, "iTime");
			s_clInvProj = glGetUniformLocation(s_progClouds, "inv_proj");
			s_clInvView = glGetUniformLocation(s_progClouds, "inv_view");
			s_clCamPos = glGetUniformLocation(s_progClouds, "cameraPosition");
			s_clLightDir = glGetUniformLocation(s_progClouds, "lightDirection");
			s_clLightColor = glGetUniformLocation(s_progClouds, "lightColor");
			s_clSkyTop = glGetUniformLocation(s_progClouds, "skyColorTop");
			s_clSkyBottom = glGetUniformLocation(s_progClouds, "skyColorBottom");
			s_clCloudTop = glGetUniformLocation(s_progClouds, "cloudColorTop");
			s_clCloudBottom = glGetUniformLocation(s_progClouds, "cloudColorBottom");
			s_clSampCloud = glGetUniformLocation(s_progClouds, "cloud");
			s_clSampWorley = glGetUniformLocation(s_progClouds, "worley32");
			s_clSampWeather = glGetUniformLocation(s_progClouds, "weatherTex");
			s_clCoverage = glGetUniformLocation(s_progClouds, "coverage_multiplier");
			s_clSpeed = glGetUniformLocation(s_progClouds, "cloudSpeed");
			s_clCrisp = glGetUniformLocation(s_progClouds, "crispiness");
			s_clCurl = glGetUniformLocation(s_progClouds, "curliness");
			s_clAbsorb = glGetUniformLocation(s_progClouds, "absorption");
			s_clDensity = glGetUniformLocation(s_progClouds, "densityFactor");
			s_clEarthR = glGetUniformLocation(s_progClouds, "earthRadius");
			s_clInnerR = glGetUniformLocation(s_progClouds, "sphereInnerRadius");
			s_clOuterR = glGetUniformLocation(s_progClouds, "sphereOuterRadius");
			s_clGlare = glGetUniformLocation(s_progClouds, "glareColor");
		}

		// Composite program (textured FSQ vertex + cloud blend fragment).
		{
			const GLuint vert = SkyDyn_CompileShader(GL_VERTEX_SHADER, vertexSourceSkyDynTextured);
			const GLuint frag = SkyDyn_CompileShader(GL_FRAGMENT_SHADER, fragmentSourceSkyDynComposite);
			if (vert != 0 && frag != 0)
			{
				const GLuint prog = glCreateProgram();
				glAttachShader(prog, vert);
				glAttachShader(prog, frag);
				glLinkProgram(prog);
				{
					GLint ok = 0;
					glGetProgramiv(prog, GL_LINK_STATUS, &ok);
					if (!ok)
					{
						char info_log[512];
						glGetProgramInfoLog(prog, sizeof(info_log), NULL, info_log);
						ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: composite program link error: %s\n", info_log);
						glDeleteProgram(prog);
					}
					else
					{
						s_compProg = prog;
						glDetachShader(s_compProg, vert);
						glDetachShader(s_compProg, frag);
					}
				}
			}
			if (vert != 0)
				glDeleteShader(vert);
			if (frag != 0)
				glDeleteShader(frag);
		}

		if (s_compProg != 0)
			s_cpClouds = glGetUniformLocation(s_compProg, "uClouds");

		if (s_progClouds != 0 && s_compProg != 0)
			ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: Tier-B volumetric clouds ready (r_volclouds to enable).\n");
	}

	return true;
}

void GL3_SkyDyn_Shutdown(void)
{
	if (s_dbgProg != 0)
	{
		glDeleteProgram(s_dbgProg);
		s_dbgProg = 0;
	}
	if (s_progPerlin != 0)
	{
		glDeleteProgram(s_progPerlin);
		s_progPerlin = 0;
	}
	if (s_progWorley != 0)
	{
		glDeleteProgram(s_progWorley);
		s_progWorley = 0;
	}
	if (s_progWeather != 0)
	{
		glDeleteProgram(s_progWeather);
		s_progWeather = 0;
	}
	if (s_perlinTex != 0)
	{
		glDeleteTextures(1, &s_perlinTex);
		s_perlinTex = 0;
	}
	if (s_worleyTex != 0)
	{
		glDeleteTextures(1, &s_worleyTex);
		s_worleyTex = 0;
	}
	if (s_weatherTex != 0)
	{
		glDeleteTextures(1, &s_weatherTex);
		s_weatherTex = 0;
	}
	s_uWeatherSeed = s_uWeatherFreq = -1;
	s_dbgWeather = s_dbgNoise = s_dbgErode = s_dbgMode = s_dbgSlice = s_dbgRes = -1;
	s_dbgAnnounced = false;
	s_lastSeed = s_lastFreq = -1.0e30f;

	if (s_progClouds != 0)
	{
		glDeleteProgram(s_progClouds);
		s_progClouds = 0;
	}
	if (s_compProg != 0)
	{
		glDeleteProgram(s_compProg);
		s_compProg = 0;
	}
	if (s_cloudTex != 0)
	{
		glDeleteTextures(1, &s_cloudTex);
		s_cloudTex = 0;
	}
	s_cloudW = s_cloudH = 0;
	s_cloudAnnounced = false;
	s_clRes = s_clTime = s_clInvProj = s_clInvView = s_clCamPos = -1;
	s_clLightDir = s_clLightColor = s_clSkyTop = s_clSkyBottom = -1;
	s_clCloudTop = s_clCloudBottom = -1;
	s_clSampCloud = s_clSampWorley = s_clSampWeather = -1;
	s_clCoverage = s_clSpeed = s_clCrisp = s_clCurl = s_clAbsorb = s_clDensity = -1;
	s_clEarthR = s_clInnerR = s_clOuterR = -1;
	s_clGlare = -1;
	s_cpClouds = -1;

	if (s_skyProg != 0)
	{
		glDeleteProgram(s_skyProg);
		s_skyProg = 0;
	}
	s_uResolution = s_uInvProj = s_uInvView = s_uLightDir = s_uSkyTop = s_uSkyBottom = -1;
	s_uSunColor = s_uMoonDir = s_uMoonColor = s_uNight = s_uTime = -1;
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
	float sunColor[3], moonDir[3], moonColor[3], night;
	float invProj[16], invView[16];
	float width, height;
	int preset;

	if (s_skyProg == 0)
		return;

	preset = (r_sky_preset != NULL) ? (int)r_sky_preset->value : 2;
	if (preset < 0 || preset > 5)
		preset = 2;
	if (preset == 2)
		preset = 0; // auto tracks day until Phase 6 sun-cycle
	SkyDyn_Preset(preset, top, bottom, sunDir, sunColor, moonDir, moonColor, &night);

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
	// Preset sun is Y-up canonical; H2 world is Z-up: swizzle (x, z, y).
	glUniform3f(s_uLightDir, sunDir[0], sunDir[2], sunDir[1]);
	glUniform3f(s_uSkyTop, top[0], top[1], top[2]);
	glUniform3f(s_uSkyBottom, bottom[0], bottom[1], bottom[2]);
	glUniform3f(s_uSunColor, sunColor[0], sunColor[1], sunColor[2]);
	glUniform3f(s_uMoonDir, moonDir[0], moonDir[2], moonDir[1]);
	glUniform3f(s_uMoonColor, moonColor[0], moonColor[1], moonColor[2]);
	glUniform1f(s_uNight, night);
	glUniform1f(s_uTime, r_newrefdef.time);

	// Caller keeps depth test on + writes off + cull/clip off; FSQ at far
	// depth loses to all opaque geometry via LEQUAL, like legacy sky quads.
	glBindVertexArray(gl3state.vaoFSQ);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GL3_SkyDyn_DrawClouds(void)
{
	float top[3], bottom[3], sunDir[3];
	float sunColor[3], moonDir[3], moonColor[3], night;
	float invProj[16], invView[16];
	float fw, fh, res;
	int cw, ch, preset;
	GLboolean blend_was_enabled;
	GLint blend_src, blend_dst;
	qboolean isNight;

	// Gates: cvars + GL 4.3 + programs + noise. Runs in the reflection pass too:
	// r_origin / r_world_matrix / r_projection_matrix are mirrored there
	// (R_RenderReflection), so rays reconstruct correctly for the mirror view.
	if (!GL3_SkyDyn_VolumetricEnabled())
		return;
	if (!GLAD_GL_VERSION_4_3 || s_progClouds == 0 || s_compProg == 0)
		return;
	if (s_perlinTex == 0 || s_worleyTex == 0 || s_weatherTex == 0)
		return;

	// Cloud buffer size follows the framebuffer, scaled by r_cloud_res.
	fw = (gl3state.fbo_width > 0) ? (float)gl3state.fbo_width : (float)viddef.width;
	fh = (gl3state.fbo_height > 0) ? (float)gl3state.fbo_height : (float)viddef.height;
	res = (r_cloud_res != NULL) ? r_cloud_res->value : 1.0f;
	if (res < 0.1f)
		res = 0.1f;
	if (res > 1.0f)
		res = 1.0f;
	cw = (int)(fw * res);
	ch = (int)(fh * res);
	if (cw < 1)
		cw = 1;
	if (ch < 1)
		ch = 1;

	if (s_cloudTex == 0 || cw != s_cloudW || ch != s_cloudH)
	{
		if (s_cloudTex != 0)
			glDeleteTextures(1, &s_cloudTex);
		s_cloudTex = 0;
		s_cloudW = s_cloudH = 0;
		glGenTextures(1, &s_cloudTex);
		glBindTexture(GL_TEXTURE_2D, s_cloudTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, cw, ch, 0, GL_RGBA, GL_FLOAT, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		s_cloudW = cw;
		s_cloudH = ch;
		s_cloudAnnounced = false;
	}

	preset = (r_sky_preset != NULL) ? (int)r_sky_preset->value : 2;
	if (preset < 0 || preset > 5)
		preset = 2;
	if (preset == 2)
		preset = 0; // auto tracks day until Phase 6 sun-cycle
	SkyDyn_Preset(preset, top, bottom, sunDir, sunColor, moonDir, moonColor, &night);
	isNight = (preset == 3) ? true : false;

	if (!SkyDyn_InvertMatrix(r_projection_matrix, invProj))
		return;
	if (!SkyDyn_InvertMatrix(r_world_matrix, invView))
		return;

	// --- Raymarch (compute) ---
	// At night the moon is the light: direction + cool dim light + dark
	// ambient cloud colors; Tier-A already shows the moon disc + stars.
	GL3_UseShader(s_progClouds);
	glUniform2f(s_clRes, (float)cw, (float)ch);
	glUniform1f(s_clTime, r_newrefdef.time);
	glUniformMatrix4fv(s_clInvProj, 1, GL_FALSE, invProj);
	glUniformMatrix4fv(s_clInvView, 1, GL_FALSE, invView);
	// H2 world is Z-up; shader dome math is Y-up: swizzle camera (x, z, y).
	// (Ray dirs are swizzled in-shader; sun/preset stays Y-up canonical.)
	glUniform3f(s_clCamPos, r_origin[0], r_origin[2], r_origin[1]);
	if (isNight)
		glUniform3f(s_clLightDir, moonDir[0], moonDir[1], moonDir[2]);
	else
		glUniform3f(s_clLightDir, sunDir[0], sunDir[1], sunDir[2]);
	if (isNight)
		glUniform3f(s_clLightColor, 0.45f, 0.55f, 0.8f);
	else if (preset == 4)
		glUniform3f(s_clLightColor, 1.0f, 0.93f, 0.82f); // morning: soft pale gold
	else if (preset == 5)
		glUniform3f(s_clLightColor, 1.0f, 0.75f, 0.55f); // evening: low warm amber
	else
		glUniform3f(s_clLightColor, 1.0f, 1.0f, 230.0f / 255.0f);
	glUniform3f(s_clSkyTop, top[0], top[1], top[2]);
	glUniform3f(s_clSkyBottom, bottom[0], bottom[1], bottom[2]);
	if (isNight)
	{
		glUniform3f(s_clCloudTop, 0.10f, 0.12f, 0.20f);
		glUniform3f(s_clCloudBottom, 0.04f, 0.05f, 0.09f);
		glUniform3f(s_clGlare, 0.35f, 0.42f, 0.55f);
	}
	else if (preset == 4)
	{
		glUniform3f(s_clCloudTop, 169.0f * 1.5f / 255.0f, 149.0f * 1.5f / 255.0f, 149.0f * 1.5f / 255.0f);
		glUniform3f(s_clCloudBottom, 65.0f * 1.5f / 255.0f, 70.0f * 1.5f / 255.0f, 80.0f * 1.5f / 255.0f);
		glUniform3f(s_clGlare, 0.9f, 0.65f, 0.4f); // morning pale gold
	}
	else if (preset == 5)
	{
		glUniform3f(s_clCloudTop, 169.0f * 1.5f / 255.0f, 149.0f * 1.5f / 255.0f, 149.0f * 1.5f / 255.0f);
		glUniform3f(s_clCloudBottom, 65.0f * 1.5f / 255.0f, 70.0f * 1.5f / 255.0f, 80.0f * 1.5f / 255.0f);
		glUniform3f(s_clGlare, 1.0f, 0.45f, 0.2f); // evening deep orange
	}
	else
	{
		glUniform3f(s_clCloudTop, 169.0f * 1.5f / 255.0f, 149.0f * 1.5f / 255.0f, 149.0f * 1.5f / 255.0f);
		glUniform3f(s_clCloudBottom, 65.0f * 1.5f / 255.0f, 70.0f * 1.5f / 255.0f, 80.0f * 1.5f / 255.0f);
		glUniform3f(s_clGlare, 0.8f, 0.32f, 0.16f); // == old hardcoded 0.8*(1,.4,.2)
	}
	glUniform1f(s_clCoverage, (r_cloud_coverage != NULL) ? r_cloud_coverage->value : 0.45f);
	glUniform1f(s_clSpeed, (r_cloud_speed != NULL) ? r_cloud_speed->value : 450.0f);
	glUniform1f(s_clCrisp, (r_cloud_crisp != NULL) ? r_cloud_crisp->value : 40.0f);
	glUniform1f(s_clCurl, (r_cloud_curl != NULL) ? r_cloud_curl->value : 0.1f);
	glUniform1f(s_clAbsorb, ((r_cloud_absorb != NULL) ? r_cloud_absorb->value : 0.35f) * 0.01f);
	glUniform1f(s_clDensity, (r_cloud_density != NULL) ? r_cloud_density->value : 0.02f);
	glUniform1f(s_clEarthR, 600000.0f);
	glUniform1f(s_clInnerR, (r_cloud_base != NULL) ? r_cloud_base->value : 3500.0f);
	glUniform1f(s_clOuterR, (r_cloud_thick != NULL) ? r_cloud_thick->value : 2500.0f);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, s_perlinTex);
	glUniform1i(s_clSampCloud, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_3D, s_worleyTex);
	glUniform1i(s_clSampWorley, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, s_weatherTex);
	glUniform1i(s_clSampWeather, 2);

	GL3_BindImageTexture(0, s_cloudTex, GL_FALSE, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((GLuint)((cw + 15) / 16), (GLuint)((ch + 15) / 16), 1);
	GL3_MemoryBarrierCompute();

	// --- Composite (graphics): cloud rgb+alpha over the framebuffer sky ---
	GL3_UseShader(s_compProg);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_cloudTex);
	glUniform1i(s_cpClouds, 0);

	glGetBooleanv(GL_BLEND, &blend_was_enabled);
	glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src);
	glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Caller state: depth test on + writes off + cull/clip off; FSQ at far
	// depth loses to opaque geometry via LEQUAL (free occlusion).
	glBindVertexArray(gl3state.vaoFSQ);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBlendFunc((GLenum)blend_src, (GLenum)blend_dst);
	if (!blend_was_enabled)
		glDisable(GL_BLEND);

	// Restore: unit 0 active, game-texture cache invalidated (raw binds above).
	glActiveTexture(GL_TEXTURE0);
	gl_state.currenttextures[0] = -1;
	gl_state.currenttextures[1] = -1;

	if (!s_cloudAnnounced)
	{
		s_cloudAnnounced = true;
		ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: volumetric clouds on (%dx%d, res=%.2f)\n", cw, ch, res);
	}
}
