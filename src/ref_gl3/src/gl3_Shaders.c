//
// gl3_Shaders.c
//
// OpenGL 3.3 shader compilation and management.
//

#include "gl3_Shaders.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// Shader source code (embedded).
// ============================================================

// --- 2D shader (UI, HUD, console) ---
static const char* vertexSource2D =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"void main() {\n"
	"    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);\n"
	"    vTexCoord = aTexCoord;\n"
	"    vColor = aColor;\n"
	"}\n";

static const char* fragmentSource2D =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"uniform sampler2D uTexture;\n"
	"uniform vec4 uColor;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 texColor = texture(uTexture, vTexCoord);\n"
	"    FragColor = texColor * vColor * uColor;\n"
	"    if (FragColor.a < 0.01) discard;\n"
	"}\n";

// --- 3D textured shader ---
// #version 400 is required so the tessellation control/evaluation stages below
// are part of core (the renderer's GL context is 4.6 core).
static const char* vertexSource3D =
	"#version 400 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"layout(location = 3) in vec3 aNormal;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"uniform vec4 uClipPlane;\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec3 vViewPos;\n"
	"out vec3 vNormal;\n"
	"void main() {\n"
	"    vec4 viewPos4 = uModelview * vec4(aPos, 1.0);\n"
	"    gl_Position = uProjection * viewPos4;\n"
	"    vViewPos = viewPos4.xyz;\n"
	"    vTexCoord = aTexCoord;\n"
	"    vColor = aColor;\n"
	"    vNormal = mat3(uModelview) * aNormal;\n"
	"    gl_ClipDistance[0] = dot(viewPos4.xyz, uClipPlane.xyz) + uClipPlane.w;\n"
	"}\n";

// Tessellation control stage for shader3D: passes attributes through and sets
// a distance-faded tessellation level (uniform uTessLevel at close range).
static const char* tessControlSource3D =
	"#version 400 core\n"
	"layout(vertices = 3) out;\n"
	"in vec2 vTexCoord[];\n"
	"in vec4 vColor[];\n"
	"in vec3 vViewPos[];\n"
	"in vec3 vNormal[];\n"
	"out vec2 tcTexCoord[];\n"
	"out vec4 tcColor[];\n"
	"out vec3 tcViewPos[];\n"
	"out vec3 tcNormal[];\n"
	"uniform float uTessLevel;\n"
	"uniform vec2 uTessDistance;\n"
	"void main() {\n"
	"    tcTexCoord[gl_InvocationID] = vTexCoord[gl_InvocationID];\n"
	"    tcColor[gl_InvocationID]    = vColor[gl_InvocationID];\n"
	"    tcViewPos[gl_InvocationID]  = vViewPos[gl_InvocationID];\n"
	"    tcNormal[gl_InvocationID]   = vNormal[gl_InvocationID];\n"
	"    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
	"    gl_out[gl_InvocationID].gl_ClipDistance[0] = gl_in[gl_InvocationID].gl_ClipDistance[0];\n"
	"    if (gl_InvocationID == 0) {\n"
	"        float d = (gl_in[0].gl_Position.w + gl_in[1].gl_Position.w + gl_in[2].gl_Position.w) / 3.0;\n"
	"        float fade = clamp((uTessDistance.y - d) / max(uTessDistance.y - uTessDistance.x, 0.001), 0.0, 1.0);\n"
	"        float lod = max(1.0, uTessLevel * fade);\n"
	"        gl_TessLevelOuter[0] = lod;\n"
	"        gl_TessLevelOuter[1] = lod;\n"
	"        gl_TessLevelOuter[2] = lod;\n"
	"        gl_TessLevelInner[0] = lod;\n"
	"    }\n"
	"}\n";

// Tessellation evaluation stage for shader3D: Phong (curved-point-normal)
// tessellation blended by uTessAlpha, plus optional luminance-driven
// displacement along the surface normal scaled by uTessDisp.
static const char* tessEvalSource3D =
	"#version 400 core\n"
	"layout(triangles, fractional_even_spacing, cw) in;\n"
	"in vec2 tcTexCoord[];\n"
	"in vec4 tcColor[];\n"
	"in vec3 tcViewPos[];\n"
	"in vec3 tcNormal[];\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec3 vViewPos;\n"
	"uniform mat4 uProjection;\n"
	"uniform float uTessAlpha;\n"
	"uniform float uTessDisp;\n"
	"uniform sampler2D uTexture;\n"
	"void main() {\n"
	"    vec3 b = gl_TessCoord.xyz;\n"
	"    vec3 p0 = tcViewPos[0], p1 = tcViewPos[1], p2 = tcViewPos[2];\n"
	"    vec3 n0 = normalize(tcNormal[0]);\n"
	"    vec3 n1 = normalize(tcNormal[1]);\n"
	"    vec3 n2 = normalize(tcNormal[2]);\n"
	"    vec3 P = b.x * p0 + b.y * p1 + b.z * p2;\n"
	"    vec3 q0 = P - n0 * dot(n0, P - p0);\n"
	"    vec3 q1 = P - n1 * dot(n1, P - p1);\n"
	"    vec3 q2 = P - n2 * dot(n2, P - p2);\n"
	"    vec3 Pphong = b.x * q0 + b.y * q1 + b.z * q2;\n"
	"    vec3 viewPos = mix(P, Pphong, uTessAlpha);\n"
	"    vec2 uv = b.x * tcTexCoord[0] + b.y * tcTexCoord[1] + b.z * tcTexCoord[2];\n"
	"    vec3 N = normalize(b.x * n0 + b.y * n1 + b.z * n2);\n"
	"    if (uTessDisp != 0.0) {\n"
	"        float h = dot(texture(uTexture, uv).rgb, vec3(0.299, 0.587, 0.114)) - 0.5;\n"
	"        viewPos += N * (h * uTessDisp);\n"
	"    }\n"
	"    gl_Position = uProjection * vec4(viewPos, 1.0);\n"
	"    gl_ClipDistance[0] = b.x * gl_in[0].gl_ClipDistance[0] + b.y * gl_in[1].gl_ClipDistance[0] + b.z * gl_in[2].gl_ClipDistance[0];\n"
	"    vTexCoord = uv;\n"
	"    vColor = b.x * tcColor[0] + b.y * tcColor[1] + b.z * tcColor[2];\n"
	"    vViewPos = viewPos;\n"
	"}\n";

static const char* fragmentSource3D =
	"#version 400 core\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"in vec3 vViewPos;\n"
	"uniform sampler2D uTexture;\n"
	"uniform vec4 uColor;\n"
	"uniform int  uNumDlights;\n"
	"uniform vec4 uDlightPosRad[8];\n"	// xyz = view-space pos, w = intensity (radius)
	"uniform vec4 uDlightColor[8];\n"	// xyz = rgb (0..1), w = unused
	"uniform float uBumpScale;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 base = texture(uTexture, vTexCoord) * vColor * uColor;\n"
	"    if (base.a < 0.01) discard;\n"
	"    vec3 dlightSum = vec3(0.0);\n"
	"    for (int i = 0; i < uNumDlights; i++) {\n"
	"        float dist = length(uDlightPosRad[i].xyz - vViewPos);\n"
	"        float atten = max(0.0, (uDlightPosRad[i].w - dist) / 256.0);\n"
	"        dlightSum += uDlightColor[i].rgb * atten;\n"
	"    }\n"
	// Bump mapping: derive a surface normal from the diffuse luminance gradient,
	// then add a directional specular highlight.
	"    float h = dot(base.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    vec3 N = normalize(vec3(-dFdx(h) * uBumpScale * 24.0, -dFdy(h) * uBumpScale * 24.0, 1.0));\n"
	"    vec3 V = normalize(-vViewPos);\n"
	"    vec3 Lv = normalize(vec3(0.3, 0.5, 0.8));\n"
	"    vec3 Hv = normalize(Lv + V);\n"
	"    float spec = pow(max(dot(N, Hv), 0.0), 32.0) * 0.6 * uBumpScale;\n"
	"    vec3 lit = base.rgb + dlightSum + vec3(spec);\n"
	"    FragColor = vec4(lit, base.a);\n"
	"}\n";

// --- 3D color-only shader (no texture) ---
static const char* vertexSource3DColor =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"uniform vec4 uClipPlane;\n"
	"out vec4 vColor;\n"
	"void main() {\n"
	"    vec4 viewPos = uModelview * vec4(aPos, 1.0);\n"
	"    gl_Position = uProjection * viewPos;\n"
	"    vColor = aColor;\n"
	"    gl_ClipDistance[0] = dot(viewPos.xyz, uClipPlane.xyz) + uClipPlane.w;\n"
	"}\n";

static const char* fragmentSource3DColor =
	"#version 330 core\n"
	"in vec4 vColor;\n"
	"uniform vec4 uColor;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    FragColor = vColor * uColor;\n"
	"    if (FragColor.a < 0.01) discard;\n"
	"}\n";

// --- 3D lightmapped shader (world surfaces: diffuse * lightmap) ---
// Vertex layout: pos3 (loc 0) + tc2 (loc 1) + lmtc2 (loc 2) = VERTEXSIZE=7 floats, matching glpoly_t verts[i][0..6].
static const char* vertexSource3DLM =
	"#version 400 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec2 aLMCoord;\n"
	"layout(location = 3) in vec3 aNormal;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"uniform vec4 uClipPlane;\n"
	"uniform int uWorldSpace;\n"
	"out vec2 vTexCoord;\n"
	"out vec2 vLMCoord;\n"
	"out vec3 vViewPos;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vNormal;\n"
	"void main() {\n"
	"    vec4 viewPos = uModelview * vec4(aPos, 1.0);\n"
	"    gl_Position = uProjection * viewPos;\n"
	"    vViewPos = viewPos.xyz;\n"
	// Only the static world is drawn in true world space; brush models are
	// drawn in model space, so park their "world" position far above any
	// water plane to keep caustics off them.
	"    vWorldPos = (uWorldSpace != 0) ? aPos : vec3(aPos.xy, 1.0e9);\n"
	"    vTexCoord = aTexCoord;\n"
	"    vLMCoord = aLMCoord;\n"
	"    vNormal = mat3(uModelview) * aNormal;\n"
	"    gl_ClipDistance[0] = dot(viewPos.xyz, uClipPlane.xyz) + uClipPlane.w;\n"
	"}\n";

// Tessellation control stage for shader3DLightmap (passthrough + tess levels).
static const char* tessControlSource3DLM =
	"#version 400 core\n"
	"layout(vertices = 3) out;\n"
	"in vec2 vTexCoord[];\n"
	"in vec2 vLMCoord[];\n"
	"in vec3 vViewPos[];\n"
	"in vec3 vWorldPos[];\n"
	"in vec3 vNormal[];\n"
	"out vec2 tcTexCoord[];\n"
	"out vec2 tcLMCoord[];\n"
	"out vec3 tcViewPos[];\n"
	"out vec3 tcWorldPos[];\n"
	"out vec3 tcNormal[];\n"
	"uniform float uTessLevel;\n"
	"uniform vec2 uTessDistance;\n"
	"void main() {\n"
	"    tcTexCoord[gl_InvocationID] = vTexCoord[gl_InvocationID];\n"
	"    tcLMCoord[gl_InvocationID]  = vLMCoord[gl_InvocationID];\n"
	"    tcViewPos[gl_InvocationID]  = vViewPos[gl_InvocationID];\n"
	"    tcWorldPos[gl_InvocationID] = vWorldPos[gl_InvocationID];\n"
	"    tcNormal[gl_InvocationID]   = vNormal[gl_InvocationID];\n"
	"    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
	"    gl_out[gl_InvocationID].gl_ClipDistance[0] = gl_in[gl_InvocationID].gl_ClipDistance[0];\n"
	"    if (gl_InvocationID == 0) {\n"
	"        float d = (gl_in[0].gl_Position.w + gl_in[1].gl_Position.w + gl_in[2].gl_Position.w) / 3.0;\n"
	"        float fade = clamp((uTessDistance.y - d) / max(uTessDistance.y - uTessDistance.x, 0.001), 0.0, 1.0);\n"
	"        float lod = max(1.0, uTessLevel * fade);\n"
	"        gl_TessLevelOuter[0] = lod;\n"
	"        gl_TessLevelOuter[1] = lod;\n"
	"        gl_TessLevelOuter[2] = lod;\n"
	"        gl_TessLevelInner[0] = lod;\n"
	"    }\n"
	"}\n";

