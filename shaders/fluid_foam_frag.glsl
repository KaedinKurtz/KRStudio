#version 430 core
// Soft additive whitewater: spray bright, foam soft, bubbles dim.

in float vLife;
in float vType;

out vec4 FragColor;

void main()
{
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    float soft = (1.0 - r2) * (1.0 - r2);
    float fade = clamp(vLife, 0.0, 1.0);
    float brightness = vType < 0.5 ? 0.9 : (vType < 1.5 ? 0.55 : 0.25);
    FragColor = vec4(vec3(1.0), 1.0) * soft * fade * brightness;
}
