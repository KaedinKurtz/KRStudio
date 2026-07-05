#version 430 core
// PICK-ID primitive pass (mate-selector P1): B-Rep EDGES (segments) and VERTICES (points)
// as screen-space-expanded instanced quads -- 4 strip corners from gl_VertexID, one instance
// per segment/point. Sub-pixel features get a fat pick footprint (~7-9 px) plus an NDC depth
// bias so a coplanar edge beats its face at equal depth. A vertex instance is a degenerate
// segment (p0 == p1) and expands to a square.
layout(location = 0) in vec3 iP0;      // per-instance: segment start (world)
layout(location = 1) in vec3 iP1;      // per-instance: segment end   (world)
layout(location = 2) in uint iId;      // per-instance: edgeId / vertexId

uniform mat4 uViewProj;                // world -> clip (bodies are world-baked; model on CPU)
uniform vec2 uViewportPx;              // pick target size in pixels
uniform float uHalfWidthPx;            // half footprint in pixels
uniform float uDepthBiasNdc;           // subtracted (scaled by w) so edges beat coplanar faces

flat out uint vId;

void main()
{
    vec4 c0 = uViewProj * vec4(iP0, 1.0);
    vec4 c1 = uViewProj * vec4(iP1, 1.0);
    // Behind-camera guard: collapse the primitive (proper near-plane clipping of expanded
    // quads is P2 polish; candidates are on-screen geometry in practice).
    if (c0.w <= 1e-4 || c1.w <= 1e-4) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vId = 0u; return; }

    vec2 halfVp = 0.5 * uViewportPx;
    vec2 p0px = c0.xy / c0.w * halfVp;
    vec2 p1px = c1.xy / c1.w * halfVp;
    vec2 d = p1px - p0px;
    float len = length(d);
    vec2 t = (len > 1e-4) ? d / len : vec2(1.0, 0.0);
    vec2 n = vec2(-t.y, t.x);

    int corner = gl_VertexID;                          // 0..3 triangle strip
    float end  = (corner >= 2) ? 1.0 : 0.0;
    float side = ((corner & 1) == 1) ? 1.0 : -1.0;

    vec4 base = mix(c0, c1, end);
    // extend past both endpoints too, so endpoints and lone points get the full footprint
    vec2 offPx = n * side * uHalfWidthPx + t * (end * 2.0 - 1.0) * uHalfWidthPx;
    base.xy += offPx / halfVp * base.w;
    base.z  -= uDepthBiasNdc * base.w;

    gl_Position = base;
    vId = iId;
}
