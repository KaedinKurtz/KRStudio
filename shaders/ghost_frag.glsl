#version 330 core
// Ghost validity robot (Phase 7). Drawn POST-tonemap (display space), so the colour is output
// directly -- no exposure pre-division (that is only for pre-tonemap overlays). A fresnel term makes
// it read as a translucent hologram (denser toward grazing silhouettes) while keeping a base opacity
// high enough that the validity tint stays readable over the background. uColor.rgb is the validity
// tint (green = reachable, red = a joint hit its limit); uColor.a is the base opacity.
in vec3 vWorldPos;
in vec3 vWorldNormal;

uniform vec3 uViewPos;
uniform vec4 uColor;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    float ndv  = abs(dot(N, V));
    float fres = pow(1.0 - ndv, 1.5);
    float a    = clamp(uColor.a * (1.6 + 1.0 * fres), 0.0, 0.95);
    vec3  col  = uColor.rgb * (0.85 + 0.4 * fres);
    FragColor  = vec4(col, a);
}
