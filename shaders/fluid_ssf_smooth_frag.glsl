#version 430 core

// Pass 2: narrow-range depth filter (Truong & Yuksel 2018), separable
// approximation. Clamps far-side outliers instead of rejecting them
// (preserves curved silhouettes) and rejects near-side foreground leaks.

in vec2 TexCoords;

uniform sampler2D u_depth;     // view-space z (0 = no fluid)
uniform vec2 u_dir;            // (texelX, 0) or (0, texelY)
uniform float u_particleRadius;
uniform float u_projScaleY;    // proj[1][1] * targetHeight / 2
uniform float u_maxSigma;      // pixel clamp on the kernel width (12 Low, 32 High)

out float outDepth;

void main()
{
    float zi = texture(u_depth, TexCoords).r;
    if (zi <= 0.0) { outDepth = 0.0; return; }

    float r = u_particleRadius;
    float delta = 10.0 * r; // narrow-range window
    float mu = 1.0 * r;     // far-side clamp offset

    // World-space kernel (zeta = 0.7r) projected to pixels at this depth.
    // The clamp must exceed the projected sprite radius (~1.5r) or each
    // sprite's dome survives filtering and refraction goes scaly.
    float sigma = clamp(0.7 * r * u_projScaleY / zi, 1.0, u_maxSigma);
    int radius = int(ceil(2.5 * sigma));

    float sum = zi;
    float wsum = 1.0;
    for (int t = 1; t <= radius; ++t) {
        float w = exp(-float(t * t) / (2.0 * sigma * sigma));
        for (int s = -1; s <= 1; s += 2) {
            float zj = texture(u_depth, TexCoords + u_dir * float(t * s)).r;
            if (zj <= 0.0) continue;
            if (zj > zi + delta) continue;          // foreground leak: reject
            if (zj < zi - delta) zj = zi - mu;      // far side: clamp, keep
            sum += w * zj;
            wsum += w;
        }
    }
    outDepth = sum / wsum;
}
