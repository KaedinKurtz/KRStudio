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
    FragColor = texture(equirectangularMap, uv);
}
