/*
================================================================================
|                           instanced_arrow_vert.glsl                          |
================================================================================
*/
#version 430 core
#define MAX_STOPS 8                         // hard upper-bound for UI

// ── per-vertex data for the arrow mesh ───────────────────────────────────────
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;       // (kept for possible lighting)

// ── per-instance transformation matrix ───────────────────────────────────────
layout(location = 2) in vec4 aInstanceMatCol0;
layout(location = 3) in vec4 aInstanceMatCol1;
layout(location = 4) in vec4 aInstanceMatCol2;
layout(location = 5) in vec4 aInstanceMatCol3;

// ── extra per-instance parameters ────────────────────────────────────────────
layout(location = 6) in float aIntensity;   // 0‥1 magnitude at the arrow tip
layout(location = 7) in float aAgeNorm;     // 0‥1 normalised lifetime

// ── global uniforms ──────────────────────────────────────────────────────────
uniform mat4 view;
uniform mat4 projection;

/*  uColoringMode: 0 = Intensity-based colouring
 *                 1 = Lifetime-based colouring           (expand if needed)
 */
uniform int   uColoringMode;

/*  Gradient description (shared by both vertex & fragment stages if desired) */
uniform int   uStopCount;                   // 2 … MAX_STOPS (0 & 1 required)
uniform vec4  uStopColor[MAX_STOPS];
uniform float uStopPos  [MAX_STOPS];

// ── varyings ─────────────────────────────────────────────────────────────────
out vec4 vs_color;

// ── helper: fetch colour at t (0‥1) ─────────────────────────────────────────
vec4 sampleGradient(float t)
{
    t = clamp(t, 0.0, 1.0);
    for (int i = 0; i < uStopCount - 1; ++i)
    {
        float a = uStopPos[i];
        float b = uStopPos[i + 1];
        if (t >= a && t <= b)
        {
            float f = (t - a) / (b - a);
            return mix(uStopColor[i], uStopColor[i + 1], f);
        }
    }
    return uStopColor[uStopCount - 1];      // safety fallback
}

// ── main ─────────────────────────────────────────────────────────────────────
void main()
{
    // Assemble full model matrix
    mat4 modelMatrix = mat4(
        aInstanceMatCol0,
        aInstanceMatCol1,
        aInstanceMatCol2,
        aInstanceMatCol3
    );

    // Transform vertex to clip space
    gl_Position = projection * view * modelMatrix * vec4(aPos, 1.0);

    // Choose parameter for the gradient
    float param = (uColoringMode == 0) ? aIntensity : aAgeNorm;

    // Compute final vertex colour
    vs_color = sampleGradient(param);
}
