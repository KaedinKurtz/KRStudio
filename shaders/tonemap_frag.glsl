#version 430 core

// Final display transform: ACES filmic (Narkowicz fit) + gamma, applied
// once at the END of the frame so water refraction/absorption and foam
// composite in linear radiance. Overlays drawn after this pass (gizmo)
// stay in display space.

in vec2 TexCoords;

uniform sampler2D u_hdr;
uniform float u_exposure;

out vec4 FragColor;

vec3 acesFilm(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(u_hdr, TexCoords).rgb * u_exposure;
    vec3 color = acesFilm(hdr);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
