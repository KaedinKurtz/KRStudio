#version 430 core

in  vec2 vUV;
layout(location = 0) out vec4 fragColor;

uniform sampler2D sceneTexture;
uniform sampler2D glowTexture;
uniform float     glowIntensity = 1.0;
uniform float     exposure      = 1.0;
uniform float     saturation    = 1.2; // NEW: Saturation control. 1.0 is normal, > 1.0 boosts saturation.

vec3 ACESFilmic(vec3 color)
{
    color *= 0.9;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 scene = texture(sceneTexture, vUV).rgb;
    vec3 glow  = texture(glowTexture , vUV).rgb * glowIntensity;
    vec3 blended = 1.0 - (1.0 - scene) * (1.0 - glow);

    // 1. Apply tone mapping
    vec3 mapped = ACESFilmic(blended * exposure);

    // 2. NEW: Apply saturation adjustment
    // Calculate the grayscale (luminance) of the color
    float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    vec3 grayscale = vec3(luma);
    
    // Linearly interpolate between the grayscale and original color
    vec3 saturated = mix(grayscale, mapped, saturation);

    fragColor = vec4(saturated, 1.0);
}