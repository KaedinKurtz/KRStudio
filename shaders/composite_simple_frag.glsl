#version 430 core
in vec2 TexCoords;
out vec4 fragColor;
uniform sampler2D screenTexture;
void main() {
    fragColor = texture(screenTexture, TexCoords);
}
