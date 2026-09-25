#version 450
// WP0 skeleton shader: attributeless fullscreen triangle (gl_VertexIndex 0..2).
// Exists so the S1 shader gate validates real Vulkan GLSL from day one.

layout(location = 0) out vec2 v_uv;

void main()
{
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
