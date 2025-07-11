#version 330 core
out vec4 FragColor;

// Data from the Vertex Shader
in vec3 v_worldPos;
in vec2 v_gridPlaneCoord;

// --- UNIFORMS ---
uniform vec3 u_cameraPos;
uniform float u_distanceToGrid; // Use the camera's zoom distance for stable fading

uniform int u_numLevels;
struct GridLevelUniform {
    float spacing;
    vec3  color;
    float fadeInCameraDistanceEnd;
    float fadeInCameraDistanceStart;
};
uniform GridLevelUniform u_levels[5];

// Feature Toggles
uniform bool u_levelVisible[5];
uniform bool u_isDotted;

uniform float u_baseLineWidthPixels;

// Axes
uniform bool u_showAxes;
uniform vec3 u_xAxisColor;
uniform vec3 u_zAxisColor;
uniform float u_axisLineWidthPixels;

// Fog
uniform bool u_useFog;
uniform vec3 u_fogColor;
uniform float u_fogStartDistance;
uniform float u_fogEndDistance;

const float DEFAULT_AA_EXTENSION_PIXELS = 0.65f;
const float MIN_AA_EXTENSION_PIXELS = 0.0f;

// --- FUNCTIONS ---

float getLevelVisibilityFactor(float currentCamDist, float levelFadeInEnd, float levelFadeInStart) {
    float startDist = min(levelFadeInStart, levelFadeInEnd);
    float endDist   = max(levelFadeInStart, levelFadeInEnd);
    return 1.0 - smoothstep(startDist, endDist, currentCamDist);
}

// This is the original, high-quality anti-aliasing line function, now corrected
// to use the u_distanceToGrid uniform.
float getLineStrength(
        float planeCoord,
        float spacing,
        float worldUnitsPerPixelForAxis,
        float desiredCorePixelWidth,
        float levelVisibilityFactor,
        float camDistToGridForAACutoff)
{
    // ------------------------------------------------------------------
    // 1. distance from fragment to nearest grid line  **in pixels**
    // ------------------------------------------------------------------
    float distToLineCenter_world = abs(fract(planeCoord / spacing + 0.5) - 0.5) * spacing;
    float distToLineCenter_px    = distToLineCenter_world / worldUnitsPerPixelForAxis;

    // ------------------------------------------------------------------
    // 2. core / AA widths  also **in pixels**
    // ------------------------------------------------------------------
    float coreHalf_px  = desiredCorePixelWidth * 0.5;
    float aaExt_px     = mix(MIN_AA_EXTENSION_PIXELS,
                             DEFAULT_AA_EXTENSION_PIXELS,
                             pow(levelVisibilityFactor, 2.5));

    // 3  fade AA away when spacing tiny & camera very close  (unchanged)
    if (spacing < 0.5) {
        float aaCut = smoothstep(7.0, 6.0, camDistToGridForAACutoff);
        aaExt_px *= aaCut;
    }

    // 4  edge positions **in pixels**, then convert back to world units once
    float solidEdge_px = coreHalf_px;
    float outerEdge_px = coreHalf_px + aaExt_px;

    // convert to world units only for the final smoothstep
    float solidEdge_wc =  solidEdge_px * worldUnitsPerPixelForAxis;
    float outerEdge_wc = outerEdge_px * worldUnitsPerPixelForAxis;
    distToLineCenter_world = distToLineCenter_px * worldUnitsPerPixelForAxis;

    return 1.0 - smoothstep(solidEdge_wc, outerEdge_wc, distToLineCenter_world);
}

