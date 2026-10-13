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

#include <SDL3/SDL.h>

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
	r_cloud_freq = ri.Cvar_Get("r_cloud_freq", "0.8", CVAR_ARCHIVE);
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
	}

	s_lastSeed = seed;
	s_lastFreq = freq;

	err = glGetError();
	ri.Con_Printf(PRINT_ALL, "GL3_SkyDyn: noise generated in %ums (perlin=%u worley=%u weather=%u)%s\n",
		(unsigned)(SDL_GetTicks() - t0), s_perlinTex, s_worleyTex, s_weatherTex,
		(err == GL_NO_ERROR) ? "" : " — glGetError() != 0, check driver log");
}

void GL3_SkyDyn_Update(void)
{
	// Regen check is two float compares when nothing changed.
	SkyDyn_GenNoise(false);
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
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// Restore: unit 0 active, game-texture cache invalidated so the next
	// R_Bind/R_BindImage re-binds (it would otherwise skip on stale texnums).
	glActiveTexture(GL_TEXTURE0);
	gl_state.currenttextures[0] = -1;
	gl_state.currenttextures[1] = -1;

	glDepthMask(GL_TRUE);
	return true;
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
	s_lastSeed = s_lastFreq = -1.0e30f;

	if (s_skyProg != 0)
	{
		glDeleteProgram(s_skyProg);
		s_skyProg = 0;
	}
	s_uResolution = s_uInvProj = s_uInvView = s_uLightDir = s_uSkyTop = s_uSkyBottom = -1;
	// Phase 4 will delete cloud FBOs here.
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
