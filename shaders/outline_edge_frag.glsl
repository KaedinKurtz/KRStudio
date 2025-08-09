#version 430 core
in vec2 TexCoords;
layout(location=0) out vec4 FragColor;

uniform sampler2D uMask;     // raw mask (white where selected)
uniform sampler2D uBlur;     // blurred mask
uniform vec3  uColor;        // outline color
uniform float uIntensity;    // outline strength scale (try 3.0–6.0)
uniform float uThreshold;    // soft threshold (ignored unless enabled)

// ===== DEBUG SWITCHES =====
const bool DEBUG_SHOW_MASK   = false;  // visualize mask (m)
const bool DEBUG_SHOW_BLUR   = false;  // visualize blur (b)
const bool DEBUG_SHOW_EDGE   = false;  // visualize raw edge (e)

// Robust edge: difference, with a little bias to ensure we get >0
void main() {
    float m = texture(uMask, TexCoords).r;
    float b = texture(uBlur, TexCoords).r;

    // Debug views
    if (DEBUG_SHOW_MASK) { FragColor = vec4(m, m, m, 1.0); return; }
    if (DEBUG_SHOW_BLUR) { FragColor = vec4(b, b, b, 1.0); return; }

    // Edge strength
    // Small bias makes sure e is non-zero when blur expands beyond mask by even 1 texel.
    float e = max(texture(uBlur, TexCoords).r - texture(uMask, TexCoords).r + 0.001, 0.0);
    // If you want a threshold, uncomment this and tune later:
    // e = smoothstep(uThreshold, 1.0, e);

    // For ADDITIVE: put all weight in alpha; keep rgb as pure tint
    float a = clamp(e * uIntensity, 0.0, 1.0);
    FragColor = vec4(uColor, a);
}
