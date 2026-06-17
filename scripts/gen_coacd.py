#!/usr/bin/env python3
# gen_coacd.py -- OFFLINE generation of CoACD (Approximate Convex Decomposition) colliders for the YCB grasp
# library. CoACD (github.com/SarahWeiii/CoACD, MIT) is run via its prebuilt Python wheel (pip install coacd);
# only the OUTPUT convex parts (derivatives of the YCB meshes) are shipped -- no CoACD source is vendored, so the
# C++ build is untouched. Real-metric: the YCB meshes are already in meters; CoACD operates on them as-is.
#
# Output per object: assets/ycb/<id>/coacd.bin
#   bytes  "COAC"            magic
#   u32    numParts
#   per part: u32 numVerts ; float32[3]*numVerts   (the part's CONVEX-HULL vertices; PhysX cooks the hull)
# Deterministic: fixed seed so the colliders are reproducible.
import coacd, trimesh, numpy as np, struct, os, sys, time

BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "ycb")
THRESHOLD = 0.04          # concavity threshold; lower => finer, preserves narrow graspable cavities (handles, cups)
SEED = 0
MAX_HULL = 64             # cap parts so PhysX colliders stay tractable while still preserving concavities

CATALOG = [
    "003_cracker_box", "004_sugar_box", "005_tomato_soup_can", "006_mustard_bottle", "007_tuna_fish_can",
    "009_gelatin_box", "010_potted_meat_can", "011_banana", "019_pitcher_base", "021_bleach_cleanser",
    "024_bowl", "025_mug", "035_power_drill", "036_wood_block", "040_large_marker", "048_hammer",
    "056_tennis_ball", "061_foam_brick", "065-b_cups", "077_rubiks_cube",
]

coacd.set_log_level("error")

def gen(oid):
    mp = os.path.join(BASE, oid, "google_16k", "nontextured.ply")
    if not os.path.isfile(mp):
        print(f"  SKIP {oid}: mesh missing ({mp})"); return None
    m = trimesh.load(mp, process=False)
    cm = coacd.Mesh(np.asarray(m.vertices), np.asarray(m.faces))
    t0 = time.time()
    parts = coacd.run_coacd(cm, threshold=THRESHOLD, max_convex_hull=MAX_HULL, seed=SEED)
    dt = time.time() - t0
    out = os.path.join(BASE, oid, "coacd.bin")
    nverts_total = 0
    with open(out, "wb") as f:
        f.write(b"COAC")
        f.write(struct.pack("<I", len(parts)))
        for v, faces in parts:
            hull = trimesh.Trimesh(np.asarray(v), np.asarray(faces)).convex_hull
            hv = np.asarray(hull.vertices, dtype=np.float32)
            nverts_total += len(hv)
            f.write(struct.pack("<I", len(hv)))
            f.write(hv.tobytes())
    print(f"  {oid:22s} {len(parts):3d} parts  {nverts_total:5d} verts  {dt:5.1f}s -> {out}", flush=True)
    return len(parts)

if __name__ == "__main__":
    only = sys.argv[1:] if len(sys.argv) > 1 else CATALOG
    print(f"CoACD gen: threshold={THRESHOLD} max_hull={MAX_HULL} seed={SEED}  ({len(only)} objects)", flush=True)
    counts = []
    for oid in only:
        c = gen(oid)
        if c is not None: counts.append(c)
    print(f"DONE: {len(counts)} objects, parts min/mean/max = "
          f"{min(counts)}/{sum(counts)//max(1,len(counts))}/{max(counts)}", flush=True)
