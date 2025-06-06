#version 330 core
out vec4 FragColor;

in vec3 v_worldPos;         // World position of the fragment
in vec2 v_gridPlaneCoord;     // Plane coordinates (e.g., local X and Z of the grid)

// --- UNIFORMS ---
uniform vec3 u_cameraPos;             // Camera's world position
uniform float u_cameraDistanceToFocal; // Distance from camera to its focal point

uniform int u_numLevels;              // Number of grid levels to render

// Struct for grid level properties
struct GridLevelUniform {
    float spacing;                  // Spacing of lines for this level
    vec3 color;                     // Color of lines for this level
    float fadeInCameraDistanceEnd;  // Distance at which this level is fully faded out / starts fading in
    float fadeInCameraDistanceStart;// Distance at which this level is fully visible
};
uniform GridLevelUniform u_levels[5]; // Array of grid levels (ensure C++ MAX_GRID_LEVELS matches)

uniform float u_baseLineWidthPixels;  // Desired "core" line width in screen pixels

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
const float DEFAULT_AA_EXTENSION_PIXELS = 0.65f; // Softer AA when fully visible
const float MIN_AA_EXTENSION_PIXELS = 0.0f;   // Sharper (or no) AA when faint/distant

// --- FUNCTION: Calculate Level Visibility ---
// Determines how visible a grid level should be based on camera distance.
// Returns 1.0 for fully visible, 0.0 for fully faded out.
float getLevelVisibilityFactor(float currentCamDistToFocal, float levelFadeInEnd, float levelFadeInStart) {
    float startDist = min(levelFadeInStart, levelFadeInEnd);
    float endDist   = max(levelFadeInStart, levelFadeInEnd);
    return 1.0 - smoothstep(startDist, endDist, currentCamDistToFocal);
}

// --- FUNCTION: Calculate Line Strength & Anti-Aliasing ---
float getLineStrength(
    float planeCoord,               // Fragment's coordinate on the current axis (e.g., v_gridPlaneCoord.x)
    float spacing,                  // Spacing of lines for the current grid level
    float worldUnitsPerPixelForAxis,// How many world units one pixel covers on this axis
    float desiredCorePixelWidth,    // Target line width in screen pixels (from u_baseLineWidthPixels)
    float levelVisibilityFactor,    // Overall visibility of this grid level (0.0 to 1.0)
    float camDistToFocalForAACutoff // Camera distance, used for AA adjustments on fine grids
) {
    float distToLineCenter = abs(fract(planeCoord / spacing + 0.5) - 0.5) * spacing;
    worldUnitsPerPixelForAxis = max(worldUnitsPerPixelForAxis, 0.00001f); 
    float coreHalfWidth_world = worldUnitsPerPixelForAxis * desiredCorePixelWidth * 0.5f;

    float final_aa_fade_factor = pow(levelVisibilityFactor, 2.5); 

    if (spacing < 0.5) { 
        float aa_distance_activation = smoothstep(7.0, 6.0, camDistToFocalForAACutoff); 
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
        if (u_levels[i].spacing <= 0.0) continue;

        float levelVisibilityFactor = getLevelVisibilityFactor(
            u_cameraDistanceToFocal,
            u_levels[i].fadeInCameraDistanceEnd,
            u_levels[i].fadeInCameraDistanceStart
        );

        if (levelVisibilityFactor < 0.01) {
            continue;
        }

        float lineStrengthX = getLineStrength(v_gridPlaneCoord.x, u_levels[i].spacing, planeUnitsPerPixel.x, u_baseLineWidthPixels, levelVisibilityFactor, u_cameraDistanceToFocal);
        float lineStrengthZ = getLineStrength(v_gridPlaneCoord.y, u_levels[i].spacing, planeUnitsPerPixel.y, u_baseLineWidthPixels, levelVisibilityFactor, u_cameraDistanceToFocal);
        float currentLineHitStrength = max(lineStrengthX, lineStrengthZ); 

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
        // Use v_worldPos (output from vertex shader, interpolated for fragment) for per-fragment distance
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
