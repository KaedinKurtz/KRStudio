#version 430 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D uMaskTexture;
uniform vec2 uTexelSize;      // vec2(1.0 / width, 1.0 / height)
uniform vec3 uOutlineColor;
uniform float uThickness;     // Example: 1.0 to 3.0

void main()
{
    float centerPixel = texture(uMaskTexture, TexCoords).r;

    // Only proceed if the current pixel is inside the mask.
    // This immediately rejects 99% of the screen and the background.
    if (centerPixel < 0.5) {
        discard;
    }

    // Sample the four direct neighbors (up, down, left, right)
    float up    = texture(uMaskTexture, TexCoords + vec2(0.0, uTexelSize.y * uThickness)).r;
    float down  = texture(uMaskTexture, TexCoords - vec2(0.0, uTexelSize.y * uThickness)).r;
    float left  = texture(uMaskTexture, TexCoords - vec2(uTexelSize.x * uThickness, 0.0)).r;
    float right = texture(uMaskTexture, TexCoords + vec2(uTexelSize.x * uThickness, 0.0)).r;

    // Find the minimum value from the neighbors. If any neighbor is
    // outside the mask (0.0), this value will be 0.0.
    float minNeighbor = min(min(up, down), min(left, right));

    // If our center pixel is "on" (white) but it has a neighbor that is
    // "off" (black), then this is an edge pixel.
    if (minNeighbor < 0.5) {
        FragColor = vec4(uOutlineColor, 1.0);
    } else {
        // This is an interior pixel of the mask, not an edge.
        discard;
    }
}