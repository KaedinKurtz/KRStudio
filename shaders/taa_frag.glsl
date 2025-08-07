#version 450 core
uniform sampler2D u_currTex, u_historyTex;
uniform sampler2D u_motionTex;
uniform sampler2D u_depthTex, u_historyDepth;
uniform float     u_alpha;
uniform vec2      resolution;
uniform float     depthThreshold;   // e.g. 0.01
uniform float     sharpenAmount;    // e.g. 0.2

in  vec2 TexCoords;
out vec4 FragColor;

void main() {
    vec3 curr = texture(u_currTex, TexCoords).rgb;

    // 1) reproject & clamp UV
    vec2 motion = texture(u_motionTex, TexCoords).xy;
    vec2 histUV = clamp(
        TexCoords + motion / resolution,
        vec2(0.0), vec2(1.0)
    );
    vec3 hist = texture(u_historyTex, histUV).rgb;

    // 2) depth?based history rejection
    float dCurr = texture(u_depthTex, TexCoords).r;
    float dHist = texture(u_historyDepth, histUV).r;
    if (abs(dCurr - dHist) > depthThreshold) {
        hist = curr;
    }

    // 3) color clamp
    vec3 minC = curr * 0.95, maxC = curr * 1.05;
    vec3 clamped = clamp(hist, minC, maxC);

    // 4) temporal blend
    vec3 blended = mix(curr, clamped, u_alpha);

    // 5) simple sharpen
    vec2 px = 1.0 / resolution;
    vec3 nbr = texture(u_currTex, TexCoords + vec2(px.x,0)).rgb
             + texture(u_currTex, TexCoords - vec2(px.x,0)).rgb
             + texture(u_currTex, TexCoords + vec2(0,px.y)).rgb
             + texture(u_currTex, TexCoords - vec2(0,px.y)).rgb;
    vec3 highpass = nbr * 0.25 - blended;
    vec3 color    = blended + sharpenAmount * highpass;

    FragColor = vec4(color, 1.0);
}
