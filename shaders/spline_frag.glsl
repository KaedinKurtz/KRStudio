#version 410 core
out vec4 FragColor;
uniform vec4  u_colour;
uniform bool  u_emissive;
void main()
{
    FragColor = u_colour;
    if (u_emissive)
        FragColor.rgb *= 3.5;           // cheap “glow” – let bloom do the rest
}