#version 330 core
out vec4 FragColor;

// From Vertex Shader
in vec3 v_worldPos;
in vec2 v_gridPlaneCoord;

// Uniforms (unchanged)
uniform vec3  u_cameraPos;
uniform float u_distanceToGrid;

uniform int u_numLevels;
struct GridLevelUniform {
    float spacing;
    vec3  color;
    float fadeInCameraDistanceEnd;
    float fadeInCameraDistanceStart;
};
uniform GridLevelUniform u_levels[5];

uniform bool u_levelVisible[5];
uniform bool u_isDotted;

uniform float u_baseLineWidthPixels;

// Axes
uniform bool  u_showAxes;
uniform vec3  u_xAxisColor;
uniform vec3  u_zAxisColor;
uniform float u_axisLineWidthPixels;

// Fog
uniform bool  u_useFog;
uniform vec3  u_fogColor;
uniform float u_fogStartDistance;
uniform float u_fogEndDistance;

// Anti-aliasing constants
const float DEFAULT_AA_EXTENSION_PIXELS = 1.0; // A 1-pixel feather provides a nice soft edge.
const float MIN_AA_EXTENSION_PIXELS     = 0.0;

// Helper: Get level visibility factor based on camera distance for fading
float getLevelVisibilityFactor(float camDist, float fadeEnd, float fadeStart) {
    float s = min(fadeStart, fadeEnd), e = max(fadeStart, fadeEnd);
    return 1.0 - smoothstep(s, e, camDist);
}

// State-of-the-art anti-aliased line strength calculation
float getLineStrength(
    float planeCoord, // The grid plane coordinate (e.g., v_gridPlaneCoord.x)
    float spacing,    // Spacing for this grid level
    float corePx,     // Target line width in pixels
    float visFactor,  // Visibility factor from fading
    float camDist)    // Camera distance for fine-tuning
{
    // Calculate the rate of change of the plane coordinate in screen space.
    // This gives us the size of one world unit in pixels at this fragment.
    // This is the core of the state-of-the-art technique.
    float worldUnitsPerPixel = length(vec2(dFdx(planeCoord), dFdy(planeCoord)));
    if (worldUnitsPerPixel < 1e-6) { // Avoid division by zero if the gradient is near-zero.
        worldUnitsPerPixel = 1e-6;
    }

    // Calculate distance to the nearest grid line in world units.
    float distWorld = abs(fract(planeCoord/spacing + 0.5) - 0.5) * spacing;
    
    // Convert world distance to screen-space pixel distance.
    float distPx = distWorld / worldUnitsPerPixel;

    // Calculate anti-aliasing feathering amount in pixels.
    float halfCore = corePx * 0.5;
    float aaExt    = mix(MIN_AA_EXTENSION_PIXELS,
                         DEFAULT_AA_EXTENSION_PIXELS,
                         pow(visFactor, 2.5));

    // Special handling for very fine grids to reduce aliasing noise from afar.
    if (spacing < 0.5) {
        float aaCut = smoothstep(7.0, 6.0, camDist);
        aaExt *= aaCut;
    }

    // Use smoothstep to create a soft edge. The line is solid inside `halfCore`
    // and fades out over `aaExt` pixels.
    return 1.0 - smoothstep(halfCore, halfCore + aaExt, distPx);
}

// State-of-the-art anti-aliased axis line strength calculation
float getAxisStrength(float planeCoord, float pxWidth) {
    // Calculate distance to the axis (at coordinate 0) in world units.
    float distWorld = abs(planeCoord);

    // Calculate the rate of change of the plane coordinate in screen space.
    float worldUnitsPerPixel = length(vec2(dFdx(planeCoord), dFdy(planeCoord)));
    if (worldUnitsPerPixel < 1e-6) { // Avoid division by zero.
        worldUnitsPerPixel = 1e-6;
    }

    // Convert world distance to screen-space pixel distance.
    float distPx = distWorld / worldUnitsPerPixel;

    // Use smoothstep for anti-aliasing.
    float halfWidth = pxWidth * 0.5;
    float aa_ext    = DEFAULT_AA_EXTENSION_PIXELS;
    return 1.0 - smoothstep(halfWidth, halfWidth + aa_ext, distPx);
}

void main() {
    float camDist = length(v_worldPos - u_cameraPos);

    vec3  outColor = vec3(0.0);
    float outAlpha = 0.0;

    // 1) Grid levels
    for (int i = 0; i < u_numLevels; ++i) {
        if (!u_levelVisible[i] || u_levels[i].spacing <= 0.0) continue;
        
        float vf = getLevelVisibilityFactor(camDist,
            u_levels[i].fadeInCameraDistanceEnd,
            u_levels[i].fadeInCameraDistanceStart);
        if (vf < 0.01) continue;

        // Calculate line strength for both X and Z grid lines using the new method.
        float sx = getLineStrength(v_gridPlaneCoord.x, u_levels[i].spacing, u_baseLineWidthPixels, vf, camDist);
        float sz = getLineStrength(v_gridPlaneCoord.y, u_levels[i].spacing, u_baseLineWidthPixels, vf, camDist);

        float hit = u_isDotted ? (sx*sz) : max(sx,sz);
        if (hit < 0.01) continue;

        float alpha = hit * vf;
        vec3  col   = u_levels[i].color;
        
        // Blend grid levels using "over" blending.
        outColor = mix(outColor, col, alpha);
        outAlpha = alpha + outAlpha*(1.0-alpha);
    }

    // 2) Axes
    if (u_showAxes) {
        // Calculate axis strength using the new method.
        float za = getAxisStrength(v_gridPlaneCoord.x, u_axisLineWidthPixels);
        if (za > 0.01) {
            outColor = mix(outColor, u_zAxisColor, za);
            outAlpha = max(outAlpha, za);
        }
        float xa = getAxisStrength(v_gridPlaneCoord.y, u_axisLineWidthPixels);
        if (xa > 0.01) {
            outColor = mix(outColor, u_xAxisColor, xa);
            outAlpha = max(outAlpha, xa);
        }
    }

    // 3) Fog
    if (u_useFog) {
        float fd = smoothstep(u_fogStartDistance, u_fogEndDistance, camDist);
        outColor = mix(outColor, u_fogColor, fd);
    }

    // 4) Final Output
    if (outAlpha < 0.001) {
        discard;
    } else {
        FragColor = vec4(outColor, outAlpha);
    }
}