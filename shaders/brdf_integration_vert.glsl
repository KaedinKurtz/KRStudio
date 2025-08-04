#version 430 core
const vec2 pos[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0,  3.0)
);
out vec2 TexCoords;
void main() {
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    TexCoords = (gl_Position.xy * 0.5) + 0.5;
}
