#version 330 core
in vec3 vWorldPos;
in vec3 vWorldNormal;

out vec4 FragColor;

uniform vec3 uBaseColor;        // per-handle base color
uniform vec3 uHighlightColor;   // e.g., vec3(1.0) or a warm tint
uniform vec3 uViewPos;          // camera position in world

uniform bool uIsHovered;        // this handle is under cursor
uniform bool uIsActive;         // this handle is being dragged
uniform float uDimFactor;       // 0.0 = no dim, 1.0 = full dim (when dragging others)

void main() {
    // Start from flat base color
    vec3 color = uBaseColor;

    // Dim non-active handles while dragging
    if (uDimFactor > 0.0 && !uIsActive) {
        // 0.25 keeps them visible but clearly muted
        color *= mix(1.0, 0.25, clamp(uDimFactor, 0.0, 1.0));
    }

    // Simple Fresnel-like rim for hover/active
    bool showGlow = (uIsHovered || uIsActive);
    if (showGlow) {
        vec3 N = normalize(vWorldNormal);
        vec3 V = normalize(uViewPos - vWorldPos);
        float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);  // soft rim
        // Active gets stronger glow than hover
        float strength = uIsActive ? 1.0 : 0.5;
        vec3 glow = mix(color, uHighlightColor, 0.6) * (0.4 + 0.6 * fres) * strength;
        color = clamp(glow, 0.0, 10.0);
    }

    FragColor = vec4(color, 1.0);
}
