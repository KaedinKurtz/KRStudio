#version 430 core
// HIGHLIGHT COMPOSITE (mate-selector P2): CAD-style hover/selection highlight from the
// pick-ID buffer. Pixel-exact face FILL tint + a crisp contour from ID DISCONTINUITY
// (categorical compare -- ids are labels, not magnitudes, so this beats a Sobel).
// Runs post-tonemap (LDR) and alpha-blends over the finished frame, so no exposure
// compensation is needed. Edge/vertex features highlight through the same compare:
// their fat pick footprints become the glow line/dot (the Fusion look).
uniform usampler2D uIdTex;
uniform uvec2 uHover;          // (entityId+1, typeBits|featureId+1); (0,0) = none
uniform int   uSelCount;       // committed selections (FIFO-capped at 8)
uniform uvec2 uSel[8];
uniform vec4  uHoverColor;     // tint colors (alpha = fill strength)
uniform vec4  uSelColor;
uniform vec2  uTexSize;
uniform float uInvExposure;    // pre-tonemap overlay: pre-divide by the photometric exposure

in vec2 TexCoords;
out vec4 FragColor;

bool matches(uvec2 id, uvec2 q) { return q.x != 0u && id == q; }

void classify(ivec2 px, out bool hov, out bool sel)
{
    uvec2 id = texelFetch(uIdTex, px, 0).rg;
    hov = matches(id, uHover);
    sel = false;
    for (int i = 0; i < uSelCount; ++i) sel = sel || matches(id, uSel[i]);
}

void main()
{
    ivec2 sz = ivec2(uTexSize) - ivec2(1);
    ivec2 px = ivec2(TexCoords * uTexSize);
    bool hov, sel;
    classify(px, hov, sel);

    // contour: this pixel is matched and any 4-neighborhood sample (2px reach) is not
    float edge = 0.0;
    if (hov || sel) {
        const ivec2 offs[4] = ivec2[4](ivec2(2, 0), ivec2(-2, 0), ivec2(0, 2), ivec2(0, -2));
        for (int k = 0; k < 4; ++k) {
            bool nh, ns;
            classify(clamp(px + offs[k], ivec2(0), sz), nh, ns);
            if ((hov && !nh) || (sel && !ns && !nh)) edge = 1.0;
        }
    }

    if (!hov && !sel) { FragColor = vec4(0.0); return; }
    vec4 fill = hov ? uHoverColor : uSelColor;
    // the contour brightens toward white and goes fully opaque
    vec3 lineRgb = mix(fill.rgb, vec3(1.0), 0.55);
    FragColor = mix(vec4(fill.rgb, fill.a), vec4(lineRgb, 0.95), edge);
    FragColor.rgb *= uInvExposure;   // survive the tonemap like the other overlays
}
