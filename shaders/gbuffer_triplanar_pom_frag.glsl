#version 450 core

layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout (location = 4) out vec4 gEmissive;

layout (location = 0) in vec3 fs_WorldPos;
layout (location = 1) in vec3 fs_WorldNormal;
layout (location = 2) in vec3 fs_ViewDir_tangent;

struct Material {
    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D aoMap;
    sampler2D metallicMap;
    sampler2D roughnessMap;
    sampler2D emissiveMap;
    sampler2D heightMap; // white = highest, black = lowest
};
uniform Material material;
uniform float u_texture_scale;
uniform float u_height_scale;

// ----------------------- Tunables (no new uniforms) -----------------------
const float HEIGHT_CONTRAST   = 2.4;   // 2.0–3.0 hardens edges
const float HEIGHT_GAMMA      = 1.0;   // keep 1.0; 0.85–1.0 can help
const int   BINSEARCH_ITERS   = 5;

const float SHADOW_STRENGTH   = 0.65;  // 0=no extra darkening, 1=full
const float SHADOW_BIAS       = 0.015; // push-off to avoid self-hit acne
const int   SHADOW_STEPS_MIN  = 6;     // tiny extra loop; keep small
const int   SHADOW_STEPS_MAX  = 16;

// ----------------------------- Helpers ------------------------------------
float shape_height(float h) {
    h = clamp((h - 0.5) * HEIGHT_CONTRAST + 0.5, 0.0, 1.0);
    return pow(h, HEIGHT_GAMMA);
}

vec3 decode_normal(vec3 nTex) { return normalize(nTex * 2.0 - 1.0); }

vec3 orient_plane_normal(vec3 nTS, vec3 T, vec3 B, vec3 N) {
    mat3 TBN = mat3(normalize(T), normalize(B), normalize(N));
    return normalize(TBN * nTS);
}

// Plane-aware POM with binary search; returns offset and self-occlusion
vec2 pom_plane_with_shadow(
    sampler2D disp, vec2 uv, vec2 vParallel, float vNormal,
    out float selfOcc)
{
    float nz = max(abs(vNormal), 1e-4);

    // Adaptive coarse steps: more at grazing
    float graze = 1.0 - clamp(nz, 0.0, 1.0);
    int   STEPS = int(mix(20.0, 72.0, graze));
    float layer = 1.0 / float(STEPS);

    // Use eye?surface ray: negate if V is frag?eye (typical)
    vec2  P   = (-vParallel / nz) * u_height_scale; // in-plane stretch
    vec2  dUV = P / float(STEPS);

    vec2  curUV = uv;
    float curD  = 0.0;

    float prevH = shape_height(texture(disp, curUV).r);
    float prevD = curD;

    // Coarse march until we cross the height
    for (int i = 0; i < STEPS; ++i) {
        curUV += dUV;
        curD  += layer;

        float h = shape_height(texture(disp, curUV).r);
        if (curD >= h) {
            // Binary refine the intersection
            vec2  loUV = curUV - dUV, hiUV = curUV;
            float loD  = prevD,       hiD  = curD;
            float loH  = prevH,       hiH  = h;

            for (int j = 0; j < BINSEARCH_ITERS; ++j) {
                vec2  midUV = 0.5*(loUV + hiUV);
                float midD  = 0.5*(loD  + hiD);
                float midH  = shape_height(texture(disp, midUV).r);
                if (midD >= midH) { hiUV = midUV; hiD = midD; hiH = midH; }
                else              { loUV = midUV; loD = midD; loH = midH; }
            }
            curUV = hiUV;
            curD  = hiD;
            break;
        }
        prevH = h;
        prevD = curD;
    }

    // --- View-dependent self-shadowing (horizon occlusion proxy) ---
    // March a few steps from the hit point back toward the eye direction
    // in the plane, and see if we "run into" higher heights.
    {
        float vparLen = length(vParallel);
        if (vparLen > 1e-6) {
            vec2  vpDir = (vParallel / vparLen) * sign(vNormal + 1e-6);
            int   stepsOcc = int(mix(float(SHADOW_STEPS_MIN), float(SHADOW_STEPS_MAX), graze));
            float occ = 0.0;
            vec2  occUV = curUV;
            float occD  = curD + SHADOW_BIAS;   // start just above the surface
            float stepLen = length(P) / float(STEPS);
            vec2  occStep = (-vpDir) * stepLen * 1.35; // slightly longer stride

            for (int k = 0; k < stepsOcc; ++k) {
                occUV -= occStep;
                occD  -= layer;                 // walk "up" the layers toward the viewer
                float h = shape_height(texture(disp, occUV).r);
                // If the sampled height rises above our plane depth, it occludes
                if (h > occD) occ += 1.0;
            }
            occ /= float(stepsOcc);
            // Convert hit ratio to a soft shadow factor
            selfOcc = mix(1.0, 1.0 - SHADOW_STRENGTH, occ);
        } else {
            selfOcc = 1.0;
        }
    }

    // Center mid-gray (0.5) around the mesh plane
    return (uv - curUV) - P * 0.5;
}

