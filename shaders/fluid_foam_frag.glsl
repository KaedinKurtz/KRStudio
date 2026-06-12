#version 430 core
// Soft additive whitewater. Brightness varies CONTINUOUSLY with type and
// remaining life instead of three hard steps: spray flashes bright, surface
// foam sits in the middle, submerged bubbles glow faintly.

in float vLife;
in float vType;

out vec4 FragColor;

void main()
{
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    float soft = (1.0 - r2) * (1.0 - r2);
    // Smooth fade-in over the last 1.5 s of life and a soft ramp between
    // the type bands (type is 0 spray / 1 foam / 2 bubble).
    float fade = smoothstep(0.0, 1.5, vLife);
    float brightness = mix(0.95, 0.55, smoothstep(0.0, 1.0, vType));
    brightness = mix(brightness, 0.22, smoothstep(1.0, 2.0, vType));
    FragColor = vec4(vec3(1.0), 1.0) * soft * fade * brightness;
}
