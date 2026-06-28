#version 450 core
out vec4 fragColor;

in vec3 TexCoords;

uniform samplerCube skybox;
uniform float u_skyNits;   // photometric luminance scale so the visible sky matches the
                           // physically-based lights/IBL (brought to display by EV exposure)

void main()
{
    fragColor = vec4(texture(skybox, TexCoords).rgb * u_skyNits, 1.0);
}