// Tessellation evaluation stage for shader3DLightmap: Phong tessellation +
// optional luminance-driven displacement (same scheme as shader3D).
static const char* tessEvalSource3DLM =
	"#version 400 core\n"
	"layout(triangles, fractional_even_spacing, cw) in;\n"
	"in vec2 tcTexCoord[];\n"
	"in vec2 tcLMCoord[];\n"
	"in vec3 tcViewPos[];\n"
	"in vec3 tcWorldPos[];\n"
	"in vec3 tcNormal[];\n"
	"out vec2 vTexCoord;\n"
	"out vec2 vLMCoord;\n"
	"out vec3 vViewPos;\n"
	"out vec3 vWorldPos;\n"
	"uniform mat4 uProjection;\n"
	"uniform float uTessAlpha;\n"
	"uniform float uTessDisp;\n"
	"uniform sampler2D uDiffuse;\n"
	"void main() {\n"
	"    vec3 b = gl_TessCoord.xyz;\n"
	"    vec3 p0 = tcViewPos[0], p1 = tcViewPos[1], p2 = tcViewPos[2];\n"
	"    vec3 n0 = normalize(tcNormal[0]);\n"
	"    vec3 n1 = normalize(tcNormal[1]);\n"
	"    vec3 n2 = normalize(tcNormal[2]);\n"
	"    vec3 P = b.x * p0 + b.y * p1 + b.z * p2;\n"
	"    vec3 q0 = P - n0 * dot(n0, P - p0);\n"
	"    vec3 q1 = P - n1 * dot(n1, P - p1);\n"
	"    vec3 q2 = P - n2 * dot(n2, P - p2);\n"
	"    vec3 Pphong = b.x * q0 + b.y * q1 + b.z * q2;\n"
	"    vec3 viewPos = mix(P, Pphong, uTessAlpha);\n"
	"    vec2 uv = b.x * tcTexCoord[0] + b.y * tcTexCoord[1] + b.z * tcTexCoord[2];\n"
	"    vec3 N = normalize(b.x * n0 + b.y * n1 + b.z * n2);\n"
	"    if (uTessDisp != 0.0) {\n"
	"        float h = dot(texture(uDiffuse, uv).rgb, vec3(0.299, 0.587, 0.114)) - 0.5;\n"
	"        viewPos += N * (h * uTessDisp);\n"
	"    }\n"
	"    gl_Position = uProjection * vec4(viewPos, 1.0);\n"
	"    gl_ClipDistance[0] = b.x * gl_in[0].gl_ClipDistance[0] + b.y * gl_in[1].gl_ClipDistance[0] + b.z * gl_in[2].gl_ClipDistance[0];\n"
	"    vTexCoord = uv;\n"
	"    vLMCoord = b.x * tcLMCoord[0] + b.y * tcLMCoord[1] + b.z * tcLMCoord[2];\n"
	"    vViewPos = viewPos;\n"
	"    vWorldPos = b.x * tcWorldPos[0] + b.y * tcWorldPos[1] + b.z * tcWorldPos[2];\n"
	"}\n";

static const char* fragmentSource3DLM =
	"#version 400 core\n"
	"in vec2 vTexCoord;\n"
	"in vec2 vLMCoord;\n"
	"in vec3 vViewPos;\n"
	"in vec3 vWorldPos;\n"
	"uniform sampler2D uDiffuse;\n"
	"uniform sampler2D uLightmap;\n"
	"uniform vec4 uColor;\n"
	"uniform float uBumpScale;\n"
	"uniform float uWaterZ;\n"
	"uniform float uCausticStrength;\n"
	"uniform float uTime;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 diffuse = texture(uDiffuse, vTexCoord);\n"
	"    vec4 lm = texture(uLightmap, vLMCoord);\n"
	// Bump mapping: perturb the surface normal with the diffuse luminance
	// gradient and add a directional specular highlight, masked by the lightmap.
	"    float h = dot(diffuse.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    vec3 N = normalize(vec3(-dFdx(h) * uBumpScale * 24.0, -dFdy(h) * uBumpScale * 24.0, 1.0));\n"
	"    vec3 V = normalize(-vViewPos);\n"
	"    vec3 Lv = normalize(vec3(0.3, 0.5, 0.8));\n"
	"    vec3 Hv = normalize(Lv + V);\n"
	"    float spec = pow(max(dot(N, Hv), 0.0), 32.0) * 0.5 * uBumpScale;\n"
	"    vec3 lit = diffuse.rgb * lm.rgb + lm.rgb * spec;\n"
	// Underwater caustics: animated light patterns on submerged world geometry.
	"    float water_depth = uWaterZ - vWorldPos.z;\n"
	"    if (uCausticStrength > 0.0 && water_depth > 0.0) {\n"
	"        vec2 cc = vWorldPos.xy * 0.06 + vec2(uTime * 0.05, uTime * 0.037);\n"
	"        float c = sin(cc.x * 2.1 + uTime * 1.3) * sin(cc.y * 1.7 - uTime * 1.1);\n"
	"        c += 0.6 * sin((cc.x + cc.y) * 1.3 + uTime * 0.9);\n"
	"        c += 0.4 * cos((cc.x - cc.y) * 1.9 - uTime * 0.7);\n"
	"        c = pow(max(c * 0.5 + 0.5, 0.0), 2.5);\n"
	"        float atten = 1.0 / (1.0 + water_depth * 0.01);\n"
	"        lit += lit * c * uCausticStrength * atten;\n"
	"    }\n"
	"    FragColor = vec4(lit, diffuse.a) * uColor;\n"
	"    if (FragColor.a < 0.01) discard;\n"
	"}\n";

// --- Post-process shader (HDR composite + exposure) ---
// Vertex layout: vec2 pos (NDC) + vec2 texcoord = 4 floats per vertex.
static const char* vertexSourcePost =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"out vec2 vTexCoord;\n"
	"void main() {\n"
	"    gl_Position = vec4(aPos, 0.0, 1.0);\n"
	"    vTexCoord = aTexCoord;\n"
	"}\n";

static const char* fragmentSourcePost =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uHDRBuffer;\n"
	"uniform sampler2D uBloomBuffer;\n"
	"uniform sampler2D uAOBuffer;\n"
	"uniform sampler2D uDepthMap;\n"
	"uniform float uExposure;\n"
	"uniform mat3  uColorProfile;\n"
	"uniform float uBloomStrength;\n"
	"uniform float uAOStrength;\n"
	"uniform int  uFogEnabled;\n"
	"uniform int  uFogMode;\n"
	"uniform vec3 uFogColor;\n"
	"uniform float uFogDensity;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform vec2 uFogNearFar;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec3  hdr   = texture(uHDRBuffer,   vTexCoord).rgb;\n"
	"    vec3  bloom = texture(uBloomBuffer, vTexCoord).rgb;\n"
	"    float ao    = mix(1.0, texture(uAOBuffer, vTexCoord).r, uAOStrength);\n"
	"    vec3  color = hdr * ao + bloom * uBloomStrength;\n"
	"    if (uFogEnabled != 0) {\n"
	"        float depth_ndc = texture(uDepthMap, vTexCoord).r * 2.0 - 1.0;\n"
	"        float near = uFogNearFar.x;\n"
	"        float far  = uFogNearFar.y;\n"
	"        float linearDist = (2.0 * near * far) / (far + near - depth_ndc * (far - near));\n"
	"        float fogFactor = 1.0;\n"
	"        if (uFogMode == 0) {\n"
	"            fogFactor = clamp((uFogEnd - linearDist) / (uFogEnd - uFogStart), 0.0, 1.0);\n"
	"        } else if (uFogMode == 1) {\n"
	"            fogFactor = exp(-uFogDensity * 0.5 * linearDist);\n"
	"        } else {\n"
	"            float d = uFogDensity * 0.5 * linearDist;\n"
	"            fogFactor = exp(-d * d);\n"
	"        }\n"
	"        color = mix(uFogColor, color, fogFactor);\n"
	"    }\n"
	"    FragColor = vec4(uColorProfile * (color * uExposure), 1.0);\n"
	"}\n";

// --- Bloom bright-pass extract shader ---
// Outputs pixels whose perceived luminance exceeds uThreshold, scaled by a smooth knee.
static const char* fragmentSourceBloomExtract =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uHDRBuffer;\n"
	"uniform float uThreshold;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec3  color      = texture(uHDRBuffer, vTexCoord).rgb;\n"
	"    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
	"    float rcp        = 1.0 / max(1.0 - uThreshold, 0.0001);\n"
	"    float weight     = clamp((brightness - uThreshold) * rcp, 0.0, 1.0);\n"
	"    FragColor = vec4(color * weight, 1.0);\n"
	"}\n";

// --- Bloom separable Gaussian blur shader (9-tap, horizontal or vertical) ---
static const char* fragmentSourceBloomBlur =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uImage;\n"
	"uniform bool      uHorizontal;\n"
	"uniform vec2      uTexelSize;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
	"    vec2 offset = uHorizontal ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);\n"
	"    vec3 result = texture(uImage, vTexCoord).rgb * weight[0];\n"
	"    for (int i = 1; i < 5; i++) {\n"
	"        result += texture(uImage, vTexCoord + offset * float(i)).rgb * weight[i];\n"
	"        result += texture(uImage, vTexCoord - offset * float(i)).rgb * weight[i];\n"
	"    }\n"
	"    FragColor = vec4(result, 1.0);\n"
	"}\n";

// --- SSAO shader: reconstructs view-space position from depth and samples a hemisphere kernel ---
static const char* fragmentSourceSSAO =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uDepthMap;\n"
	"uniform sampler2D uNoiseTex;\n"
	"uniform vec3  uKernel[16];\n"
	"uniform vec4  uProjParams;\n"			// x=P[0], y=P[5], z=P[10], w=P[14]
	"uniform float uRadius;\n"
	"uniform float uBias;\n"
	"uniform vec2  uScreenSize;\n"
	"out float FragColor;\n"
	"vec3 ReconstructViewPos(vec2 tc) {\n"
	"    float depth  = texture(uDepthMap, tc).r;\n"
	"    float ndc_z  = depth * 2.0 - 1.0;\n"
	"    float view_z = -uProjParams.w / (ndc_z + uProjParams.z);\n"
	"    float view_x = (tc.x * 2.0 - 1.0) * (-view_z) / uProjParams.x;\n"
	"    float view_y = (tc.y * 2.0 - 1.0) * (-view_z) / uProjParams.y;\n"
	"    return vec3(view_x, view_y, view_z);\n"
	"}\n"
	"void main() {\n"
	"    float depth = texture(uDepthMap, vTexCoord).r;\n"
	"    if (depth >= 0.9999) { FragColor = 1.0; return; }\n"
	"    vec3 fragPos  = ReconstructViewPos(vTexCoord);\n"
	// Best-neighbor normal: pick the neighbor pair with the smaller depth difference
	// to avoid discontinuity artifacts at geometry edges.
	"    vec2 ts = 1.0 / uScreenSize;\n"
	"    vec3 pr = ReconstructViewPos(vTexCoord + vec2(ts.x,  0.0));\n"
	"    vec3 pl = ReconstructViewPos(vTexCoord - vec2(ts.x,  0.0));\n"
	"    vec3 pu = ReconstructViewPos(vTexCoord + vec2(0.0,  ts.y));\n"
	"    vec3 pd = ReconstructViewPos(vTexCoord - vec2(0.0,  ts.y));\n"
	"    vec3 dx = (abs(pr.z - fragPos.z) < abs(pl.z - fragPos.z)) ? pr - fragPos : fragPos - pl;\n"
	"    vec3 dy = (abs(pu.z - fragPos.z) < abs(pd.z - fragPos.z)) ? pu - fragPos : fragPos - pd;\n"
	"    vec3 N  = normalize(cross(dx, dy));\n"
	// Camera-facing check: flip N if it points away from the viewer in view-space.
	"    if (dot(N, -normalize(fragPos)) < 0.0) N = -N;\n"
	"    vec2 noiseScale = uScreenSize / 4.0;\n"
	"    vec3 randomVec  = normalize(texture(uNoiseTex, vTexCoord * noiseScale).rgb);\n"
	// Gram-Schmidt with degeneracy guard: skip if randomVec is nearly parallel to N.
	"    vec3 tangent = randomVec - N * dot(randomVec, N);\n"
	"    if (length(tangent) < 0.001) { FragColor = 1.0; return; }\n"
	"    tangent        = normalize(tangent);\n"
	"    vec3 bitangent = cross(N, tangent);\n"
	"    mat3 TBN       = mat3(tangent, bitangent, N);\n"
	"    float occlusion = 0.0;\n"
	"    for (int i = 0; i < 16; i++) {\n"
	"        vec3 samplePos = fragPos + TBN * uKernel[i] * uRadius;\n"
	// Skip samples that landed behind the camera (view-space z >= 0).
	"        if (samplePos.z >= 0.0) continue;\n"
	"        float rcp_w   = -1.0 / samplePos.z;\n"
	"        vec2  sampleTC = clamp(vec2(\n"
	"            samplePos.x * uProjParams.x * rcp_w * 0.5 + 0.5,\n"
	"            samplePos.y * uProjParams.y * rcp_w * 0.5 + 0.5), 0.0, 1.0);\n"
	"        vec3  scenePos   = ReconstructViewPos(sampleTC);\n"
	// Range check: linear falloff within uRadius; avoids division by near-zero.
	"        float rangeCheck = smoothstep(0.0, 1.0, 1.0 - abs(fragPos.z - scenePos.z) / uRadius);\n"
	"        occlusion += (scenePos.z >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;\n"
	"    }\n"
	"    FragColor = 1.0 - (occlusion / 16.0);\n"
	"}\n";

