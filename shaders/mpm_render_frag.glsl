#version 430 core
// Lit spherical sprite for MLS-MPM particles. In a viz mode the colour IS the
// data, so shading is flattened (less specular, more ambient) to keep the
// gradient readable; in Default mode it gets full lit-sphere shading.
in vec3 vColor;
in float vSpeed;
in float vViz;
out vec4 FragColor;

void main()
{
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(c, c);
    if (r2 > 1.0) discard;
    vec3 n = vec3(c.x, -c.y, sqrt(max(0.0, 1.0 - r2)));
    vec3 L = normalize(vec3(0.4, 0.85, 0.5));
    float diff = clamp(dot(n, L), 0.0, 1.0);

    if (vViz > 0.5) {
        // Data-as-colour: gentle round shading so spheres stay legible.
        vec3 col = vColor * (0.75 + 0.25 * diff);
        FragColor = vec4(col, 1.0);
        return;
    }
    float lit = diff * 0.8 + 0.25;
    float spec = pow(clamp(dot(reflect(-L, n), vec3(0, 0, 1)), 0.0, 1.0), 24.0) * 0.35;
    vec3 col = vColor * lit + vec3(spec);
    col += vColor * clamp(vSpeed * 0.04, 0.0, 0.3); // motion highlight
    FragColor = vec4(col, 1.0);
}
