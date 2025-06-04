#version 330 core
out vec4 FragColor;

in vec3 v_worldPos;

uniform vec3 u_color;
uniform vec3 u_cameraPos;

// Fog Uniforms
uniform bool u_useFog;
uniform vec3 u_fogColor;
uniform float u_fogStartDistance;
uniform float u_fogEndDistance;

void main()
{
    vec3 finalColor = u_color;
    float finalAlpha = 1.0;

    if (u_useFog) {
        float distToCam = length(v_worldPos - u_cameraPos);
        // fogFactor is 0 when close, 1 when far
        float fogFactor = smoothstep(u_fogStartDistance, u_fogEndDistance, distToCam);
        
        // Blend the original color with the fog color
        finalColor = mix(finalColor, u_fogColor, fogFactor);
        // Also fade out the alpha to ensure it disappears completely
        finalAlpha *= (1.0 - fogFactor);
    }
    
    if (finalAlpha < 0.01) {
        discard;
    }

    FragColor = vec4(finalColor, finalAlpha);
}