// --- SSAO box blur (4x4, 16-tap) ---
static const char* fragmentSourceSSAOBlur =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uSSAOInput;\n"
	"uniform vec2 uTexelSize;\n"
	"out float FragColor;\n"
	"void main() {\n"
	"    float result = 0.0;\n"
	"    for (int x = 0; x < 4; x++)\n"
	"        for (int y = 0; y < 4; y++)\n"
	"            result += texture(uSSAOInput, vTexCoord + (vec2(float(x), float(y)) - 1.5) * uTexelSize).r;\n"
	"    FragColor = result / 16.0;\n"
	"}\n";

// --- Water surface shader (9-float vertex layout: pos3+tc2+col4, Gerstner waves + reflection + refraction) ---
static const char* vertexSourceWater =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"uniform vec4 uClipPlane;\n"
	"uniform float uTime;\n"
	"uniform float uWaveHeight;\n"
	"uniform float uWaveSpeed;\n"
	"uniform float uWaveSharp;\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec4 vClipPos;\n"
	"out vec3 vViewPos;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vNormal;\n"
	"// Gerstner wave for a Z-up world: the wave plane is XY, displacement is along Z.\n"
	"// P += (Q*A*D*cos(phase), A*sin(phase)) horizontally+vertically. The tangent and\n"
	"// binormal are accumulated via the analytic partial derivatives so the surface\n"
	"// normal can be rebuilt after the vertices are displaced (GPU Gems formulation).\n"
	"vec3 gerstnerWave(vec2 dir, float w, float amp, float speed, float q, float t, float hscale, vec3 p, inout vec3 tangent, inout vec3 binormal) {\n"
	"    float phase = w * dot(dir, p.xy) + speed * t;\n"
	"    float s = sin(phase);\n"
	"    float c = cos(phase);\n"
	"    float qA = q * amp * hscale;   // horizontal displacement magnitude\n"
	"    float wqA = w * qA;            // horizontal derivative factor\n"
	"    float wA = w * amp * hscale;   // vertical derivative factor\n"
	"    tangent  += vec3(-dir.x * dir.x * wqA * s, -dir.x * dir.y * wqA * s, dir.x * wA * c);\n"
	"    binormal += vec3(-dir.x * dir.y * wqA * s, -dir.y * dir.y * wqA * s, dir.y * wA * c);\n"
	"    return vec3(dir.x * qA * c, dir.y * qA * c, amp * hscale * s);\n"
	"}\n"
	"void main() {\n"
	"    vec3 worldPos = aPos;\n"
	"    vec3 tangent = vec3(1.0, 0.0, 0.0);\n"
	"    vec3 binormal = vec3(0.0, 1.0, 0.0);\n"
	"    float t = uTime * uWaveSpeed;\n"
	"    vec3 disp = vec3(0.0);\n"
	"    disp += gerstnerWave(normalize(vec2( 1.0,  0.3)), 0.0314, 1.5, 1.0, uWaveSharp, t, uWaveHeight, worldPos, tangent, binormal);\n"
	"    disp += gerstnerWave(normalize(vec2( 0.7, -0.7)), 0.048, 1.2, 1.3, uWaveSharp, t, uWaveHeight, worldPos, tangent, binormal);\n"
	"    disp += gerstnerWave(normalize(vec2(-0.4,  0.9)), 0.09,  0.8, 1.7, uWaveSharp, t, uWaveHeight, worldPos, tangent, binormal);\n"
	"    disp += gerstnerWave(normalize(vec2( 0.2,  1.0)), 0.157, 0.4, 2.1, uWaveSharp, t, uWaveHeight, worldPos, tangent, binormal);\n"
	"    worldPos += disp;\n"
	"    vNormal = normalize(cross(tangent, binormal));\n"
	"    vWorldPos = worldPos;\n"
	"    vec4 viewPos = uModelview * vec4(worldPos, 1.0);\n"
	"    gl_Position = uProjection * viewPos;\n"
	"    vClipPos    = gl_Position;\n"
	"    vTexCoord   = aTexCoord;\n"
	"    vColor      = aColor;\n"
	"    vViewPos    = viewPos.xyz;\n"
	"    gl_ClipDistance[0] = dot(viewPos.xyz, uClipPlane.xyz) + uClipPlane.w;\n"
	"}\n";

static const char* fragmentSourceWater =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"in vec4 vClipPos;\n"
	"in vec3 vViewPos;\n"
	"in vec3 vWorldPos;\n"
	"in vec3 vNormal;\n"
	"uniform sampler2D uTexture;\n"
	"uniform sampler2D uReflectTex;\n"
	"uniform sampler2D uRefractTex;\n"
	"uniform vec4  uColor;\n"
	"uniform float uReflectAmt;\n"
	"uniform float uRefractAmt;\n"
	"uniform float uTime;\n"
	"uniform float uBumpScale;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 waterColor = texture(uTexture, vTexCoord) * vColor * uColor;\n"
	"    if (waterColor.a < 0.01) discard;\n"
	"    vec2 screenUV = vClipPos.xy / vClipPos.w * 0.5 + 0.5;\n"
	"    vec3 N = normalize(vNormal);\n"
	"    // Refraction: sample the scene (without water) with a small UV offset\n"
	"    // that follows the displaced surface normal.\n"
	"    vec3 blended = waterColor.rgb;\n"
	"    if (uRefractAmt > 0.001) {\n"
	"        vec2 refractUV = clamp(screenUV + N.xy * uRefractAmt, 0.0, 1.0);\n"
	"        refractUV.y = 1.0 - refractUV.y;\n"
	"        vec3 refractColor = texture(uRefractTex, refractUV).rgb;\n"
	"        blended = mix(refractColor, waterColor.rgb, 0.35);\n"
	"    }\n"
	"    // Reflection: projective lookup with normal distortion and a fresnel term.\n"
	"    vec2 reflectUV = clamp(screenUV + N.xy * 0.05, 0.0, 1.0);\n"
	"    reflectUV.y = 1.0 - reflectUV.y;\n"
	"    vec3 reflectColor = texture(uReflectTex, reflectUV).rgb;\n"
	"    vec3 V = normalize(-vViewPos);\n"
	"    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.6 + 0.4;\n"
	"    blended = mix(blended, reflectColor, clamp(uReflectAmt * fresnel, 0.0, 1.0));\n"
	"    // Specular highlight (Blinn-Phong).\n"
	"    vec3 L = normalize(vec3(0.3, 0.6, 0.7));\n"
	"    vec3 Hv = normalize(L + V);\n"
	"    float spec = pow(max(dot(N, Hv), 0.0), 96.0) * uBumpScale;\n"
	"    blended += vec3(spec);\n"
	"    FragColor = vec4(blended, waterColor.a);\n"
	"}\n";

