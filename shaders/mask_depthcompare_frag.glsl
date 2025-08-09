#version 430 core
layout(location=0) out vec4 FragColor;

uniform sampler2D uDepth;         // bind ctx.targetFBOs.finalDepthTexture here
uniform vec2      uViewportSize;  // (pp[0].w, pp[0].h)

void main() {
    float sceneZ = texture(uDepth, gl_FragCoord.xy / uViewportSize).r;
    float myZ    = gl_FragCoord.z;  // same NDC depth space as the target FBO
    float eps = 2e-3;               // relax if needed
    if (abs(sceneZ - myZ) < eps) {
        FragColor = vec4(1.0);      // white mask
    } else {
        discard;
    }
}