float getAxisStrength(float planeCoord, float worldUnitsPerPixel, float pixelWidth) {
    float distToLineCenter = abs(planeCoord);
    float coreHalfWidth_world = worldUnitsPerPixel * pixelWidth * 0.5f;
    float aaExtension_world = worldUnitsPerPixel * DEFAULT_AA_EXTENSION_PIXELS;
    float solidEdge   = coreHalfWidth_world;
    float outerAaEdge = coreHalfWidth_world + aaExtension_world;
    solidEdge = min(solidEdge, outerAaEdge - (worldUnitsPerPixel * 0.01f));
    solidEdge = max(0.0, solidEdge);
    return 1.0 - smoothstep(solidEdge, outerAaEdge, distToLineCenter);
}


// --- MAIN SHADER LOGIC ---
void main() {
    vec3 finalLineColorRGB = vec3(0.0);
    float finalLineAlpha = 0.0;

    vec2 planeUnitsPerPixel = fwidth(v_gridPlaneCoord);

    // --- 1. Draw Grid Levels ---
    for (int i = 0; i < u_numLevels; ++i) {
        // Check if this level is toggled on in the UI
        if (!u_levelVisible[i] || u_levels[i].spacing <= 0.0) {
            continue;
        }

        // Calculate visibility based on camera distance
        float levelVisibilityFactor = getLevelVisibilityFactor(
            u_distanceToGrid,
            u_levels[i].fadeInCameraDistanceEnd,
            u_levels[i].fadeInCameraDistanceStart
        );

        if (levelVisibilityFactor < 0.01) {
            continue;
        }

        float lineStrengthX = getLineStrength(v_gridPlaneCoord.x, u_levels[i].spacing, planeUnitsPerPixel.x, u_baseLineWidthPixels, levelVisibilityFactor, u_distanceToGrid);
        float lineStrengthZ = getLineStrength(v_gridPlaneCoord.y, u_levels[i].spacing, planeUnitsPerPixel.y, u_baseLineWidthPixels, levelVisibilityFactor, u_distanceToGrid);
        
        float currentLineHitStrength = u_isDotted ? (lineStrengthX * lineStrengthZ) : max(lineStrengthX, lineStrengthZ);

        if (currentLineHitStrength < 0.01) {
            continue;
        }
        
        float effectiveAlphaForThisLevel = currentLineHitStrength * levelVisibilityFactor;

        if (effectiveAlphaForThisLevel > 0.01) {
            vec3 currentLevelColorRGB = u_levels[i].color;
            // Use standard "over" blending to composite layers correctly
            finalLineColorRGB = mix(finalLineColorRGB, currentLevelColorRGB, effectiveAlphaForThisLevel);
            finalLineAlpha    = effectiveAlphaForThisLevel + finalLineAlpha * (1.0 - effectiveAlphaForThisLevel);
        }
    }

    // --- 2. Draw Coordinate Axes ---
    if (u_showAxes) {
        float zAxisStrength = getAxisStrength(v_gridPlaneCoord.x, planeUnitsPerPixel.x, u_axisLineWidthPixels);
        if (zAxisStrength > 0.01) {
            finalLineColorRGB = mix(finalLineColorRGB, u_zAxisColor, zAxisStrength);
            finalLineAlpha = max(finalLineAlpha, zAxisStrength);
        }

        float xAxisStrength = getAxisStrength(v_gridPlaneCoord.y, planeUnitsPerPixel.y, u_axisLineWidthPixels);
        if (xAxisStrength > 0.01) {
            finalLineColorRGB = mix(finalLineColorRGB, u_xAxisColor, xAxisStrength);
            finalLineAlpha = max(finalLineAlpha, xAxisStrength);
        }
    }
    
    // --- 3. Apply Fog ---
    if (u_useFog) {
        float distToCamFragment = length(v_worldPos - u_cameraPos);
        float fogFactor = smoothstep(u_fogStartDistance, u_fogEndDistance, distToCamFragment);
        finalLineColorRGB = mix(finalLineColorRGB, u_fogColor, fogFactor);
    }

    // --- Final Output ---
    if (finalLineAlpha < 0.001) {
        discard;
    } else {
        FragColor = vec4(finalLineColorRGB, finalLineAlpha);
    }
}