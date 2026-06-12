#version 430 core
// Anisotropy pass (Yu & Turk 2013, simplified): fit an ellipsoid to each
// particle's neighbourhood so surface splats flatten into the local sheet
// instead of rendering as spheres ("dragon scales" fix). One neighbour walk
// accumulates raw moments; covariance = E[yy^T] - E[y]E[y]^T in coordinates
// relative to the particle (no cancellation at world scale).
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer GridHead { int head[]; };
layout(std430, binding = 2) buffer GridNext { int nxt[]; };

// quat: ellipsoid orientation (world). radiiN.xyz: radii as MULTIPLES of the
// render radius (sizeScale applied at draw time). radiiN.w: 1 = valid.
// center.xyz: Laplacian-smoothed splat centre.
struct Aniso {
    vec4 quat;
    vec4 radiiN;
    vec4 center;
};
layout(std430, binding = 6) buffer AnisoBuf { Aniso aniso[]; };

uniform int u_particleCount;
uniform float u_h;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;

const float KAPPA = 4.0;        // max radii stretch ratio
const int   N_ISO = 25;         // below this, neighbourhood too sparse: stay a sphere
const float LAMBDA_CENTER = 0.6; // centre smoothing toward neighbourhood mean

ivec3 cellOf(vec3 pos)
{
    vec3 rel = (pos - u_domainMin) / u_h;
    return clamp(ivec3(rel), ivec3(0), ivec3(u_gridNx - 1, u_gridNy - 1, u_gridNz - 1));
}

// One cyclic Jacobi rotation on symmetric A (in-place), accumulating V.
void jacobiRotate(inout mat3 A, inout mat3 V, int pIdx, int qIdx)
{
    float apq = A[qIdx][pIdx];
    if (abs(apq) < 1e-12) return;
    float app = A[pIdx][pIdx];
    float aqq = A[qIdx][qIdx];
    float theta = 0.5 * (aqq - app) / apq;
    float t = sign(theta) / (abs(theta) + sqrt(theta * theta + 1.0));
    float c = inversesqrt(t * t + 1.0);
    float s = t * c;

    mat3 R = mat3(1.0);
    R[pIdx][pIdx] = c;  R[qIdx][qIdx] = c;
    R[qIdx][pIdx] = s;  R[pIdx][qIdx] = -s;
    A = transpose(R) * A * R;
    V = V * R;
}

vec4 quatFromMat3(mat3 m)
{
    float tr = m[0][0] + m[1][1] + m[2][2];
    vec4 q;
    if (tr > 0.0) {
        float s = sqrt(tr + 1.0) * 2.0;
        q = vec4((m[1][2] - m[2][1]) / s, (m[2][0] - m[0][2]) / s,
                 (m[0][1] - m[1][0]) / s, 0.25 * s);
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        q = vec4(0.25 * s, (m[1][0] + m[0][1]) / s,
                 (m[2][0] + m[0][2]) / s, (m[1][2] - m[2][1]) / s);
    } else if (m[1][1] > m[2][2]) {
        float s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        q = vec4((m[1][0] + m[0][1]) / s, 0.25 * s,
                 (m[2][1] + m[1][2]) / s, (m[2][0] - m[0][2]) / s);
    } else {
        float s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        q = vec4((m[2][0] + m[0][2]) / s, (m[2][1] + m[1][2]) / s,
                 0.25 * s, (m[0][1] - m[1][0]) / s);
    }
    return normalize(q);
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;

    vec3 pi = p[i].posLife.xyz;
    if (p[i].posLife.w <= 0.0) {
        aniso[i] = Aniso(vec4(0, 0, 0, 1), vec4(vec3(1.0), 0.0), vec4(pi, 0.0));
        return;
    }

    ivec3 cc = cellOf(pi);
    float wsum = 0.0;
    vec3 m1 = vec3(0.0);
    // Symmetric second moment, 6 unique terms.
    float xx = 0.0, xy = 0.0, xz = 0.0, yy = 0.0, yz = 0.0, zz = 0.0;
    int neighbors = 0;

    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        ivec3 c = cc + ivec3(dx, dy, dz);
        if (any(lessThan(c, ivec3(0))) ||
            any(greaterThanEqual(c, ivec3(u_gridNx, u_gridNy, u_gridNz)))) continue;
        int j = head[(c.z * u_gridNy + c.y) * u_gridNx + c.x];
        while (j >= 0) {
            vec3 y = p[j].posLife.xyz - pi;
            float r = length(y);
            if (r < u_h && p[j].posLife.w > 0.0) {
                float w = 1.0 - r / u_h; // Yu-Turk isotropic weight
                wsum += w;
                m1 += w * y;
                xx += w * y.x * y.x; xy += w * y.x * y.y; xz += w * y.x * y.z;
                yy += w * y.y * y.y; yz += w * y.y * y.z; zz += w * y.z * y.z;
                ++neighbors;
            }
            j = nxt[j];
        }
    }

    if (neighbors < N_ISO || wsum < 1e-6) {
        // Sparse neighbourhood (droplet/spray): isotropic sphere, raw centre.
        aniso[i] = Aniso(vec4(0, 0, 0, 1), vec4(vec3(1.0), 1.0), vec4(pi, 0.0));
        return;
    }

    vec3 mean = m1 / wsum;
    mat3 C;
    C[0] = vec3(xx, xy, xz) / wsum - mean.x * mean;
    C[1] = vec3(xy, yy, yz) / wsum - mean.y * mean;
    C[2] = vec3(xz, yz, zz) / wsum - mean.z * mean;

    // Cyclic Jacobi, 4 sweeps: plenty for 3x3 symmetric.
    mat3 V = mat3(1.0);
    for (int sweep = 0; sweep < 4; ++sweep) {
        jacobiRotate(C, V, 0, 1);
        jacobiRotate(C, V, 0, 2);
        jacobiRotate(C, V, 1, 2);
    }
    vec3 ev = vec3(C[0][0], C[1][1], C[2][2]);
    float evMax = max(ev.x, max(ev.y, ev.z));
    // Stretch clamp on RADII ratio kappa => eigenvalue ratio kappa^2.
    ev = max(ev, vec3(evMax / (KAPPA * KAPPA)));

    // Radii proportional to sqrt(eigenvalue), volume-normalised so the splat
    // keeps the sphere's volume (geo mean of radii = 1).
    vec3 rad = sqrt(max(ev, vec3(1e-12)));
    float geo = pow(rad.x * rad.y * rad.z, 1.0 / 3.0);
    rad = clamp(rad / max(geo, 1e-9), vec3(0.4), vec3(2.5));

    // Right-handed eigenbasis for a clean quaternion.
    if (determinant(V) < 0.0) V[2] = -V[2];

    aniso[i].quat = quatFromMat3(V);
    aniso[i].radiiN = vec4(rad, 1.0);
    aniso[i].center = vec4(pi + LAMBDA_CENTER * mean, 0.0);
}
