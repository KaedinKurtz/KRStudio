#version 430 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D sceneTexture;
uniform float threshold;

void main()
{
    vec3 color = texture(sceneTexture, TexCoords).rgb;

    // Calculate the brightness of the pixel
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // If the brightness is below the threshold, discard the pixel (output black)
    if(brightness < threshold)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        // Otherwise, output the original bright color
        FragColor = vec4(color, 1.0);
    }
}