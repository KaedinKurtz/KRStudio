#version 430 core
out vec4 FragColor;
in vec2 TexCoords;

// Input from your G-Buffer
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D uSelectionMask; 

// Parameters to control the effect
uniform vec2 uTexelSize;       // vec2(1.0 / width, 1.0 / height)
uniform vec3 uOutlineColor;
uniform float uNormalThreshold; // How sensitive to creases (e.g., 0.1 - 0.5)
uniform float uDepthThreshold;  // How sensitive to silhouettes (e.g., 0.05 - 0.2)

// Sobel kernels (optional, but good for quality)
const mat3 sobelX = mat3(-1, 0, 1, -2, 0, 2, -1, 0, 1);
const mat3 sobelY = mat3(-1, -2, -1, 0, 0, 0, 1, 2, 1);

void main()
{
    if (texture(uSelectionMask, TexCoords).r < 0.5) {
        discard;
    }

    // --- Normal Edge Detection (for creases) ---
    vec3 normal_grad_x = vec3(0.0);
    vec3 normal_grad_y = vec3(0.0);
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            vec3 n = texture(gNormal, TexCoords + vec2(i, j) * uTexelSize).rgb;
            normal_grad_x += n * sobelX[i+1][j+1];
            normal_grad_y += n * sobelY[i+1][j+1];
        }
    }
    // The length of the gradient vector tells us the edge strength
    float normalEdge = length(normal_grad_x) + length(normal_grad_y);


    // --- Depth Edge Detection (for silhouettes) ---
    float depth_grad_x = 0.0;
    float depth_grad_y = 0.0;
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            // We use the 'z' component of the world position
            float d = texture(gPosition, TexCoords + vec2(i, j) * uTexelSize).z;
            depth_grad_x += d * sobelX[i+1][j+1];
            depth_grad_y += d * sobelY[i+1][j+1];
        }
    }
    float depthEdge = length(vec2(depth_grad_x, depth_grad_y));

    // --- Combine and Output ---
    if (normalEdge > uNormalThreshold || depthEdge > uDepthThreshold) {
        FragColor = vec4(uOutlineColor, 1.0);
    } else {
        discard;
    }
}