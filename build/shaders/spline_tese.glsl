#version 430 core

layout (isoline, equal_spacing, cw) in;

uniform mat4 u_proj;
uniform mat4 u_view;

layout(std430, binding = 0) buffer ControlPointBuffer {
    vec3 points[];
};

vec3 p(int i) {
    return points[clamp(i, 0, int(points.length()) - 1)];
}

vec3 evalCatmullRom(float t) {
    int numPoints = int(points.length());
    if (numPoints < 4) return vec3(0.0);
    float ft = t * (numPoints - 3);
    int i = int(floor(ft)) + 1;
    float u = fract(ft);
    vec3 P0 = p(i - 1), P1 = p(i), P2 = p(i + 1), P3 = p(i + 2);
    float u2 = u * u, u3 = u2 * u;
    return 0.5 * ((2.0 * P1) + (-P0 + P2) * u + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * u2 + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * u3);
}

void main() {
    float t = gl_TessCoord.x;
    vec3 pos = evalCatmullRom(t);
    gl_Position = u_proj * u_view * vec4(pos, 1.0);
}