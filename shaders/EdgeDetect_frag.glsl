#version 430 core

layout(location = 0) out vec4 FragColor;
in  vec2 TexCoords;

uniform sampler2D uDepthTex;
uniform float      uWidth;
uniform float      uHeight;

void main() {
    float dx = 1.0 / uWidth;
    float dy = 1.0 / uHeight;

    // read linear depth via z?component of world?position
    float c = texture(uDepthTex, TexCoords).z;
    float l = texture(uDepthTex, TexCoords + vec2(-dx,  0)).z;
    float r = texture(uDepthTex, TexCoords + vec2(+dx,  0)).z;
    float u = texture(uDepthTex, TexCoords + vec2( 0, +dy)).z;
    float d = texture(uDepthTex, TexCoords + vec2( 0, -dy)).z;

    // simple depth gradient
    float gx = r - l;
    float gy = u - d;
    float edge = length(vec2(gx, gy)) * 50.0; // scale to taste

    FragColor = vec4(vec3(edge), 1.0);
}
