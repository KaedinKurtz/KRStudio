// Corrected glow_line_geom.glsl
#version 410 core
layout (lines) in;
layout (triangle_strip, max_vertices = 4) out;

uniform mat4 u_view;
uniform mat4 u_proj;
uniform float u_thickness = 8.0;
uniform vec2 u_viewport_size; // NEW: The size of the viewport in pixels

out float g_fade_coord;

void main() {
    vec3 p1_world = gl_in[0].gl_Position.xyz;
    vec3 p2_world = gl_in[1].gl_Position.xyz;

    vec4 p1_clip = u_proj * u_view * vec4(p1_world, 1.0);
    vec4 p2_clip = u_proj * u_view * vec4(p2_world, 1.0);

    // Convert from clip space to screen-space NDC (-1 to 1)
    vec2 p1_ndc = p1_clip.xy / p1_clip.w;
    vec2 p2_ndc = p2_clip.xy / p2_clip.w;

    // Get the 2D direction of the line on the screen
    vec2 line_dir_screen = normalize(p2_ndc - p1_ndc);
    
    // Get the perpendicular offset vector
    vec2 offset_dir = vec2(-line_dir_screen.y, line_dir_screen.x);

    // THE FIX: Calculate the offset in NDC space based on viewport size.
    // This makes 'u_thickness' correspond to a number of pixels.
    vec2 offset = offset_dir * u_thickness / u_viewport_size;

    // Emit the four vertices of the quad
    g_fade_coord = 1.0;
    gl_Position = p1_clip + vec4(offset * p1_clip.w, 0.0, 0.0);
    EmitVertex();

    g_fade_coord = -1.0;
    gl_Position = p1_clip - vec4(offset * p1_clip.w, 0.0, 0.0);
    EmitVertex();

    g_fade_coord = 1.0;
    gl_Position = p2_clip + vec4(offset * p2_clip.w, 0.0, 0.0);
    EmitVertex();

    g_fade_coord = -1.0;
    gl_Position = p2_clip - vec4(offset * p2_clip.w, 0.0, 0.0);
    EmitVertex();

    EndPrimitive();
}