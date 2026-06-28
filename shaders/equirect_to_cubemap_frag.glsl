#version 430 core
in vec3 WorldDir;
out vec4 FragColor;

uniform sampler2D equirectangularMap;
const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    return uv * invAtan + 0.5;
}

void main() {
    vec3 dir = normalize(WorldDir);
    vec2 uv = sampleSphericalMap(dir);
    // Clamp the HDR to a finite max so a very bright sun cannot overflow GL_RGB16F
    // to +inf in the baked cubemap. The skybox samples this cubemap directly and
    // feeds it to ACES; acesFilm(+inf) = inf/inf = NaN, which renders as a CYAN
    // speckle on the sun. 1000 is far above any tonemapped white (ACES saturates
    // ~16) so the sun still reads as a bright white disk -- just never NaN.
    vec3 c = min(texture(equirectangularMap, uv).rgb, vec3(1000.0));
    FragColor = vec4(c, 1.0);
}