static GLuint CompileShader(GLenum type, const char* source)
{
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		char info_log[512];
		glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3 Shader compile error: %s\n", info_log);
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

static GLuint CreateProgram(const char* vert_src, const char* frag_src)
{
	const GLuint vert = CompileShader(GL_VERTEX_SHADER, vert_src);
	if (vert == 0)
		return 0;

	const GLuint frag = CompileShader(GL_FRAGMENT_SHADER, frag_src);
	if (frag == 0)
	{
		glDeleteShader(vert);
		return 0;
	}

	const GLuint program = glCreateProgram();
	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glLinkProgram(program);

	GLint success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success)
	{
		char info_log[512];
		glGetProgramInfoLog(program, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3 Program link error: %s\n", info_log);
		glDeleteProgram(program);
		glDeleteShader(vert);
		glDeleteShader(frag);
		return 0;
	}

	// Shaders can be detached/deleted after linking.
	glDetachShader(program, vert);
	glDetachShader(program, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	return program;
}

// Link a program that includes tessellation control + evaluation stages.
static GLuint CreateProgramTess(const char* vert_src, const char* tesc_src, const char* tese_src, const char* frag_src)
{
	GLuint stages[4] = { 0, 0, 0, 0 };
	const GLenum types[4] = { GL_VERTEX_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER, GL_FRAGMENT_SHADER };
	const char* sources[4] = { vert_src, tesc_src, tese_src, frag_src };

	GLuint program = glCreateProgram();

	for (int i = 0; i < 4; i++)
	{
		stages[i] = CompileShader(types[i], sources[i]);
		if (stages[i] == 0)
		{
			for (int j = 0; j < 4; j++)
				if (stages[j] != 0)
					glDeleteShader(stages[j]);
			glDeleteProgram(program);
			return 0;
		}
		glAttachShader(program, stages[i]);
	}

	glLinkProgram(program);

	GLint success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success)
	{
		char info_log[512];
		glGetProgramInfoLog(program, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3 Tess program link error: %s\n", info_log);
		glDeleteProgram(program);
		program = 0;
	}

	// Shaders can be detached/deleted after linking.
	for (int i = 0; i < 4; i++)
	{
		glDetachShader(program, stages[i]);
		glDeleteShader(stages[i]);
	}

	return program;
}

// ============================================================
// Public API.
// ============================================================

static GLuint currentProgram = 0;

void GL3_UseShader(const GLuint program)
{
	if (currentProgram != program)
	{
		glUseProgram(program);
		currentProgram = program;
	}
}

qboolean GL3_InitShaders(void)
{
	// --- 2D shader ---
	gl3state.shader2D = CreateProgram(vertexSource2D, fragmentSource2D);
	if (gl3state.shader2D == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 2D shader program\n");
		return false;
	}

	gl3state.uni2D_projection = glGetUniformLocation(gl3state.shader2D, "uProjection");
	gl3state.uni2D_texture = glGetUniformLocation(gl3state.shader2D, "uTexture");
	gl3state.uni2D_color = glGetUniformLocation(gl3state.shader2D, "uColor");

	// --- 3D textured shader (no tessellation; all non-patch draws) ---
	gl3state.shader3D = CreateProgram(vertexSource3D, fragmentSource3D);
	if (gl3state.shader3D == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D shader program\n");
		return false;
	}

	gl3state.uni3D_projection = glGetUniformLocation(gl3state.shader3D, "uProjection");
	gl3state.uni3D_modelview = glGetUniformLocation(gl3state.shader3D, "uModelview");
	gl3state.uni3D_texture = glGetUniformLocation(gl3state.shader3D, "uTexture");
	gl3state.uni3D_color = glGetUniformLocation(gl3state.shader3D, "uColor");

	gl3state.uni3D_numDlights   = glGetUniformLocation(gl3state.shader3D, "uNumDlights");
	gl3state.uni3D_dlightPosRad = glGetUniformLocation(gl3state.shader3D, "uDlightPosRad");
	gl3state.uni3D_dlightColor  = glGetUniformLocation(gl3state.shader3D, "uDlightColor");
	gl3state.uni3D_clipPlane    = glGetUniformLocation(gl3state.shader3D, "uClipPlane");
	gl3state.uni3D_bumpScale    = glGetUniformLocation(gl3state.shader3D, "uBumpScale");

	// --- 3D textured shader WITH tessellation stages (drawn as GL_PATCHES). ---
	gl3state.shader3DTess = CreateProgramTess(vertexSource3D, tessControlSource3D, tessEvalSource3D, fragmentSource3D);
	if (gl3state.shader3DTess == 0)
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D tessellation shader program\n");

	if (gl3state.shader3DTess != 0)
	{
		gl3state.uni3DT_projection   = glGetUniformLocation(gl3state.shader3DTess, "uProjection");
		gl3state.uni3DT_modelview    = glGetUniformLocation(gl3state.shader3DTess, "uModelview");
		gl3state.uni3DT_texture      = glGetUniformLocation(gl3state.shader3DTess, "uTexture");
		gl3state.uni3DT_color        = glGetUniformLocation(gl3state.shader3DTess, "uColor");
		gl3state.uni3DT_numDlights   = glGetUniformLocation(gl3state.shader3DTess, "uNumDlights");
		gl3state.uni3DT_dlightPosRad = glGetUniformLocation(gl3state.shader3DTess, "uDlightPosRad");
		gl3state.uni3DT_dlightColor  = glGetUniformLocation(gl3state.shader3DTess, "uDlightColor");
		gl3state.uni3DT_clipPlane    = glGetUniformLocation(gl3state.shader3DTess, "uClipPlane");
		gl3state.uni3DT_bumpScale    = glGetUniformLocation(gl3state.shader3DTess, "uBumpScale");
		gl3state.uni3DT_tessLevel    = glGetUniformLocation(gl3state.shader3DTess, "uTessLevel");
		gl3state.uni3DT_tessAlpha    = glGetUniformLocation(gl3state.shader3DTess, "uTessAlpha");
		gl3state.uni3DT_tessDisp     = glGetUniformLocation(gl3state.shader3DTess, "uTessDisp");
		gl3state.uni3DT_tessDistance = glGetUniformLocation(gl3state.shader3DTess, "uTessDistance");
	}

	// --- 3D color-only shader ---
	gl3state.shader3DColor = CreateProgram(vertexSource3DColor, fragmentSource3DColor);
	if (gl3state.shader3DColor == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D color shader program\n");
		return false;
	}

	gl3state.uni3DColor_projection = glGetUniformLocation(gl3state.shader3DColor, "uProjection");
	gl3state.uni3DColor_modelview = glGetUniformLocation(gl3state.shader3DColor, "uModelview");
	gl3state.uni3DColor_color = glGetUniformLocation(gl3state.shader3DColor, "uColor");
	gl3state.uni3DColor_clipPlane = glGetUniformLocation(gl3state.shader3DColor, "uClipPlane");

	// --- 3D lightmapped shader (no tessellation; world surfaces) ---
	gl3state.shader3DLightmap = CreateProgram(vertexSource3DLM, fragmentSource3DLM);
	if (gl3state.shader3DLightmap == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D lightmap shader program\n");
		return false;
	}

	gl3state.uni3DLM_projection = glGetUniformLocation(gl3state.shader3DLightmap, "uProjection");
	gl3state.uni3DLM_modelview  = glGetUniformLocation(gl3state.shader3DLightmap, "uModelview");
	gl3state.uni3DLM_diffuse    = glGetUniformLocation(gl3state.shader3DLightmap, "uDiffuse");
	gl3state.uni3DLM_lightmap   = glGetUniformLocation(gl3state.shader3DLightmap, "uLightmap");
	gl3state.uni3DLM_color      = glGetUniformLocation(gl3state.shader3DLightmap, "uColor");
	gl3state.uni3DLM_clipPlane  = glGetUniformLocation(gl3state.shader3DLightmap, "uClipPlane");
	gl3state.uni3DLM_bumpScale  = glGetUniformLocation(gl3state.shader3DLightmap, "uBumpScale");
	gl3state.uni3DLM_worldSpace = glGetUniformLocation(gl3state.shader3DLightmap, "uWorldSpace");
	gl3state.uni3DLM_waterZ     = glGetUniformLocation(gl3state.shader3DLightmap, "uWaterZ");
	gl3state.uni3DLM_caustic    = glGetUniformLocation(gl3state.shader3DLightmap, "uCausticStrength");
	gl3state.uni3DLM_time       = glGetUniformLocation(gl3state.shader3DLightmap, "uTime");

	// --- 3D lightmapped shader WITH tessellation stages (drawn as GL_PATCHES). ---
	gl3state.shader3DLMTess = CreateProgramTess(vertexSource3DLM, tessControlSource3DLM, tessEvalSource3DLM, fragmentSource3DLM);
	if (gl3state.shader3DLMTess == 0)
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D lightmap tessellation shader program\n");

	if (gl3state.shader3DLMTess != 0)
	{
		gl3state.uni3DLMT_projection   = glGetUniformLocation(gl3state.shader3DLMTess, "uProjection");
		gl3state.uni3DLMT_modelview    = glGetUniformLocation(gl3state.shader3DLMTess, "uModelview");
		gl3state.uni3DLMT_diffuse      = glGetUniformLocation(gl3state.shader3DLMTess, "uDiffuse");
		gl3state.uni3DLMT_lightmap     = glGetUniformLocation(gl3state.shader3DLMTess, "uLightmap");
		gl3state.uni3DLMT_color        = glGetUniformLocation(gl3state.shader3DLMTess, "uColor");
		gl3state.uni3DLMT_clipPlane    = glGetUniformLocation(gl3state.shader3DLMTess, "uClipPlane");
		gl3state.uni3DLMT_bumpScale    = glGetUniformLocation(gl3state.shader3DLMTess, "uBumpScale");
		gl3state.uni3DLMT_worldSpace   = glGetUniformLocation(gl3state.shader3DLMTess, "uWorldSpace");
		gl3state.uni3DLMT_waterZ       = glGetUniformLocation(gl3state.shader3DLMTess, "uWaterZ");
		gl3state.uni3DLMT_caustic      = glGetUniformLocation(gl3state.shader3DLMTess, "uCausticStrength");
		gl3state.uni3DLMT_time         = glGetUniformLocation(gl3state.shader3DLMTess, "uTime");
		gl3state.uni3DLMT_tessLevel    = glGetUniformLocation(gl3state.shader3DLMTess, "uTessLevel");
		gl3state.uni3DLMT_tessAlpha    = glGetUniformLocation(gl3state.shader3DLMTess, "uTessAlpha");
		gl3state.uni3DLMT_tessDisp     = glGetUniformLocation(gl3state.shader3DLMTess, "uTessDisp");
		gl3state.uni3DLMT_tessDistance = glGetUniformLocation(gl3state.shader3DLMTess, "uTessDistance");
	}

	// Bind sampler units once
	GL3_UseShader(gl3state.shader2D);
	glUniform1i(gl3state.uni2D_texture, 0);
	glUniform4f(gl3state.uni2D_color, 1.0f, 1.0f, 1.0f, 1.0f);

	GL3_UseShader(gl3state.shader3D);
	glUniform1i(gl3state.uni3D_texture, 0);
	glUniform4f(gl3state.uni3D_color, 1.0f, 1.0f, 1.0f, 1.0f);
	glUniform1i(gl3state.uni3D_numDlights, 0);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform1i(gl3state.uni3DT_texture, 0);
		glUniform4f(gl3state.uni3DT_color, 1.0f, 1.0f, 1.0f, 1.0f);
		glUniform1i(gl3state.uni3DT_numDlights, 0);
		glUniform1f(gl3state.uni3DT_tessLevel, 1.0f);
		glUniform1f(gl3state.uni3DT_tessAlpha, 0.75f);
		glUniform1f(gl3state.uni3DT_tessDisp, 0.0f);
		glUniform2f(gl3state.uni3DT_tessDistance, 768.0f, 3072.0f);
	}

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform1i(gl3state.uni3DLM_diffuse, 0);
	glUniform1i(gl3state.uni3DLM_lightmap, 1);
	glUniform4f(gl3state.uni3DLM_color, 1.0f, 1.0f, 1.0f, 1.0f);
	glUniform1i(gl3state.uni3DLM_worldSpace, 1);
	glUniform1f(gl3state.uni3DLM_waterZ, -1.0e9f);
	glUniform1f(gl3state.uni3DLM_caustic, 0.0f);
	glUniform1f(gl3state.uni3DLM_time, 0.0f);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniform1i(gl3state.uni3DLMT_diffuse, 0);
		glUniform1i(gl3state.uni3DLMT_lightmap, 1);
		glUniform4f(gl3state.uni3DLMT_color, 1.0f, 1.0f, 1.0f, 1.0f);
		glUniform1i(gl3state.uni3DLMT_worldSpace, 1);
		glUniform1f(gl3state.uni3DLMT_waterZ, -1.0e9f);
		glUniform1f(gl3state.uni3DLMT_caustic, 0.0f);
		glUniform1f(gl3state.uni3DLMT_time, 0.0f);
		glUniform1f(gl3state.uni3DLMT_tessLevel, 1.0f);
		glUniform1f(gl3state.uni3DLMT_tessAlpha, 0.75f);
		glUniform1f(gl3state.uni3DLMT_tessDisp, 0.0f);
		glUniform2f(gl3state.uni3DLMT_tessDistance, 768.0f, 3072.0f);
	}

	// --- Create shared 2D VAO/VBO ---
	glGenVertexArrays(1, &gl3state.vao2D);
	glGenBuffers(1, &gl3state.vbo2D);

	glBindVertexArray(gl3state.vao2D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo2D);

	// 2D vertex layout: vec2 pos, vec2 texcoord, vec4 color = 8 floats per vertex.
	const GLsizei stride2D = 8 * sizeof(float);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride2D, (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride2D, (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride2D, (void*)(4 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	// --- Create lightmapped world VAO/VBO (VERTEXSIZE=7 floats: pos3+tc2+lmtc2) ---
	glGenVertexArrays(1, &gl3state.vao3DLM);
	glGenBuffers(1, &gl3state.vbo3DLM);

	glBindVertexArray(gl3state.vao3DLM);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLM);

	const GLsizei strideLM = 7 * sizeof(float);

	// pos3 (location 0).
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideLM, (void*)0);
	glEnableVertexAttribArray(0);

	// tc2 diffuse (location 1).
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, strideLM, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// tc2 lightmap (location 2).
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideLM, (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	// --- Create generic 3D VAO/VBO (9 floats/vert: pos3+tc2+col4) ---
	glGenVertexArrays(1, &gl3state.vao3D);
	glGenBuffers(1, &gl3state.vbo3D);

	glBindVertexArray(gl3state.vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);

	const GLsizei stride3D = 9 * sizeof(float);

	// pos3 (location 0).
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride3D, (void*)0);
	glEnableVertexAttribArray(0);

	// tc2 (location 1).
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride3D, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// col4 (location 2).
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride3D, (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	// --- Create tessellation-ready 3D VAO/VBO (12 floats/vert: pos3+tc2+col4+nrm3) ---
	// Used only when r_tessellation is enabled; keeps the 9-float path untouched.
	glGenVertexArrays(1, &gl3state.vao3DT);
	glGenBuffers(1, &gl3state.vbo3DT);

	glBindVertexArray(gl3state.vao3DT);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DT);

	const GLsizei stride3DT = 12 * sizeof(float);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride3DT, (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride3DT, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride3DT, (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride3DT, (void*)(9 * sizeof(float)));
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);

	// --- Create tessellation-ready lightmap VAO/VBO (10 floats/vert: pos3+tc2+lmtc2+nrm3) ---
	glGenVertexArrays(1, &gl3state.vao3DLMT);
	glGenBuffers(1, &gl3state.vbo3DLMT);

	glBindVertexArray(gl3state.vao3DLMT);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLMT);

	const GLsizei strideLMT = 10 * sizeof(float);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideLMT, (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, strideLMT, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideLMT, (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, strideLMT, (void*)(7 * sizeof(float)));
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);

	// Tessellation operates on triangle patches.
	glPatchParameteri(GL_PATCH_VERTICES, 3);

	// --- Create full-screen quad VAO/VBO (4 floats/vert: NDC pos2 + tc2) ---
	{
		static const float fsq_verts[4 * 4] = {
			-1.0f,  1.0f,  0.0f, 1.0f,   // top-left
			-1.0f, -1.0f,  0.0f, 0.0f,   // bottom-left
			 1.0f,  1.0f,  1.0f, 1.0f,   // top-right
			 1.0f, -1.0f,  1.0f, 0.0f    // bottom-right
		};

		glGenVertexArrays(1, &gl3state.vaoFSQ);
		glGenBuffers(1, &gl3state.vboFSQ);

		glBindVertexArray(gl3state.vaoFSQ);
		glBindBuffer(GL_ARRAY_BUFFER, gl3state.vboFSQ);
		glBufferData(GL_ARRAY_BUFFER, sizeof(fsq_verts), fsq_verts, GL_STATIC_DRAW);

		const GLsizei strideFSQ = 4 * sizeof(float);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, strideFSQ, (void*)0);
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, strideFSQ, (void*)(2 * sizeof(float)));
		glEnableVertexAttribArray(1);

		glBindVertexArray(0);
	}

	// --- Post-process shader ---
	gl3state.shaderPost = CreateProgram(vertexSourcePost, fragmentSourcePost);
	if (gl3state.shaderPost == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create post-process shader program\n");
		return false;
	}

	gl3state.uniPost_hdrBuffer = glGetUniformLocation(gl3state.shaderPost, "uHDRBuffer");
	gl3state.uniPost_exposure  = glGetUniformLocation(gl3state.shaderPost, "uExposure");
	gl3state.uniPost_colorProfile = glGetUniformLocation(gl3state.shaderPost, "uColorProfile");

	gl3state.uniPost_depthMap    = glGetUniformLocation(gl3state.shaderPost, "uDepthMap");
	gl3state.uniPost_fogEnabled  = glGetUniformLocation(gl3state.shaderPost, "uFogEnabled");
	gl3state.uniPost_fogMode     = glGetUniformLocation(gl3state.shaderPost, "uFogMode");
	gl3state.uniPost_fogColor    = glGetUniformLocation(gl3state.shaderPost, "uFogColor");
	gl3state.uniPost_fogDensity  = glGetUniformLocation(gl3state.shaderPost, "uFogDensity");
	gl3state.uniPost_fogStart    = glGetUniformLocation(gl3state.shaderPost, "uFogStart");
	gl3state.uniPost_fogEnd      = glGetUniformLocation(gl3state.shaderPost, "uFogEnd");
	gl3state.uniPost_fogNearFar  = glGetUniformLocation(gl3state.shaderPost, "uFogNearFar");

	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_hdrBuffer, 0);
	glUniform1f(gl3state.uniPost_exposure, 1.0f);
	glUniform1i(gl3state.uniPost_depthMap, 3);
	glUniform1i(gl3state.uniPost_fogEnabled, 0);

	// Default to the sRGB profile (identity) until GL3_CompositeHDR applies the real one.
	{
		static const GLfloat identity[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		glUniformMatrix3fv(gl3state.uniPost_colorProfile, 1, GL_TRUE, identity);
	}

	// --- Create 1x1 white texture for color-only 2D drawing ---
	{
		const GLubyte white_pixel[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &gl3state.whiteTexture);
		glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// --- Water surface shader ---
	gl3state.shaderWater = CreateProgram(vertexSourceWater, fragmentSourceWater);
	if (gl3state.shaderWater == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create water shader program\n");
		return false;
	}

	gl3state.uniWater_projection = glGetUniformLocation(gl3state.shaderWater, "uProjection");
	gl3state.uniWater_modelview  = glGetUniformLocation(gl3state.shaderWater, "uModelview");
	gl3state.uniWater_color      = glGetUniformLocation(gl3state.shaderWater, "uColor");
	gl3state.uniWater_reflectTex = glGetUniformLocation(gl3state.shaderWater, "uReflectTex");
	gl3state.uniWater_reflectAmt = glGetUniformLocation(gl3state.shaderWater, "uReflectAmt");
	gl3state.uniWater_refractTex = glGetUniformLocation(gl3state.shaderWater, "uRefractTex");
	gl3state.uniWater_refractAmt = glGetUniformLocation(gl3state.shaderWater, "uRefractAmt");
	gl3state.uniWater_time       = glGetUniformLocation(gl3state.shaderWater, "uTime");
	gl3state.uniWater_clipPlane  = glGetUniformLocation(gl3state.shaderWater, "uClipPlane");
	gl3state.uniWater_bumpScale  = glGetUniformLocation(gl3state.shaderWater, "uBumpScale");
	gl3state.uniWater_waveHeight = glGetUniformLocation(gl3state.shaderWater, "uWaveHeight");
	gl3state.uniWater_waveSpeed  = glGetUniformLocation(gl3state.shaderWater, "uWaveSpeed");
	gl3state.uniWater_waveSharp  = glGetUniformLocation(gl3state.shaderWater, "uWaveSharp");

	GL3_UseShader(gl3state.shaderWater);
	glUniform1i(gl3state.uniWater_reflectTex, 1);      // TMU1: reflection texture.
	glUniform1i(gl3state.uniWater_refractTex, 2);      // TMU2: refraction texture.
	glUniform4f(gl3state.uniWater_color, 1.0f, 1.0f, 1.0f, 1.0f);
	glUniform1f(gl3state.uniWater_reflectAmt, 0.35f);
	glUniform1f(gl3state.uniWater_refractAmt, 0.5f);
	glUniform1f(gl3state.uniWater_time, 0.0f);
	glUniform1f(gl3state.uniWater_waveHeight, 0.6f);
	glUniform1f(gl3state.uniWater_waveSpeed, 1.0f);
	glUniform1f(gl3state.uniWater_waveSharp, 0.5f);

	// Neutral clip plane + default bump strength on all 3D programs.
	{
		const float neutral_plane[4] = { 0.0f, 0.0f, 1.0f, 1.0e6f };
		GL3_UpdateClipPlane(neutral_plane);
		GL3_UpdateBumpScale(1.0f, 1.0f);
	}

	ri.Con_Printf(PRINT_ALL, "GL3 shaders initialized.\n");

	return true;
}

void GL3_ShutdownShaders(void)
{
	if (gl3state.vao2D != 0) { glDeleteVertexArrays(1, &gl3state.vao2D); gl3state.vao2D = 0; }
	if (gl3state.vbo2D != 0) { glDeleteBuffers(1, &gl3state.vbo2D); gl3state.vbo2D = 0; }

	if (gl3state.vao3DLM != 0) { glDeleteVertexArrays(1, &gl3state.vao3DLM); gl3state.vao3DLM = 0; }
	if (gl3state.vbo3DLM != 0) { glDeleteBuffers(1, &gl3state.vbo3DLM); gl3state.vbo3DLM = 0; }

	if (gl3state.vao3D != 0) { glDeleteVertexArrays(1, &gl3state.vao3D); gl3state.vao3D = 0; }
	if (gl3state.vbo3D != 0) { glDeleteBuffers(1, &gl3state.vbo3D); gl3state.vbo3D = 0; }

	if (gl3state.vao3DT != 0) { glDeleteVertexArrays(1, &gl3state.vao3DT); gl3state.vao3DT = 0; }
	if (gl3state.vbo3DT != 0) { glDeleteBuffers(1, &gl3state.vbo3DT); gl3state.vbo3DT = 0; }

	if (gl3state.vao3DLMT != 0) { glDeleteVertexArrays(1, &gl3state.vao3DLMT); gl3state.vao3DLMT = 0; }
	if (gl3state.vbo3DLMT != 0) { glDeleteBuffers(1, &gl3state.vbo3DLMT); gl3state.vbo3DLMT = 0; }

	if (gl3state.vaoFSQ != 0) { glDeleteVertexArrays(1, &gl3state.vaoFSQ); gl3state.vaoFSQ = 0; }
	if (gl3state.vboFSQ != 0) { glDeleteBuffers(1, &gl3state.vboFSQ); gl3state.vboFSQ = 0; }

	if (gl3state.shader2D != 0) { glDeleteProgram(gl3state.shader2D); gl3state.shader2D = 0; }
	if (gl3state.shader3D != 0) { glDeleteProgram(gl3state.shader3D); gl3state.shader3D = 0; }
	if (gl3state.shader3DTess != 0) { glDeleteProgram(gl3state.shader3DTess); gl3state.shader3DTess = 0; }
	if (gl3state.shader3DColor != 0) { glDeleteProgram(gl3state.shader3DColor); gl3state.shader3DColor = 0; }
	if (gl3state.shader3DLightmap != 0) { glDeleteProgram(gl3state.shader3DLightmap); gl3state.shader3DLightmap = 0; }
	if (gl3state.shader3DLMTess != 0) { glDeleteProgram(gl3state.shader3DLMTess); gl3state.shader3DLMTess = 0; }

	if (gl3state.shaderPost != 0) { glDeleteProgram(gl3state.shaderPost); gl3state.shaderPost = 0; }
	if (gl3state.shaderWater != 0) { glDeleteProgram(gl3state.shaderWater); gl3state.shaderWater = 0; }

	if (gl3state.whiteTexture != 0) { glDeleteTextures(1, &gl3state.whiteTexture); gl3state.whiteTexture = 0; }

	currentProgram = 0;
}

// ============================================================
// Matrix helpers.
// ============================================================

void GL3_UpdateProjection2D(const float width, const float height)
{
	// Orthographic projection: left=0, right=width, top=0, bottom=height, near=-99999, far=99999.
	const float l = 0.0f;
	const float r = width;
	const float t = 0.0f;
	const float b = height;
	const float n = -99999.0f;
	const float f = 99999.0f;

	const float proj[16] = {
		2.0f / (r - l),       0.0f,                 0.0f,                0.0f,
		0.0f,                 2.0f / (t - b),       0.0f,                0.0f,
		0.0f,                 0.0f,                 -2.0f / (f - n),     0.0f,
		-(r + l) / (r - l),   -(t + b) / (t - b),   -(f + n) / (f - n), 1.0f
	};

	GL3_UseShader(gl3state.shader2D);
	glUniformMatrix4fv(gl3state.uni2D_projection, 1, GL_FALSE, proj);
}

void GL3_UpdateProjection3D(const float fov_y, const float aspect, const float znear, const float zfar)
{
	const float f = 1.0f / tanf(fov_y * (float)M_PI / 360.0f);

	const float proj[16] = {
		f / aspect, 0.0f, 0.0f,                                  0.0f,
		0.0f,       f,    0.0f,                                  0.0f,
		0.0f,       0.0f, (zfar + znear) / (znear - zfar),      -1.0f,
		0.0f,       0.0f, (2.0f * zfar * znear) / (znear - zfar), 0.0f
	};

	GL3_UseShader(gl3state.shader3D);
	glUniformMatrix4fv(gl3state.uni3D_projection, 1, GL_FALSE, proj);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniformMatrix4fv(gl3state.uni3DT_projection, 1, GL_FALSE, proj);
	}

	GL3_UseShader(gl3state.shader3DColor);
	glUniformMatrix4fv(gl3state.uni3DColor_projection, 1, GL_FALSE, proj);

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniformMatrix4fv(gl3state.uni3DLM_projection, 1, GL_FALSE, proj);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniformMatrix4fv(gl3state.uni3DLMT_projection, 1, GL_FALSE, proj);
	}

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniformMatrix4fv(gl3state.uniWater_projection, 1, GL_FALSE, proj);
	}

	// Store projection matrix for software queries.
	memcpy(r_projection_matrix, proj, sizeof(proj));

	// Cache projection parameters for SSAO view-space reconstruction.
	gl3state.projParams[0] = proj[0];   // P[0]  = f / aspect
	gl3state.projParams[1] = proj[5];   // P[5]  = f
	gl3state.projParams[2] = proj[10];  // P[10] = (zfar+znear)/(znear-zfar)
	gl3state.projParams[3] = proj[14];  // P[14] = 2*zfar*znear/(znear-zfar)
}

