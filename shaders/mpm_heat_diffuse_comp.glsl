#version 430 core
// MLS-MPM thermal stage 3: energy-conserving Fourier conduction on the grid,
// div(k grad T). One explicit (Jacobi) sweep per frame using the HARMONIC MEAN
// of adjacent-node conductivities for each face (the correct series-resistance
// flux between cells of different k -- two solid bodies touching on the grid):
//   flux_face = kface*(T_nbr - T_n),   kface = 2 k_n k_nbr / (k_n + k_nbr)
//   dT_n      = (S*dt*dx / C_n) * sum_faces flux_face
// C_n is the node thermal mass (J/K). The per-face conductance is clamped
// SYMMETRICALLY (identical on both sides of a face), so the pairwise flux stays
// antisymmetric and energy is conserved exactly even where the clamp is active at
// heterogeneous-k interfaces; the clamp also makes the update unconditionally
// stable. S is a speed multiplier: real metals conduct on minute timescales at
// this grid resolution, so S accelerates conduction to interactive rates.
layout(local_size_x = 64) in;

layout(std430, binding = 4) buffer TempA { float ta[]; };   // in  temperature
layout(std430, binding = 5) buffer TempB { float tb[]; };   // out temperature
layout(std430, binding = 7) buffer GridC { float gc[]; };   // node thermal mass C (J/K)
layout(std430, binding = 8) buffer GridK { float gk[]; };   // node conductivity k (W/m.K)

uniform int   u_N;
uniform float u_coef;     // S * dt * dx
uniform float u_betaMax;  // explicit-stability cap on the per-cell diagonal weight
const float SENTINEL = -1.0e9;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    int cells = u_N * u_N * u_N;
    if (idx >= uint(cells)) return;
    float T = ta[idx];
    if (T < -1.0e8) { tb[idx] = SENTINEL; return; }      // empty cell stays empty

    int cz = int(idx) / (u_N * u_N);
    int rem = int(idx) - cz * u_N * u_N;
    int cy = rem / u_N;
    int cx = rem - cy * u_N;

    float kn = gk[idx];
    float Cn = gc[idx];
    // Net energy into this cell (J), summed over faces. The per-face conductance
    // a_f is SYMMETRIC (both cells compute the same value from kface, C_n, C_nbr),
    // so the pairwise flux a_f*(T_nbr - T_n) is exactly antisymmetric -> total
    // energy is conserved even at heterogeneous-k interfaces and even when clamped.
    // a_f is capped at betaMax*min(C_n,C_nbr)/6 so each cell's sum a_f/C <= betaMax:
    // the update T + dE/C_n is then a convex combination of neighbour temperatures
    // (non-negative weights summing to 1) -> unconditionally stable.
    float dE = 0.0;
    ivec3 offs[6] = ivec3[6](ivec3(1,0,0), ivec3(-1,0,0), ivec3(0,1,0),
                             ivec3(0,-1,0), ivec3(0,0,1), ivec3(0,0,-1));
    for (int f = 0; f < 6; ++f) {
        ivec3 nc = ivec3(cx, cy, cz) + offs[f];
        if (any(lessThan(nc, ivec3(0))) || any(greaterThanEqual(nc, ivec3(u_N)))) continue;
        int nidx = (nc.z * u_N + nc.y) * u_N + nc.x;
        float Tn = ta[nidx];
        if (Tn < -1.0e8) continue;                        // material boundary = insulated
        float knb = gk[nidx];
        float Cnb = gc[nidx];
        float kf = (kn + knb > 1.0e-12) ? (2.0 * kn * knb) / (kn + knb) : 0.0; // harmonic mean
        float aF = u_coef * kf;                           // nominal face conductance (J/K)
        float aMax = u_betaMax * min(Cn, Cnb) * (1.0 / 6.0); // symmetric stability cap
        aF = min(aF, aMax);
        dE += aF * (Tn - T);                              // antisymmetric energy flux
    }
    tb[idx] = T + ((Cn > 1.0e-9) ? dE / Cn : 0.0);
}
