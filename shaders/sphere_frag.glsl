#version 330 core
out vec4 FragColor;

in vec3 v_worldPos;      // World position of the fragment
in vec2 v_gridPlaneCoord;  // Plane coordinates (e.g., local X and Z of the grid)

// --- UNIFORMS ---
uniform vec3 u_cameraPos;

// FIX: Use perpendicular distance to the grid's plane for fading
uniform float u_distanceToGridPlane;

uniform int u_numLevels;            // Number of grid levels to render

// Struct for grid level properties
struct GridLevelUniform {
    float spacing;
    vec3 color; // Per-level color is sent within this struct
    float fadeInCameraDistanceEnd;
    float fadeInCameraDistanceStart;
};
uniform GridLevelUniform u_levels[5];

// FIX: New uniforms for per-level visibility and style
uniform bool u_levelVisible[5];
uniform bool u_isDotted;

uniform float u_baseLineWidthPixels;

// --- Axes Uniforms ---
uniform bool u_showAxes;
uniform vec3 u_xAxisColor;
uniform vec3 u_zAxisColor;
uniform float u_axisLineWidthPixels;

// --- Fog Uniforms ---
uniform bool u_useFog;
uniform vec3 u_fogColor;
uniform float u_fogStartDistance;
uniform float u_fogEndDistance;

// --- CONSTANTS FOR ANTI-ALIASING ---
const float DEFAULT_AA_EXTENSION_PIXELS = 0.65f;
const float MIN_AA_EXTENSION_PIXELS = 0.0f;

// --- FUNCTION: Calculate Level Visibility ---
// Determines how visible a grid level should be based on camera distance.
// FIX: Now uses the per-grid perpendicular distance.
float getLevelVisibilityFactor(float currentGridDist, float levelFadeInEnd, float levelFadeInStart) {
    float startDist = min(levelFadeInStart, levelFadeInEnd);
    float endDist   = max(levelFadeInStart, levelFadeInEnd);
    return 1.0 - smoothstep(startDist, endDist, currentGridDist);
}

// --- FUNCTION: Calculate Line Strength & Anti-Aliasing ---
// FIX: Now uses the per-grid perpendicular distance for AA cutoff.
float getLineStrength(
    float planeCoord,
    float spacing,
    float worldUnitsPerPixelForAxis,
    float desiredCorePixelWidth,
    float levelVisibilityFactor,
    float camDistToGridForAACutoff
) {
    float distToLineCenter = abs(fract(planeCoord / spacing + 0.5) - 0.5) * spacing;
    worldUnitsPerPixelForAxis = max(worldUnitsPerPixelForAxis, 0.00001f);
    float coreHalfWidth_world = worldUnitsPerPixelForAxis * desiredCorePixelWidth * 0.5f;

    float final_aa_fade_factor = pow(levelVisibilityFactor, 2.5);

    if (spacing < 0.5) {
        float aa_distance_activation = smoothstep(7.0, 6.0, camDistToGridForAACutoff);
        final_aa_fade_factor *= aa_distance_activation;
    }

    float current_aaExtension_pixels = mix(MIN_AA_EXTENSION_PIXELS, DEFAULT_AA_EXTENSION_PIXELS, final_aa_fade_factor);
    float aaExtension_world = worldUnitsPerPixelForAxis * current_aaExtension_pixels;

    float solidEdge   = coreHalfWidth_world;
    float outerAaEdge = coreHalfWidth_world + aaExtension_world;

    outerAaEdge = min(outerAaEdge, spacing * 0.49f);
    solidEdge   = min(solidEdge, outerAaEdge - (worldUnitsPerPixelForAxis * 0.05f));
    solidEdge   = max(0.0, solidEdge);

    return 1.0 - smoothstep(solidEdge, outerAaEdge, distToLineCenter);
}

// --- FUNCTION: Calculate Axis Strength ---
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
        // FIX: Check per-level visibility toggle
        if (!u_levelVisible[i] || u_levels[i].spacing <= 0.0) {
            continue;
        }

        // FIX: Use perpendicular distance for fading
        float levelVisibilityFactor = getLevelVisibilityFactor(
            u_distanceToGridPlane,
            u_levels[i].fadeInCameraDistanceEnd,
            u_levels[i].fadeInCameraDistanceStart
        );

        if (levelVisibilityFactor < 0.01) {
            continue;
        }

        float lineStrengthX = getLineStrength(v_gridPlaneCoord.x, u_levels[i].spacing, planeUnitsPerPixel.x, u_baseLineWidthPixels, levelVisibilityFactor, u_distanceToGridPlane);
        float lineStrengthZ = getLineStrength(v_gridPlaneCoord.y, u_levels[i].spacing, planeUnitsPerPixel.y, u_baseLineWidthPixels, levelVisibilityFactor, u_distanceToGridPlane);
        
        // FIX: Logic for Lines vs. Dots
        float currentLineHitStrength = 0.0;
        if (u_isDotted) {
            // For dots, multiply strengths to get only the intersections.
            currentLineHitStrength = lineStrengthX * lineStrengthZ;
        } else {
            // For lines, take the stronger of the two lines.
            currentLineHitStrength = max(lineStrengthX, lineStrengthZ);
        }

        if (currentLineHitStrength < 0.01) {
            continue;
        }
        
        float effectiveAlphaForThisLevel = currentLineHitStrength * levelVisibilityFactor;

        if (effectiveAlphaForThisLevel > 0.01) {
            vec3 currentLevelColorRGB = u_levels[i].color;
            finalLineColorRGB = currentLevelColorRGB * effectiveAlphaForThisLevel + finalLineColorRGB * (1.0 - effectiveAlphaForThisLevel);
            finalLineAlpha    = effectiveAlphaForThisLevel + finalLineAlpha    * (1.0 - effectiveAlphaForThisLevel);
        }
    }

    // --- 2. Draw Coordinate Axes ---
    if (u_showAxes) {
        float zAxisStrength = getAxisStrength(v_gridPlaneCoord.x, planeUnitsPerPixel.x, u_axisLineWidthPixels);
        if (zAxisStrength > 0.01) {
            finalLineColorRGB = mix(finalLineColorRGB, u_zAxisColor, zAxisStrength);
            finalLineAlpha = mix(finalLineAlpha, 1.0, zAxisStrength);
        }

        float xAxisStrength = getAxisStrength(v_gridPlaneCoord.y, planeUnitsPerPixel.y, u_axisLineWidthPixels);
        if (xAxisStrength > 0.01) {
            finalLineColorRGB = mix(finalLineColorRGB, u_xAxisColor, xAxisStrength);
            finalLineAlpha = mix(finalLineAlpha, 1.0, xAxisStrength);
        }
    }
    
    // --- 3. Apply Fog ---
    if (u_useFog) {
        float distToCamFragment = length(v_worldPos - u_cameraPos);
        float fogFactor = smoothstep(u_fogStartDistance, u_fogEndDistance, distToCamFragment);
        
        finalLineColorRGB = mix(finalLineColorRGB, u_fogColor, fogFactor);
        finalLineAlpha *= (1.0 - fogFactor);
    }

    if (finalLineAlpha < 0.001) {
        discard;
    } else {
        FragColor = vec4(finalLineColorRGB, finalLineAlpha);
    }
}