void GL3_UpdateModelview3D(const float* matrix4x4)
{
	GL3_UseShader(gl3state.shader3D);
	glUniformMatrix4fv(gl3state.uni3D_modelview, 1, GL_FALSE, matrix4x4);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniformMatrix4fv(gl3state.uni3DT_modelview, 1, GL_FALSE, matrix4x4);
	}

	GL3_UseShader(gl3state.shader3DColor);
	glUniformMatrix4fv(gl3state.uni3DColor_modelview, 1, GL_FALSE, matrix4x4);

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniformMatrix4fv(gl3state.uni3DLM_modelview, 1, GL_FALSE, matrix4x4);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniformMatrix4fv(gl3state.uni3DLMT_modelview, 1, GL_FALSE, matrix4x4);
	}

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniformMatrix4fv(gl3state.uniWater_modelview, 1, GL_FALSE, matrix4x4);
	}
}

void GL3_UpdateModelviewLM(const float* matrix4x4)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glUniformMatrix4fv(gl3state.uni3DLM_modelview, 1, GL_FALSE, matrix4x4);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniformMatrix4fv(gl3state.uni3DLMT_modelview, 1, GL_FALSE, matrix4x4);
	}
}

void GL3_SetLMColor(const float r, const float g, const float b, const float a)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform4f(gl3state.uni3DLM_color, r, g, b, a);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniform4f(gl3state.uni3DLMT_color, r, g, b, a);
	}
}

void GL3_Set3DColor(const float r, const float g, const float b, const float a)
{
	GL3_UseShader(gl3state.shader3D);
	glUniform4f(gl3state.uni3D_color, r, g, b, a);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform4f(gl3state.uni3DT_color, r, g, b, a);
	}

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniform4f(gl3state.uniWater_color, r, g, b, a);
	}
}

// Set the view-space clip plane used by the reflection pass. Pass a neutral
// plane (0,0,1,large) to effectively disable clipping.
void GL3_UpdateClipPlane(const float plane[4])
{
	GL3_UseShader(gl3state.shader3D);
	glUniform4fv(gl3state.uni3D_clipPlane, 1, plane);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform4fv(gl3state.uni3DT_clipPlane, 1, plane);
	}

	GL3_UseShader(gl3state.shader3DColor);
	glUniform4fv(gl3state.uni3DColor_clipPlane, 1, plane);

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform4fv(gl3state.uni3DLM_clipPlane, 1, plane);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniform4fv(gl3state.uni3DLMT_clipPlane, 1, plane);
	}

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniform4fv(gl3state.uniWater_clipPlane, 1, plane);
	}
}

// Set the bump-map (normal perturbation + specular) strength. World surfaces
// (models + lightmapped world) use `world_scale`, the water surface uses
// `water_scale`.
void GL3_UpdateBumpScale(const float world_scale, const float water_scale)
{
	GL3_UseShader(gl3state.shader3D);
	glUniform1f(gl3state.uni3D_bumpScale, world_scale);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform1f(gl3state.uni3DT_bumpScale, world_scale);
	}

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform1f(gl3state.uni3DLM_bumpScale, world_scale);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniform1f(gl3state.uni3DLMT_bumpScale, world_scale);
	}

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniform1f(gl3state.uniWater_bumpScale, water_scale);
	}
}

