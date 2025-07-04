#version 430 core
layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

uniform mat4 u_view;
uniform mat4 u_proj;
uniform float u_thickness;
uniform vec2 u_viewport_size; // The C++ code will now provide this

out vec2 g_uv; // Pass UV coord to the fragment shader

void main() {
    // Transform the center point all the way to clip space
    vec4 pos_clip = u_proj * u_view * gl_in[0].gl_Position;

    // THE FIX: Calculate the quad's radius in screen-space (NDC),
    // making it proportional to the viewport size.
    vec2 radius_ndc = (u_thickness / 2.0) / u_viewport_size;

    // Generate the quad corners relative to the center in clip space
    gl_Position = pos_clip + vec4(-radius_ndc.x, -radius_ndc.y, 0.0, 0.0) * pos_clip.w;
    g_uv = vec2(-1.0, -1.0);
    EmitVertex();

    gl_Position = pos_clip + vec4(radius_ndc.x, -radius_ndc.y, 0.0, 0.0) * pos_clip.w;
    g_uv = vec2(1.0, -1.0);
    EmitVertex();

    gl_Position = pos_clip + vec4(-radius_ndc.x, radius_ndc.y, 0.0, 0.0) * pos_clip.w;
    g_uv = vec2(-1.0, 1.0);
    EmitVertex();

    gl_Position = pos_clip + vec4(radius_ndc.x, radius_ndc.y, 0.0, 0.0) * pos_clip.w;
    g_uv = vec2(1.0, 1.0);
    EmitVertex();

    EndPrimitive();
}