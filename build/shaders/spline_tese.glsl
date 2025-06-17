#version 410 core
layout (isoline, equal_spacing, cw) in;

uniform int  u_type;
uniform int  u_cpCount;
uniform vec3 u_cp[32];

vec3 evalCatmullRom(float t);
vec3 evalBezier    (float t);
vec3 evalLinear    (float t);

vec3 evalCurve(float t)
{
    if      (u_type == 0) return evalLinear(t);
    else if (u_type == 1) return evalCatmullRom(t);
    else                  return evalBezier(t);
}

void main()
{
    float t = gl_TessCoord.x;           // 0-1 on this isoline
    vec3  p = evalCurve(t);
    gl_Position = vec4(p, 1.0);
}

/* -- helpers (very compact) -------------------------------------------------- */
vec3 p(int i) { return u_cp[ clamp(i,0,u_cpCount-1) ]; }

vec3 evalLinear(float t)
{
    float ft = t * (u_cpCount-1);
    int   i  = int(floor(ft));
    float u  = fract(ft);
    return mix(p(i), p(i+1), u);
}

vec3 evalBezier(float t)
{
    vec3 b[32];
    for (int i=0;i<u_cpCount;++i) b[i] = u_cp[i];
    for (int k=u_cpCount-1;k>0;--k)
        for (int i=0;i<k;++i) b[i] = mix(b[i], b[i+1], t);
    return b[0];
}

vec3 evalCatmullRom(float t)
{
    float ft = t * (u_cpCount-3);
    int   i  = int(floor(ft))+1;
    float u  = fract(ft);

    vec3 P0 = p(i-1), P1 = p(i), P2 = p(i+1), P3 = p(i+2);
    float u2 = u*u, u3 = u2*u;
    return 0.5 * ( (2.*P1) +
                   (-P0 + P2)*u +
                   (2.*P0 -5.*P1 +4.*P2 -P3)*u2 +
                   (-P0 +3.*P1 -3.*P2 +P3)*u3 );
}