// Flag whether the geometry about to be drawn by shader3DLightmap is in true
// world space (static world) or in model space (brush models).
void GL3_SetWorldSpace(const int world_space)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform1i(gl3state.uni3DLM_worldSpace, world_space ? 1 : 0);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniform1i(gl3state.uni3DLMT_worldSpace, world_space ? 1 : 0);
	}
}

// Configure underwater caustics for world surfaces. `water_z` is the world-space
// Z of the nearest horizontal water plane; pass a very large value to disable.
void GL3_UpdateCaustics(const float water_z, const float strength, const float time)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform1f(gl3state.uni3DLM_waterZ, water_z);
	glUniform1f(gl3state.uni3DLM_caustic, strength);
	glUniform1f(gl3state.uni3DLM_time, time);

	if (gl3state.shader3DLMTess != 0)
	{
		GL3_UseShader(gl3state.shader3DLMTess);
		glUniform1f(gl3state.uni3DLMT_waterZ, water_z);
		glUniform1f(gl3state.uni3DLMT_caustic, strength);
		glUniform1f(gl3state.uni3DLMT_time, time);
	}
}

// ============================================================
// Dynamic polygon drawing helpers.
// ============================================================

// Growable scratch buffer used to expand primitives into tessellated patch
// vertices. Only touched when tessellation is enabled.
static float* s_tess_scratch = NULL;
static int    s_tess_scratch_cap = 0;

static float* TessScratch(const int floats_needed)
{
	if (floats_needed > s_tess_scratch_cap)
	{
		int cap = 4096;
		while (cap < floats_needed)
			cap *= 2;

		float* p = (float*)realloc(s_tess_scratch, cap * sizeof(float));
		if (p == NULL)
			return NULL;

		s_tess_scratch = p;
		s_tess_scratch_cap = cap;
	}

	return s_tess_scratch;
}

static int TessEnabled(void)
{
	return (r_tessellation != NULL && (int)r_tessellation->value > 0);
}

static int TessFactor(void)
{
	const int v = (int)r_tessellation->value;
	return (v < 1) ? 1 : (v + 1);
}

static qboolean IsTriangleMode(const GLenum mode)
{
	return (mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN);
}

// Compute a normalized face normal for triangle (a,b,c). Points are 9-float
// (3D) or 7-float (LM) vertices, so we read the leading position only.
static void FaceNormal(const float* a, const float* b, const float* c, float* out)
{
	const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
	const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
	float nx = uy * vz - uz * vy;
	float ny = uz * vx - ux * vz;
	float nz = ux * vy - uy * vx;
	const float len = sqrtf(nx * nx + ny * ny + nz * nz);

	if (len > 1.0e-8f)
	{
		nx /= len; ny /= len; nz /= len;
	}
	else
	{
		nx = 0.0f; ny = 0.0f; nz = 1.0f;
	}

	out[0] = nx; out[1] = ny; out[2] = nz;
}

// Draw a polygon using shader3DLightmap (VERTEXSIZE=7 floats/vert: pos3+tc2+lmtc2).
// When tessellation is enabled, the fan is expanded into triangle patches with
// per-face normals and drawn as GL_PATCHES.
void GL3_DrawLMPoly(const float* verts, const int numverts)
{
	if (!TessEnabled() || gl3state.shader3DLMTess == 0 || numverts < 3)
	{
		GL3_UseShader(gl3state.shader3DLightmap);
		glBindVertexArray(gl3state.vao3DLM);
		glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLM);
		glBufferData(GL_ARRAY_BUFFER, numverts * 7 * sizeof(float), verts, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLE_FAN, 0, numverts);
		return;
	}

	const int num_tris = numverts - 2;
	const int out_verts = num_tris * 3;
	float* out = TessScratch(out_verts * 10);

	if (out == NULL)
	{
		GL3_UseShader(gl3state.shader3DLightmap);
		glBindVertexArray(gl3state.vao3DLM);
		glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLM);
		glBufferData(GL_ARRAY_BUFFER, numverts * 7 * sizeof(float), verts, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLE_FAN, 0, numverts);
		return;
	}

	for (int t = 0; t < num_tris; t++)
	{
		const int i0 = 0, i1 = t + 1, i2 = t + 2;
		const float* a = &verts[i0 * 7];
		const float* b = &verts[i1 * 7];
		const float* c = &verts[i2 * 7];

		float fn[3];
		FaceNormal(a, b, c, fn);

		float* o = out + t * 3 * 10;

		memcpy(o + 0, a, 7 * sizeof(float)); memcpy(o + 7, fn, 3 * sizeof(float));
		memcpy(o + 10, b, 7 * sizeof(float)); memcpy(o + 17, fn, 3 * sizeof(float));
		memcpy(o + 20, c, 7 * sizeof(float)); memcpy(o + 27, fn, 3 * sizeof(float));
	}

	GL3_UseShader(gl3state.shader3DLMTess);
	glUniform1f(gl3state.uni3DLMT_tessLevel, (float)TessFactor());
	glUniform1f(gl3state.uni3DLMT_tessDisp, r_tessellation_disp->value);
	glBindVertexArray(gl3state.vao3DLMT);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLMT);
	glBufferData(GL_ARRAY_BUFFER, out_verts * 10 * sizeof(float), out, GL_STREAM_DRAW);
	glDrawArrays(GL_PATCHES, 0, out_verts);
}

// Draw a polygon using shader3D (9 floats/vert: pos3+tc2+col4).
void GL3_Draw3DPoly(const GLenum mode, const float* verts, const int numverts)
{
	GL3_Draw3DPolyN(mode, verts, NULL, numverts);
}

// Same as GL3_Draw3DPoly but with an optional per-vertex normal array (3
// floats/vert) used when tessellation is enabled. Only honored for GL_TRIANGLES.
void GL3_Draw3DPolyN(const GLenum mode, const float* verts, const float* normals, const int numverts)
{
	if (!TessEnabled() || gl3state.shader3DTess == 0 || !IsTriangleMode(mode) || numverts < 3)
	{
		GL3_UseShader(gl3state.shader3D);
		glBindVertexArray(gl3state.vao3D);
		glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);
		glBufferData(GL_ARRAY_BUFFER, numverts * 9 * sizeof(float), verts, GL_STREAM_DRAW);
		glDrawArrays(mode, 0, numverts);
		return;
	}

	const int num_tris = (mode == GL_TRIANGLES) ? (numverts / 3) : (numverts - 2);
	const int out_verts = num_tris * 3;
	float* out = TessScratch(out_verts * 12);

	if (out == NULL)
	{
		GL3_UseShader(gl3state.shader3D);
		glBindVertexArray(gl3state.vao3D);
		glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);
		glBufferData(GL_ARRAY_BUFFER, numverts * 9 * sizeof(float), verts, GL_STREAM_DRAW);
		glDrawArrays(mode, 0, numverts);
		return;
	}

	const qboolean use_supplied = (normals != NULL && mode == GL_TRIANGLES);

	for (int t = 0; t < num_tris; t++)
	{
		int i0, i1, i2;

		if (mode == GL_TRIANGLES)
		{
			i0 = t * 3; i1 = t * 3 + 1; i2 = t * 3 + 2;
		}
		else if (mode == GL_TRIANGLE_FAN)
		{
			i0 = 0; i1 = t + 1; i2 = t + 2;
		}
		else
		{
			if (t % 2 == 0) { i0 = t; i1 = t + 1; i2 = t + 2; }
			else            { i0 = t + 1; i1 = t; i2 = t + 2; }
		}

		const float* a = &verts[i0 * 9];
		const float* b = &verts[i1 * 9];
		const float* c = &verts[i2 * 9];

		float fn[3];
		FaceNormal(a, b, c, fn);

		const float* n0 = use_supplied ? &normals[i0 * 3] : fn;
		const float* n1 = use_supplied ? &normals[i1 * 3] : fn;
		const float* n2 = use_supplied ? &normals[i2 * 3] : fn;

		float* o = out + t * 3 * 12;

		memcpy(o + 0, a, 9 * sizeof(float));  memcpy(o + 9, n0, 3 * sizeof(float));
		memcpy(o + 12, b, 9 * sizeof(float)); memcpy(o + 21, n1, 3 * sizeof(float));
		memcpy(o + 24, c, 9 * sizeof(float)); memcpy(o + 33, n2, 3 * sizeof(float));
	}

	GL3_UseShader(gl3state.shader3DTess);
	glUniform1f(gl3state.uni3DT_tessLevel, (float)TessFactor());
	glUniform1f(gl3state.uni3DT_tessDisp, r_tessellation_disp->value);
	glBindVertexArray(gl3state.vao3DT);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DT);
	glBufferData(GL_ARRAY_BUFFER, out_verts * 12 * sizeof(float), out, GL_STREAM_DRAW);
	glDrawArrays(GL_PATCHES, 0, out_verts);
}

// Copy the current scene color into the refraction FBO.
// Called before water surfaces are drawn so the refraction texture
// contains the scene without water. Preserves the current draw framebuffer.
void GL3_CopySceneToRefract(void)
{
	if (gl3state.fboRefract == 0 || gl3state.fbo3D == 0)
		return;

	GLint cur_read_fb = 0, cur_draw_fb = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &cur_read_fb);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &cur_draw_fb);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, gl3state.fbo3D);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl3state.fboRefract);
	glBlitFramebuffer(0, 0, gl3state.fbo_width, gl3state.fbo_height,
	                  0, 0, gl3state.refract_width, gl3state.refract_height,
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)cur_read_fb);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)cur_draw_fb);
}

// Draw a water polygon using shaderWater (same 9 floats/vert as GL3_Draw3DPoly).
// Binds the reflection texture to TMU1 and refraction texture to TMU2;
// TMU0 must already be bound to the water diffuse. reflect_valid selects
// whether this frame's planar reflection is sampled (0 disables reflection
// but keeps the Gerstner waves and refraction).
void GL3_DrawWaterPoly(const GLenum mode, const float* verts, const int numverts, const int reflect_valid)
{
	GL3_UseShader(gl3state.shaderWater);

	glUniform1f(gl3state.uniWater_time, r_newrefdef.time);
	const float reflect_strength = (reflect_valid && gl3state.fboTexReflect != 0)
		? r_reflections_intensity->value : 0.0f;
	glUniform1f(gl3state.uniWater_reflectAmt, reflect_strength);
	const float refract_strength = ((int)r_refractions->value && gl3state.fboTexRefract != 0)
		? r_refractions_intensity->value : 0.0f;
	glUniform1f(gl3state.uniWater_refractAmt, refract_strength);
	glUniform1f(gl3state.uniWater_waveHeight, r_water_wave_height->value);
	glUniform1f(gl3state.uniWater_waveSpeed, r_water_wave_speed->value);
	glUniform1f(gl3state.uniWater_waveSharp, r_water_wave_sharp->value);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexReflect);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexRefract);
	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(gl3state.vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);
	glBufferData(GL_ARRAY_BUFFER, numverts * 9 * sizeof(float), verts, GL_STREAM_DRAW);
	glDrawArrays(mode, 0, numverts);
}

// ============================================================
// Fog configuration helpers.
// ============================================================

void GL3_SetFog(const int enabled, const int mode, const float r, const float g, const float b, const float density, const float start, const float end)
{
	// Update post-process shader fog uniforms (depth-based fog applied after bloom).
	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_fogEnabled, enabled);
	if (enabled)
	{
		glUniform1i(gl3state.uniPost_fogMode, mode);
		glUniform3f(gl3state.uniPost_fogColor, r, g, b);
		glUniform1f(gl3state.uniPost_fogDensity, density);
		glUniform1f(gl3state.uniPost_fogStart, start);
		glUniform1f(gl3state.uniPost_fogEnd, end);
		glUniform2f(gl3state.uniPost_fogNearFar, 1.0f, end);
	}
}

void GL3_DisableFog(void)
{
	GL3_SetFog(0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

// ============================================================
// FBO management.
// ============================================================

qboolean GL3_InitFBO(const int width, const int height)
{
	glGenFramebuffers(1, &gl3state.fbo3D);
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fbo3D);

	// RGBA16F color texture.
	glGenTextures(1, &gl3state.fboTex3D);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTex3D);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl3state.fboTex3D, 0);

	// Depth-stencil texture (depth samplable by SSAO shader, stencil used by shadow system).
	glGenTextures(1, &gl3state.fboDepth3D);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboDepth3D);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gl3state.fboDepth3D, 0);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitFBO: framebuffer incomplete (status 0x%x)\n", (unsigned)status);
		GL3_ShutdownFBO();
		return false;
	}

	gl3state.fbo_width  = width;
	gl3state.fbo_height = height;

	ri.Con_Printf(PRINT_ALL, "GL3 HDR FBO initialized (%dx%d RGBA16F).\n", width, height);

	return true;
}

