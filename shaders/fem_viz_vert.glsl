#version 430 core
// Phase 5 FEM body recolour: position (loc 0) + per-vertex nodal scalar (loc 1)
// from the FEM solve. Normalises the scalar by the shared [rangeMin,rangeMax].
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aScalar;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_rangeMin;
uniform float u_rangeMax;

out float vT;

void main()
{
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
    vT = clamp((aScalar - u_rangeMin) / max(u_rangeMax - u_rangeMin, 1e-6), 0.0, 1.0);
}
