#version 430 core

// Lingering surface foam, decay step: the world-anchored accumulation
// buffer diffuses slightly (foam spreads) and fades each frame. New foam
// is splatted on top by the inject pass.

in vec2 TexCoords;

uniform sampler2D u_prev;
uniform vec2 u_texel;
uniform float u_decay; // per-frame retention (~0.97)

out float outFoam;

void main()
{
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(u_prev, TexCoords + vec2(x, y) * u_texel).r;
    outFoam = (sum / 9.0) * u_decay;
}
