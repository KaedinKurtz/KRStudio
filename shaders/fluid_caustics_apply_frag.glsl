#version 430 core

// Additively lights the scene with this frame's water caustics BEFORE the
// water composites over it (so you see the pattern through the water).
// Looks the world position up in the G-buffer and samples the top-down
// caustic intensity map with a soft 3x3 kernel.

in vec2 TexCoords;

uniform sampler2D u_gPosition;
uniform usampler2D u_caustics;
uniform vec2 u_worldMin;
uniform vec2 u_worldSize;
uniform float u_floorY;
uniform vec3 u_lightColor;

out vec4 FragColor;

void main()
{
    vec4 wp = texture(u_gPosition, TexCoords);
    if (wp.w <= 0.0) discard;                       // empty G-buffer texel
    if (abs(wp.y - u_floorY) > 0.35) discard;       // receivers near the floor only

    vec2 uv = (wp.xz - u_worldMin) / u_worldSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) discard;

    ivec2 size = textureSize(u_caustics, 0);
    ivec2 c = ivec2(uv * vec2(size));
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            ivec2 t = clamp(c + ivec2(x, y), ivec2(0), size - 1);
            sum += float(texelFetch(u_caustics, t, 0).r);
        }
    // 64 per splat, 9 taps; normalise against a "flat water" baseline so
    // even coverage reads neutral and focal lines read bright.
    float intensity = sum / (9.0 * 64.0 * 3.0);
    intensity = max(intensity - 0.4, 0.0); // flat-coverage floor
    if (intensity <= 0.0) discard;

    FragColor = vec4(u_lightColor * min(intensity, 2.5), 1.0);
}
