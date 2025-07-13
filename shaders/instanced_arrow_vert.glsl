#version 430 core
#define MAX_STOPS 8

// --- Per-Vertex & Per-Instance Attributes ---
// These are all confirmed to be receiving correct data.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aInstanceMatCol0;
layout(location = 3) in vec4 aInstanceMatCol1;
layout(location = 4) in vec4 aInstanceMatCol2;
layout(location = 5) in vec4 aInstanceMatCol3;
layout(location = 6) in float aIntensity;
layout(location = 7) in float aAgeNorm;

// --- Uniforms ---
// These are all confirmed to be receiving correct data.
uniform mat4 view;
uniform mat4 projection;
uniform int u_coloringMode;
uniform int u_stopCount;
uniform vec4 u_stopColor[MAX_STOPS];
uniform float u_stopPos[MAX_STOPS];

// --- Varying Output ---
out vec4 vs_color;

// ========================================================================
// --- A More Robust Gradient Sampling Function ---
// This version is written to be less prone to compiler/driver optimization bugs.
// ========================================================================
vec4 sampleGradient(float t)
{
    // Clamp the input parameter to the valid 0.0 - 1.0 range.
    t = clamp(t, 0.0, 1.0);

    // Explicitly handle the case of two stops, which is the most common.
    // This avoids the loop entirely for the default gradient.
    if (u_stopCount == 2) {
        // Direct linear interpolation. 't' itself becomes the mix factor
        // because the position range is from 0.0 to 1.0.
        return mix(u_stopColor[0], u_stopColor[1], t);
    }

    // Handle more complex gradients with a safer loop.
    if (u_stopCount > 2) {
        for (int i = 0; i < u_stopCount - 1; ++i)
        {
            float pos1 = u_stopPos[i];
            float pos2 = u_stopPos[i+1];

            if (t >= pos1 && t <= pos2)
            {
                // Calculate the denominator, safely handling when stops overlap.
                float range = pos2 - pos1;
                if (range < 0.0001) {
                    return u_stopColor[i]; // If range is zero, return the first color.
                }
                
                // Calculate the interpolation factor and mix the colors.
                float factor = (t - pos1) / range;
                return mix(u_stopColor[i], u_stopColor[i+1], factor);
            }
        }
    }
    
    // Fallback: If only one stop exists, or if 't' is outside all ranges
    // (which shouldn't happen with clamp), return the first color.
    if (u_stopCount > 0) {
        return u_stopColor[0];
    }

    // Final safety net: If no stops exist, return an obvious debug color.
    return vec4(1.0, 0.0, 1.0, 1.0); // Magenta
}

void main()
{
    // Reconstruct the model matrix.
    mat4 modelMatrix = mat4(
        aInstanceMatCol0, aInstanceMatCol1, aInstanceMatCol2, aInstanceMatCol3
    );
    
    // Transform the vertex position into clip space.
    gl_Position = projection * view * modelMatrix * vec4(aPos, 1.0);
    
    // Choose the parameter for the gradient.
    float param = (u_coloringMode == 0) ? aIntensity : aAgeNorm;

    // Compute the final vertex color using our robust function.
    vs_color = sampleGradient(param);
}