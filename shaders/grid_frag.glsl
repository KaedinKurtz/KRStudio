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

// --- Axes Uniforms (if you re-enable axes drawing logic later) ---
uniform bool u_showAxes;
uniform vec3 u_xAxisColor;
uniform vec3 u_zAxisColor;
uniform float u_axisLineWidthPixels;

// --- Fog Uniforms (if you re-enable fog logic later) ---
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
    // smoothstep gives 0 if currentCamDistToFocal < startDist, 1 if > endDist.
    // We want the opposite: 1 if close, 0 if far.
    return 1.0 - smoothstep(startDist, endDist, currentCamDistToFocal);
}

// --- FUNCTION: Calculate Line Strength & Anti-Aliasing ---
// Determines the opacity of a line at a given fragment, including AA.
// Handles "Constant Width Lines" and "Equal Width" via desiredCorePixelWidth.
// Handles "Anti-Aliasing" via smoothstep and dynamic AA extension.
float getLineStrength(
    float planeCoord,               // Fragment's coordinate on the current axis (e.g., v_gridPlaneCoord.x)
    float spacing,                  // Spacing of lines for the current grid level
    float worldUnitsPerPixelForAxis,// How many world units one pixel covers on this axis
    float desiredCorePixelWidth,    // Target line width in screen pixels (from u_baseLineWidthPixels)
    float levelVisibilityFactor,    // Overall visibility of this grid level (0.0 to 1.0)
    float camDistToFocalForAACutoff // Camera distance, used for AA adjustments on fine grids
) {
    // Calculate distance from fragment center to the center of the nearest grid line
    float distToLineCenter = abs(fract(planeCoord / spacing + 0.5) - 0.5) * spacing;

    worldUnitsPerPixelForAxis = max(worldUnitsPerPixelForAxis, 0.00001f); // Avoid division by zero

    // 1. CONSTANT SCREEN-SPACE WIDTH: Convert desired pixel width to world units
    float coreHalfWidth_world = worldUnitsPerPixelForAxis * desiredCorePixelWidth * 0.5f;

    // 2. ANTI-ALIASING: Adjust AA "softness" based on visibility and distance (for fine grids)
    float final_aa_fade_factor = pow(levelVisibilityFactor, 2.5); // AA softens as line becomes more visible

    // For very fine grids (e.g., 10cm lines, spacing < 0.5), make AA sharper when camera is distant
    if (spacing < 0.5) {
        float aa_distance_activation = smoothstep(7.0, 6.0, camDistToFocalForAACutoff); // 1 if cam < 6 units, 0 if cam > 7 units
        final_aa_fade_factor *= aa_distance_activation;
    }

    float current_aaExtension_pixels = mix(MIN_AA_EXTENSION_PIXELS, DEFAULT_AA_EXTENSION_PIXELS, final_aa_fade_factor);
    float aaExtension_world = worldUnitsPerPixelForAxis * current_aaExtension_pixels;

    // Define the edges for the anti-aliased line
    float solidEdge   = coreHalfWidth_world;
    float outerAaEdge = coreHalfWidth_world + aaExtension_world;

    // Clamp edges to prevent artifacts and ensure a minimum AA gradient
    outerAaEdge = min(outerAaEdge, spacing * 0.49f); // Don't let AA bleed into half the spacing
    solidEdge   = min(solidEdge, outerAaEdge - (worldUnitsPerPixelForAxis * 0.05f)); // Ensure some AA region
    solidEdge   = max(0.0, solidEdge); // Solid edge cannot be negative

    // Calculate line strength using smoothstep for anti-aliasing
    return 1.0 - smoothstep(solidEdge, outerAaEdge, distToLineCenter);
}

// --- FUNCTION: Calculate Axis Strength (if you re-enable axes) ---
float getAxisStrength(float planeCoord, float worldUnitsPerPixel, float pixelWidth) {
    float distToLineCenter = abs(planeCoord); // Axis is at coordinate 0
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

    // Estimate world units covered by one pixel on grid's X and Z axes
    // Used by getLineStrength to achieve constant screen-space line width.
    vec2 planeUnitsPerPixel = fwidth(v_gridPlaneCoord);

    // --- 1. Draw Grid Levels ---
    // Loop from finest to coarsest (assuming C++ sorts u_levels by increasing spacing)
    for (int i = 0; i < u_numLevels; ++i) {
        if (u_levels[i].spacing <= 0.0) continue;

        // Calculate visibility for this level based on distance
        float levelVisibilityFactor = getLevelVisibilityFactor(
            u_cameraDistanceToFocal,
            u_levels[i].fadeInCameraDistanceEnd,
            u_levels[i].fadeInCameraDistanceStart
        );

        // If level is too faint, skip it
        if (levelVisibilityFactor < 0.01) {
            continue;
        }

        // Calculate line strength (includes AA and constant width logic)
        float lineStrengthX = getLineStrength(v_gridPlaneCoord.x, u_levels[i].spacing, planeUnitsPerPixel.x, u_baseLineWidthPixels, levelVisibilityFactor, u_cameraDistanceToFocal);
        float lineStrengthZ = getLineStrength(v_gridPlaneCoord.y, u_levels[i].spacing, planeUnitsPerPixel.y, u_baseLineWidthPixels, levelVisibilityFactor, u_cameraDistanceToFocal);
        float currentLineHitStrength = max(lineStrengthX, lineStrengthZ); // Pixel is on a line if hit on X or Z

        // If line is too faint at this pixel, skip
        if (currentLineHitStrength < 0.01) {
            continue;
        }
        
        // Combine line's own strength with the level's overall visibility
        float effectiveAlphaForThisLevel = currentLineHitStrength * levelVisibilityFactor;

        if (effectiveAlphaForThisLevel > 0.01) {
            vec3 currentLevelColorRGB = u_levels[i].color;
            
            // 3. PRIORITY DRAWING: Blend current level OVER previously accumulated colors.
            // Since loop is fine-to-coarse, coarser lines will draw over finer ones.
            finalLineColorRGB = currentLevelColorRGB * effectiveAlphaForThisLevel + finalLineColorRGB * (1.0 - effectiveAlphaForThisLevel);
            finalLineAlpha    = effectiveAlphaForThisLevel + finalLineAlpha    * (1.0 - effectiveAlphaForThisLevel);
        }
    }

    // --- 2. Draw Coordinate Axes (Optional - current logic is commented out but can be re-enabled) ---
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
    
    // --- 3. Apply Fog (Optional - current logic is commented out but can be re-enabled) ---
    if (u_useFog) {
        float distToCamFragment = length(v_worldPos - u_cameraPos); 
        float fogFactor = smoothstep(u_fogStartDistance, u_fogEndDistance, distToCamFragment);
        
        finalLineColorRGB = mix(finalLineColorRGB, u_fogColor, fogFactor);
        finalLineAlpha *= (1.0 - fogFactor); // Fog also fades alpha
    }

    // Discard fragment if it's effectively transparent
    if (finalLineAlpha < 0.001) { 
        discard;
    } else {
        FragColor = vec4(finalLineColorRGB, finalLineAlpha);
    }
}