void GL3_ShutdownFBO(void)
{
	if (gl3state.fboTex3D   != 0) { glDeleteTextures(1, &gl3state.fboTex3D);         gl3state.fboTex3D   = 0; }
	if (gl3state.fboDepth3D != 0) { glDeleteTextures(1, &gl3state.fboDepth3D);        gl3state.fboDepth3D = 0; }
	if (gl3state.fbo3D      != 0) { glDeleteFramebuffers(1, &gl3state.fbo3D);        gl3state.fbo3D      = 0; }

	gl3state.fbo_width  = 0;
	gl3state.fbo_height = 0;
}

// ============================================================
// Reflection FBO management.
// ============================================================

qboolean GL3_InitReflect(const int width, const int height)
{
	// Full resolution so the planar reflection is sampled 1:1 (crisp mirror).
	gl3state.reflect_width  = (width  > 0) ? width  : 1;
	gl3state.reflect_height = (height > 0) ? height : 1;

	// RGBA16F color texture (sampled by the water shader).
	glGenTextures(1, &gl3state.fboTexReflect);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexReflect);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl3state.reflect_width, gl3state.reflect_height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	// Depth renderbuffer (not sampled; only needed for depth testing during reflection render).
	glGenRenderbuffers(1, &gl3state.rboReflectDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, gl3state.rboReflectDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gl3state.reflect_width, gl3state.reflect_height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &gl3state.fboReflect);
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboReflect);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl3state.fboTexReflect, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl3state.rboReflectDepth);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitReflect: framebuffer incomplete (status 0x%x)\n", (unsigned)status);
		GL3_ShutdownReflect();
		return false;
	}

	ri.Con_Printf(PRINT_ALL, "GL3 reflection FBO initialized (%dx%d RGBA16F).\n", gl3state.reflect_width, gl3state.reflect_height);
	return true;
}

void GL3_ShutdownReflect(void)
{
	if (gl3state.fboTexReflect   != 0) { glDeleteTextures(1,      &gl3state.fboTexReflect);   gl3state.fboTexReflect   = 0; }
	if (gl3state.rboReflectDepth != 0) { glDeleteRenderbuffers(1, &gl3state.rboReflectDepth); gl3state.rboReflectDepth = 0; }
	if (gl3state.fboReflect      != 0) { glDeleteFramebuffers(1,  &gl3state.fboReflect);      gl3state.fboReflect      = 0; }

	gl3state.reflect_width  = 0;
	gl3state.reflect_height = 0;
}

// ============================================================
// Refraction FBO management.
// ============================================================

qboolean GL3_InitRefract(const int width, const int height)
{
	gl3state.refract_width  = (width  > 0) ? width  : 1;
	gl3state.refract_height = (height > 0) ? height : 1;

	glGenTextures(1, &gl3state.fboTexRefract);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexRefract);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl3state.refract_width, gl3state.refract_height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenRenderbuffers(1, &gl3state.rboRefractDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, gl3state.rboRefractDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gl3state.refract_width, gl3state.refract_height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &gl3state.fboRefract);
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboRefract);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl3state.fboTexRefract, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl3state.rboRefractDepth);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitRefract: framebuffer incomplete (status 0x%x)\n", (unsigned)status);
		GL3_ShutdownRefract();
		return false;
	}

	ri.Con_Printf(PRINT_ALL, "GL3 refraction FBO initialized (%dx%d RGBA16F).\n", gl3state.refract_width, gl3state.refract_height);
	return true;
}

void GL3_ShutdownRefract(void)
{
	if (gl3state.fboTexRefract   != 0) { glDeleteTextures(1,      &gl3state.fboTexRefract);   gl3state.fboTexRefract   = 0; }
	if (gl3state.rboRefractDepth != 0) { glDeleteRenderbuffers(1, &gl3state.rboRefractDepth); gl3state.rboRefractDepth = 0; }
	if (gl3state.fboRefract      != 0) { glDeleteFramebuffers(1,  &gl3state.fboRefract);      gl3state.fboRefract      = 0; }

	gl3state.refract_width  = 0;
	gl3state.refract_height = 0;
}

// ============================================================
// Bloom FBO + shader management.
// ============================================================

// Creates a single-color-attachment framebuffer with a GL_RGB16F texture (no depth).
// Returns false on failure; on failure the partially-created resources are left for GL3_ShutdownBloom to clean up.
static qboolean CreateBloomFBO(GLuint* out_fbo, GLuint* out_tex, const int w, const int h)
{
	glGenTextures(1, out_tex);
	glBindTexture(GL_TEXTURE_2D, *out_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, out_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *out_tex, 0);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3 bloom FBO incomplete (status 0x%x)\n", (unsigned)status);
		return false;
	}

	return true;
}

qboolean GL3_InitBloom(const int width, const int height)
{
	gl3state.bloom_width  = width  > 1 ? width  / 2 : 1;
	gl3state.bloom_height = height > 1 ? height / 2 : 1;

	// --- Bloom extract shader ---
	gl3state.shaderBloomExtract = CreateProgram(vertexSourcePost, fragmentSourceBloomExtract);
	if (gl3state.shaderBloomExtract == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitBloom: failed to create bloom extract shader\n");
		return false;
	}

	gl3state.uniBloomExtract_hdrBuffer = glGetUniformLocation(gl3state.shaderBloomExtract, "uHDRBuffer");
	gl3state.uniBloomExtract_threshold = glGetUniformLocation(gl3state.shaderBloomExtract, "uThreshold");

	GL3_UseShader(gl3state.shaderBloomExtract);
	glUniform1i(gl3state.uniBloomExtract_hdrBuffer, 0);

	// --- Bloom blur shader ---
	gl3state.shaderBloomBlur = CreateProgram(vertexSourcePost, fragmentSourceBloomBlur);
	if (gl3state.shaderBloomBlur == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitBloom: failed to create bloom blur shader\n");
		return false;
	}

	gl3state.uniBloomBlur_image      = glGetUniformLocation(gl3state.shaderBloomBlur, "uImage");
	gl3state.uniBloomBlur_horizontal = glGetUniformLocation(gl3state.shaderBloomBlur, "uHorizontal");
	gl3state.uniBloomBlur_texelSize  = glGetUniformLocation(gl3state.shaderBloomBlur, "uTexelSize");

	GL3_UseShader(gl3state.shaderBloomBlur);
	glUniform1i(gl3state.uniBloomBlur_image, 0);

	// --- Query and initialise bloom uniforms in shaderPost ---
	gl3state.uniPost_bloomBuffer   = glGetUniformLocation(gl3state.shaderPost, "uBloomBuffer");
	gl3state.uniPost_bloomStrength = glGetUniformLocation(gl3state.shaderPost, "uBloomStrength");

	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_bloomBuffer,   1);		// TMU1
	glUniform1f(gl3state.uniPost_bloomStrength, 0.0f);

	// --- Create bloom FBOs ---
	if (!CreateBloomFBO(&gl3state.fboBloomExtract, &gl3state.fboTexBloomExtract,
						gl3state.bloom_width, gl3state.bloom_height))
		return false;

	for (int i = 0; i < 2; i++)
	{
		if (!CreateBloomFBO(&gl3state.fboBloomPingPong[i], &gl3state.fboTexBloomPingPong[i],
							gl3state.bloom_width, gl3state.bloom_height))
			return false;
	}

	ri.Con_Printf(PRINT_ALL, "GL3 bloom FBOs initialized (%dx%d).\n", gl3state.bloom_width, gl3state.bloom_height);

	return true;
}

void GL3_ShutdownBloom(void)
{
	if (gl3state.fboTexBloomExtract != 0) { glDeleteTextures(1,     &gl3state.fboTexBloomExtract);  gl3state.fboTexBloomExtract = 0; }
	if (gl3state.fboBloomExtract    != 0) { glDeleteFramebuffers(1, &gl3state.fboBloomExtract);     gl3state.fboBloomExtract    = 0; }

	for (int i = 0; i < 2; i++)
	{
		if (gl3state.fboTexBloomPingPong[i] != 0) { glDeleteTextures(1,     &gl3state.fboTexBloomPingPong[i]);  gl3state.fboTexBloomPingPong[i] = 0; }
		if (gl3state.fboBloomPingPong[i]    != 0) { glDeleteFramebuffers(1, &gl3state.fboBloomPingPong[i]);     gl3state.fboBloomPingPong[i]    = 0; }
	}

	if (gl3state.shaderBloomExtract != 0) { glDeleteProgram(gl3state.shaderBloomExtract); gl3state.shaderBloomExtract = 0; }
	if (gl3state.shaderBloomBlur    != 0) { glDeleteProgram(gl3state.shaderBloomBlur);    gl3state.shaderBloomBlur    = 0; }

	gl3state.bloom_width  = 0;
	gl3state.bloom_height = 0;
}

void GL3_CompositeHDR(const int w, const int h, const float exposure, const float bloom_strength, const float ao_strength)
{
	if (gl3state.fbo3D == 0 || gl3state.shaderPost == 0)
		return;

	glViewport(0, 0, w, h);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);

	GL3_UseShader(gl3state.shaderPost);
	glUniform1f(gl3state.uniPost_exposure,      exposure);
	glUniform1f(gl3state.uniPost_bloomStrength, bloom_strength);
	glUniform1f(gl3state.uniPost_aoStrength,    ao_strength);

	// Color profile: map content into the wider primaries, then re-interpret as sRGB.
	// (D65 white point is shared by all profiles, so white/neutral colors stay unchanged.)
	static const float sRGB_gamut[9]   = { 1.00000f,  0.00000f,  0.00000f,  0.00000f,  1.00000f,  0.00000f,  0.00000f,  0.00000f,  1.00000f };
	static const float AdobeRGB_gamut[9] = { 1.39836f, -0.39836f,  0.00000f,  0.00000f,  1.00000f,  0.00000f,  0.00000f, -0.04293f,  1.04293f };
	static const float DCI_P3_gamut[9]    = { 1.22494f, -0.22494f,  0.00000f, -0.04206f,  1.04206f,  0.00000f, -0.01964f, -0.07864f,  1.09827f };
	static const float Rec2020_gamut[9]   = { 1.66049f, -0.58764f, -0.07285f, -0.12455f,  1.13290f, -0.00835f, -0.01815f, -0.10058f,  1.11873f };

	const float* profile = sRGB_gamut;
	switch ((int)r_colorprofile->value)
	{
		case 1: profile = AdobeRGB_gamut; break;
		case 2: profile = DCI_P3_gamut; break;
		case 3: profile = Rec2020_gamut; break;
		default: break;
	}

	glUniformMatrix3fv(gl3state.uniPost_colorProfile, 1, GL_TRUE, profile);

	// Bind the HDR color texture to TMU0 and keep the binding cache consistent.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTex3D);
	gl_state.currenttmu = 0;
	gl_state.currenttextures[0] = (int)gl3state.fboTex3D;

	// Bind the bloom result (or a 1x1 white fallback) to TMU1.
	glActiveTexture(GL_TEXTURE1);
	const GLuint bloom_tex = (gl3state.fboTexBloomPingPong[1] != 0)
		? gl3state.fboTexBloomPingPong[1]
		: gl3state.whiteTexture;
	glBindTexture(GL_TEXTURE_2D, bloom_tex);
	gl_state.currenttextures[1] = (int)bloom_tex;

	// Bind the AO result (or white fallback) to TMU2 — skips gl_state cache (MAX_TEXTURE_UNITS=2).
	glActiveTexture(GL_TEXTURE2);
	const GLuint ao_tex = (gl3state.fboTexSSAOBlur != 0)
		? gl3state.fboTexSSAOBlur
		: gl3state.whiteTexture;
	glBindTexture(GL_TEXTURE_2D, ao_tex);

	// Bind the depth texture to TMU3 for post-process fog.
	glActiveTexture(GL_TEXTURE3);
	if (gl3state.fboDepth3D != 0)
		glBindTexture(GL_TEXTURE_2D, gl3state.fboDepth3D);

	// Restore active unit to TMU0 so subsequent engine code isn't surprised.
	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(gl3state.vaoFSQ);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

// Number of separable Gaussian blur passes (must be even so the final result lands in pingpong[1]).
#define BLOOM_BLUR_PASSES 10

