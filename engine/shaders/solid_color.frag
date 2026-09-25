#version 450
// WP0 skeleton shader: solid color via push constants — exercises the per-draw
// push-constant convention from IMPLEMENTATION_PLAN.md (shader changeover rule 2).

layout(push_constant) uniform Push { vec4 color; } pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    o_color = pc.color;
}