// -------- Tri-planar sampling (color) using POM offsets ----------
vec3 sample_triplanar(sampler2D tex, vec3 wp, vec3 wn,
                      vec2 off_x, vec2 off_y, vec2 off_z)
{
    vec3 w = pow(abs(wn), vec3(3.0));
    w /= (w.x + w.y + w.z + 1e-6);

    vec2 uvx = vec2(wp.z, wp.y) * u_texture_scale - off_x; // YZ for X faces
    vec2 uvy = vec2(wp.x, wp.z) * u_texture_scale - off_y; // XZ for Y faces
    vec2 uvz = vec2(wp.x, wp.y) * u_texture_scale - off_z; // XY for Z faces

    vec3 cx = texture(tex, uvx).rgb;
    vec3 cy = texture(tex, uvy).rgb;
    vec3 cz = texture(tex, uvz).rgb;

    return cx * w.x + cy * w.y + cz * w.z;
}

// -------- Tri-planar normal sampling (world space) ---------------
vec3 sample_triplanar_normal(sampler2D nmap, vec3 wp, vec3 wn,
                             vec2 off_x, vec2 off_y, vec2 off_z)
{
    vec3 w = pow(abs(wn), vec3(3.0));
    w /= (w.x + w.y + w.z + 1e-6);

    float sx = sign(wn.x), sy = sign(wn.y), sz = sign(wn.z);

    // X faces ? plane YZ
    vec2 uvx = vec2(wp.z, wp.y) * u_texture_scale - off_x;
    vec3 nx  = decode_normal(texture(nmap, uvx).rgb);
    vec3 Nx  = orient_plane_normal(nx, vec3(0,1,0), vec3(0,0,1), vec3(sx,0,0));

    // Y faces ? plane XZ
    vec2 uvy = vec2(wp.x, wp.z) * u_texture_scale - off_y;
    vec3 ny  = decode_normal(texture(nmap, uvy).rgb);
    vec3 Ny  = orient_plane_normal(ny, vec3(1,0,0), vec3(0,0,1), vec3(0,sy,0));

    // Z faces ? plane XY
    vec2 uvz = vec2(wp.x, wp.y) * u_texture_scale - off_z;
    vec3 nz  = decode_normal(texture(nmap, uvz).rgb);
    vec3 Nz  = orient_plane_normal(nz, vec3(1,0,0), vec3(0,1,0), vec3(0,0,sz));

    return normalize(Nx * w.x + Ny * w.y + Nz * w.z);
}

void main()
{
    vec3 N = normalize(fs_WorldNormal);
    vec3 V = normalize(fs_ViewDir_tangent); // frag?eye; we negate inside POM 

    float sx = sign(N.x), sy = sign(N.y), sz = sign(N.z);

    // X faces ? YZ plane
    vec2 uv_x = vec2(fs_WorldPos.z, fs_WorldPos.y) * u_texture_scale;
    vec2 vp_x = vec2(V.z, V.y);
    if (sx < 0.0) { uv_x.x = -uv_x.x; vp_x.x = -vp_x.x; }
    float occ_x; vec2 off_x = pom_plane_with_shadow(material.heightMap, uv_x, vp_x, V.x, occ_x);

    // Y faces ? XZ plane
    vec2 uv_y = vec2(fs_WorldPos.x, fs_WorldPos.z) * u_texture_scale;
    vec2 vp_y = vec2(V.x, V.z);
    if (sy < 0.0) { uv_y.x = -uv_y.x; vp_y.x = -vp_y.x; }
    float occ_y; vec2 off_y = pom_plane_with_shadow(material.heightMap, uv_y, vp_y, V.y, occ_y);

    // Z faces ? XY plane
    vec2 uv_z = vec2(fs_WorldPos.x, fs_WorldPos.y) * u_texture_scale;
    vec2 vp_z = vec2(V.x, V.y);
    if (sz < 0.0) { uv_z.x = -uv_z.x; vp_z.x = -vp_z.x; }
    float occ_z; vec2 off_z = pom_plane_with_shadow(material.heightMap, uv_z, vp_z, V.z, occ_z);

    // Weight the self-occlusion like other tri-planar textures
    vec3 w = pow(abs(N), vec3(3.0));
    w /= (w.x + w.y + w.z + 1e-6);
    float selfOcc = occ_x * w.x + occ_y * w.y + occ_z * w.z;

    // Sample all maps with the SAME offsets
    vec3  albedo    = sample_triplanar(material.albedoMap,    fs_WorldPos, N, off_x, off_y, off_z);
    float ao_tex    = sample_triplanar(material.aoMap,        fs_WorldPos, N, off_x, off_y, off_z).r;
    float metallic  = sample_triplanar(material.metallicMap,  fs_WorldPos, N, off_x, off_y, off_z).r;
    float roughness = sample_triplanar(material.roughnessMap, fs_WorldPos, N, off_x, off_y, off_z).r;
    vec3  emissive  = sample_triplanar(material.emissiveMap,  fs_WorldPos, N, off_x, off_y, off_z);

    // Parallaxed world-space normal
    vec3 Np = sample_triplanar_normal(material.normalMap, fs_WorldPos, N, off_x, off_y, off_z);

    // Combine AO with self-occlusion
    float ao_out = clamp(ao_tex * selfOcc, 0.0, 1.0);

    // G-buffer
    gPosition   = vec4(fs_WorldPos, 1.0);
    gNormal     = vec4(Np, 1.0);
    gAlbedoAO   = vec4(albedo, ao_out);
    gMetalRough = vec4(metallic, roughness, 0.0, 1.0);
    gEmissive   = vec4(emissive, 1.0);
}