void GL3_RenderBloom(const float threshold, const float strength)
{
	if (strength <= 0.0f || gl3state.fboBloomExtract == 0)
		return;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, gl3state.bloom_width, gl3state.bloom_height);

	glBindVertexArray(gl3state.vaoFSQ);
	glActiveTexture(GL_TEXTURE0);

	// --- Bright-pass extract ---
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboBloomExtract);
	GL3_UseShader(gl3state.shaderBloomExtract);
	glUniform1f(gl3state.uniBloomExtract_threshold, threshold);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTex3D);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// --- Separable Gaussian blur: ping-pong between two half-res FBOs ---
	// Pass i even  → horizontal, writes to pingpong[0].
	// Pass i odd   → vertical,   writes to pingpong[1].
	// After BLOOM_BLUR_PASSES (even count) the result is in pingpong[1].
	GL3_UseShader(gl3state.shaderBloomBlur);
	const float texel_w = 1.0f / (float)gl3state.bloom_width;
	const float texel_h = 1.0f / (float)gl3state.bloom_height;
	glUniform2f(gl3state.uniBloomBlur_texelSize, texel_w, texel_h);

	for (int i = 0; i < BLOOM_BLUR_PASSES; i++)
	{
		const int horizontal = (i % 2 == 0) ? 1 : 0;
		const int dst = horizontal ? 0 : 1;
		const GLuint src_tex = (i == 0)
			? gl3state.fboTexBloomExtract
			: gl3state.fboTexBloomPingPong[1 - dst];

		glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboBloomPingPong[dst]);
		glUniform1i(gl3state.uniBloomBlur_horizontal, horizontal);
		glBindTexture(GL_TEXTURE_2D, src_tex);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);
	// Final bloom result is now in fboTexBloomPingPong[1].
}

// ============================================================
// Per-frame dynamic lighting.
// ============================================================

void GL3_UpdateDlights(void)
{
	const int n = (r_newrefdef.num_dlights < GL3_MAX_DLIGHTS)
				  ? r_newrefdef.num_dlights : GL3_MAX_DLIGHTS;

	GL3_UseShader(gl3state.shader3D);
	glUniform1i(gl3state.uni3D_numDlights, n);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform1i(gl3state.uni3DT_numDlights, n);
	}

	if (n == 0)
		return;

	float pos_rad[GL3_MAX_DLIGHTS * 4];
	float colors[GL3_MAX_DLIGHTS * 4];

	// r_world_matrix is the view matrix (world-space → view-space).
	const float* M = r_world_matrix;

	for (int i = 0; i < n; i++)
	{
		const dlight_t* dl = &r_newrefdef.dlights[i];
		const float x = dl->origin[0];
		const float y = dl->origin[1];
		const float z = dl->origin[2];

		// Column-major matrix-vector multiply: view_pos = M * [x, y, z, 1].
		pos_rad[i * 4 + 0] = M[0] * x + M[4] * y + M[8]  * z + M[12];
		pos_rad[i * 4 + 1] = M[1] * x + M[5] * y + M[9]  * z + M[13];
		pos_rad[i * 4 + 2] = M[2] * x + M[6] * y + M[10] * z + M[14];
		pos_rad[i * 4 + 3] = dl->intensity;

		colors[i * 4 + 0] = (float)dl->color.r / 255.0f;
		colors[i * 4 + 1] = (float)dl->color.g / 255.0f;
		colors[i * 4 + 2] = (float)dl->color.b / 255.0f;
		colors[i * 4 + 3] = 0.0f;
	}

	glUniform4fv(gl3state.uni3D_dlightPosRad, n, pos_rad);
	glUniform4fv(gl3state.uni3D_dlightColor,  n, colors);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform4fv(gl3state.uni3DT_dlightPosRad, n, pos_rad);
		glUniform4fv(gl3state.uni3DT_dlightColor,  n, colors);
	}
}

// Set only the model-shader dynamic-light count (used to disable dynamic
// lights on models during the water-reflection pass, where the mirrored
// dlight tint reads as a full-strength colour wash).
void GL3_SetNumDlights(const int n)
{
	GL3_UseShader(gl3state.shader3D);
	glUniform1i(gl3state.uni3D_numDlights, n);

	if (gl3state.shader3DTess != 0)
	{
		GL3_UseShader(gl3state.shader3DTess);
		glUniform1i(gl3state.uni3DT_numDlights, n);
	}
}

// ============================================================
// SSAO post-process.
// ============================================================

void GL3_ShutdownSSAO(void)
{
	if (gl3state.fboTexSSAO     != 0) { glDeleteTextures(1,     &gl3state.fboTexSSAO);     gl3state.fboTexSSAO     = 0; }
	if (gl3state.fboSSAO        != 0) { glDeleteFramebuffers(1, &gl3state.fboSSAO);        gl3state.fboSSAO        = 0; }
	if (gl3state.fboTexSSAOBlur != 0) { glDeleteTextures(1,     &gl3state.fboTexSSAOBlur); gl3state.fboTexSSAOBlur = 0; }
	if (gl3state.fboSSAOBlur    != 0) { glDeleteFramebuffers(1, &gl3state.fboSSAOBlur);    gl3state.fboSSAOBlur    = 0; }
	if (gl3state.ssaoNoiseTex   != 0) { glDeleteTextures(1,     &gl3state.ssaoNoiseTex);   gl3state.ssaoNoiseTex   = 0; }
	if (gl3state.shaderSSAO     != 0) { glDeleteProgram(gl3state.shaderSSAO);              gl3state.shaderSSAO     = 0; }
	if (gl3state.shaderSSAOBlur != 0) { glDeleteProgram(gl3state.shaderSSAOBlur);          gl3state.shaderSSAOBlur = 0; }
}

qboolean GL3_InitSSAO(const int width, const int height)
{
	// --- SSAO shader ---
	gl3state.shaderSSAO = CreateProgram(vertexSourcePost, fragmentSourceSSAO);
	if (gl3state.shaderSSAO == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitSSAO: failed to create SSAO shader\n");
		return false;
	}

	gl3state.uniSSAO_depthMap   = glGetUniformLocation(gl3state.shaderSSAO, "uDepthMap");
	gl3state.uniSSAO_noiseTex   = glGetUniformLocation(gl3state.shaderSSAO, "uNoiseTex");
	gl3state.uniSSAO_kernel     = glGetUniformLocation(gl3state.shaderSSAO, "uKernel");
	gl3state.uniSSAO_projParams = glGetUniformLocation(gl3state.shaderSSAO, "uProjParams");
	gl3state.uniSSAO_radius     = glGetUniformLocation(gl3state.shaderSSAO, "uRadius");
	gl3state.uniSSAO_bias       = glGetUniformLocation(gl3state.shaderSSAO, "uBias");
	gl3state.uniSSAO_screenSize = glGetUniformLocation(gl3state.shaderSSAO, "uScreenSize");

	GL3_UseShader(gl3state.shaderSSAO);
	glUniform1i(gl3state.uniSSAO_depthMap, 0);	// TMU0
	glUniform1i(gl3state.uniSSAO_noiseTex, 1);	// TMU1

	// --- SSAO blur shader ---
	gl3state.shaderSSAOBlur = CreateProgram(vertexSourcePost, fragmentSourceSSAOBlur);
	if (gl3state.shaderSSAOBlur == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitSSAO: failed to create SSAO blur shader\n");
		GL3_ShutdownSSAO();
		return false;
	}

	gl3state.uniSSAOBlur_ssaoInput = glGetUniformLocation(gl3state.shaderSSAOBlur, "uSSAOInput");
	gl3state.uniSSAOBlur_texelSize = glGetUniformLocation(gl3state.shaderSSAOBlur, "uTexelSize");

	GL3_UseShader(gl3state.shaderSSAOBlur);
	glUniform1i(gl3state.uniSSAOBlur_ssaoInput, 0);	// TMU0

	// --- Wire up AO uniforms in shaderPost (queried here; sampler bound to TMU2 once) ---
	gl3state.uniPost_aoBuffer   = glGetUniformLocation(gl3state.shaderPost, "uAOBuffer");
	gl3state.uniPost_aoStrength = glGetUniformLocation(gl3state.shaderPost, "uAOStrength");

	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_aoBuffer,   2);		// TMU2
	glUniform1f(gl3state.uniPost_aoStrength, 0.0f);

	// --- Generate SSAO hemisphere sample kernel (16 samples, deterministic LCG) ---
	{
		unsigned int state = 12345u;
		float kernel[16 * 3];

		for (int i = 0; i < 16; i++)
		{
			state = state * 1664525u + 1013904223u;
			const float rx = (float)state / 4294967295.0f;
			state = state * 1664525u + 1013904223u;
			const float ry = (float)state / 4294967295.0f;
			state = state * 1664525u + 1013904223u;
			const float rz = (float)state / 4294967295.0f;

			// Hemisphere sample: x,y in [-1,1], z in [0,1].
			float sx = rx * 2.0f - 1.0f;
			float sy = ry * 2.0f - 1.0f;
			float sz = rz;

			// Normalize.
			const float len = sqrtf(sx * sx + sy * sy + sz * sz);
			if (len > 0.0f) { sx /= len; sy /= len; sz /= len; }

			// Accelerate distribution toward origin: lerp(0.1, 1.0, (i/16)^2).
			float scale = (float)i / 16.0f;
			scale = 0.1f + scale * scale * 0.9f;

			kernel[i * 3 + 0] = sx * scale;
			kernel[i * 3 + 1] = sy * scale;
			kernel[i * 3 + 2] = sz * scale;
		}

		GL3_UseShader(gl3state.shaderSSAO);
		glUniform3fv(gl3state.uniSSAO_kernel, 16, kernel);
	}

	// --- Generate 4x4 noise texture (random XY rotation vectors, z=0) ---
	{
		unsigned int state = 98765u;
		float noise[4 * 4 * 3];

		for (int i = 0; i < 16; i++)
		{
			state = state * 1664525u + 1013904223u;
			const float nx = (float)state / 4294967295.0f * 2.0f - 1.0f;
			state = state * 1664525u + 1013904223u;
			const float ny = (float)state / 4294967295.0f * 2.0f - 1.0f;

			const float nlen = sqrtf(nx * nx + ny * ny);
			noise[i * 3 + 0] = (nlen > 0.0f) ? nx / nlen : 1.0f;
			noise[i * 3 + 1] = (nlen > 0.0f) ? ny / nlen : 0.0f;
			noise[i * 3 + 2] = 0.0f;
		}

		glGenTextures(1, &gl3state.ssaoNoiseTex);
		glBindTexture(GL_TEXTURE_2D, gl3state.ssaoNoiseTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// --- Create two full-res GL_R16F FBOs (raw SSAO + blurred result, no depth) ---
	{
		GLuint* fbos[2]     = { &gl3state.fboSSAO,    &gl3state.fboSSAOBlur    };
		GLuint* textures[2] = { &gl3state.fboTexSSAO, &gl3state.fboTexSSAOBlur };

		for (int i = 0; i < 2; i++)
		{
			glGenTextures(1, textures[i]);
			glBindTexture(GL_TEXTURE_2D, *textures[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindTexture(GL_TEXTURE_2D, 0);

			glGenFramebuffers(1, fbos[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, *fbos[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *textures[i], 0);

			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			if (status != GL_FRAMEBUFFER_COMPLETE)
			{
				ri.Con_Printf(PRINT_ALL, "GL3_InitSSAO: SSAO FBO %d incomplete (status 0x%x)\n", i, (unsigned)status);
				GL3_ShutdownSSAO();
				return false;
			}
		}
	}

	ri.Con_Printf(PRINT_ALL, "GL3 SSAO initialized (%dx%d).\n", width, height);

	return true;
}

void GL3_RenderSSAO(const float radius, const float bias)
{
	if (gl3state.fbo3D == 0 || gl3state.fboTexSSAOBlur == 0 || gl3state.shaderSSAO == 0)
		return;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, gl3state.fbo_width, gl3state.fbo_height);
	glBindVertexArray(gl3state.vaoFSQ);

	// --- SSAO pass: sample depth hemisphere → raw occlusion map ---
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboSSAO);
	GL3_UseShader(gl3state.shaderSSAO);
	glUniform1f(gl3state.uniSSAO_radius, radius);
	glUniform1f(gl3state.uniSSAO_bias, bias);
	glUniform2f(gl3state.uniSSAO_screenSize, (float)gl3state.fbo_width, (float)gl3state.fbo_height);
	glUniform4fv(gl3state.uniSSAO_projParams, 1, gl3state.projParams);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboDepth3D);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gl3state.ssaoNoiseTex);
	glActiveTexture(GL_TEXTURE0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// --- Blur pass: 4x4 box blur smooths the raw occlusion map ---
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboSSAOBlur);
	GL3_UseShader(gl3state.shaderSSAOBlur);
	glUniform2f(gl3state.uniSSAOBlur_texelSize,
		1.0f / (float)gl3state.fbo_width,
		1.0f / (float)gl3state.fbo_height);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexSSAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);

	// Restore TMU1 to idle so the engine's texture cache stays consistent.
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}
