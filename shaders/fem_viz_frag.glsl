#version 430 core
// Phase 5 FEM body recolour. ramp() is kept IDENTICAL to mpm_render_vert.glsl so
// FEM meshes and MPM splats use the same cold->hot colormap (this engine's shader
// loader does not support #include, hence the duplication — keep in sync).
in float vT;
out vec4 FragColor;

vec3 ramp(float t)
{
    t = clamp(t, 0.0, 1.0);
    const vec3 c0 = vec3(0.05, 0.05, 0.35); // cold
    const vec3 c1 = vec3(0.10, 0.55, 0.85);
    const vec3 c2 = vec3(0.15, 0.80, 0.30);
    const vec3 c3 = vec3(0.95, 0.85, 0.15);
    const vec3 c4 = vec3(0.90, 0.25, 0.10);
    const vec3 c5 = vec3(1.00, 0.95, 0.90); // hot
    if (t < 0.2) return mix(c0, c1, t / 0.2);
    if (t < 0.4) return mix(c1, c2, (t - 0.2) / 0.2);
    if (t < 0.6) return mix(c2, c3, (t - 0.4) / 0.2);
    if (t < 0.8) return mix(c3, c4, (t - 0.6) / 0.2);
    return mix(c4, c5, (t - 0.8) / 0.2);
}

void main()
{
    FragColor = vec4(ramp(vT), 1.0);
}
