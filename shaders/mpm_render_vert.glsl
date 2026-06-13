#version 430 core
// MLS-MPM particle billboards: positions pulled straight from the solver SSBO.
struct Particle {
    vec4 posMass;
    vec4 velVol;
    vec4 c0, c1, c2;
    vec4 f0, f1, f2;
    vec4 plastic;   // x Jp/J, y temperature, z heatCap, w meltTemp
    vec4 matl;      // w = material type
    vec4 color;     // rgb, w alive
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_viewportHeight;
uniform float u_radius;

out vec3 vColor;
out float vSpeed;

void main()
{
    Particle pp = p[gl_VertexID];
    if (pp.color.w <= 0.0) {        // dead/parked particle -> cull off-screen
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        return;
    }
    vColor = pp.color.rgb;
    vSpeed = length(pp.velVol.xyz);

    vec4 vp = u_view * vec4(pp.posMass.xyz, 1.0);
    gl_Position = u_projection * vp;
    float dist = max(0.1, -vp.z);
    gl_PointSize = clamp(u_radius * 2.2 * u_viewportHeight * u_projection[1][1] / dist, 1.0, 64.0);
}
