#version 430 core
// Spherical fluid particle sprite with simple lighting + speed tint.

in float vSpeed;
in float vLife;
out vec4 fragColor;

uniform vec3 u_waterColor;
uniform float u_turbidity;   // 0 clear/glossy .. 1 murky/flat
uniform float u_emissivity;  // additive glow
uniform float u_foaminess;   // speed-driven white water

void main()
{
    if (vLife <= 0.0) discard;

    // Circular mask + fake sphere normal from sprite coords
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;
    vec3 n = vec3(uv.x, -uv.y, sqrt(max(0.0, 1.0 - r2)));

    const vec3 lightDir = normalize(vec3(0.4, 0.8, 0.45));
    float ndl = max(dot(n, lightDir), 0.0);
    float fresnel = pow(1.0 - n.z, 2.0);

    vec3 deepWater = u_waterColor * 0.22;
    vec3 foam = vec3(0.85, 0.92, 0.98);

    float foamAmt = clamp(vSpeed * 0.25 * u_foaminess, 0.0, 0.7);
    vec3 base = mix(deepWater, u_waterColor, 0.35 + 0.65 * ndl);
    // turbidity: scatter-dominated look — flatter shading, weaker fresnel
    base = mix(base, u_waterColor, u_turbidity * 0.6);
    float gloss = (1.0 - u_turbidity);
    vec3 color = mix(base, foam, foamAmt) + fresnel * 0.2 * gloss
               + u_waterColor * u_emissivity;

    fragColor = vec4(color, 1.0);
}
