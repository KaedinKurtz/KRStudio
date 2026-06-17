#!/usr/bin/env python3
# gen_coacd_gso.py -- generate CoACD colliders (MIT, prebuilt wheel) for the GSO objects that PASSED the C++
# GATE FILTER (assets/gso/_valid.txt). Same offline approach + binary format as gen_coacd.py; only the survivors
# get colliders. Parallel across processes (CoACD is CPU-bound). Coarser than the YCB pass (threshold 0.06,
# max_hull 32) to keep hundreds of objects tractable; the generalized rate is the goal, not perfect colliders.
import os, sys, struct, time
import numpy as np
import coacd, trimesh                       # module-level: shared by all worker THREADS (CoACD frees the GIL in C++)
from concurrent.futures import ThreadPoolExecutor, as_completed

coacd.set_log_level("error")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GSO  = os.path.join(REPO, "assets", "gso")
THRESHOLD, MAX_HULL, SEED = 0.06, 32, 0

def gen(name):
    obj = os.path.join(GSO, name, "model.obj")
    out = os.path.join(GSO, name, "coacd.bin")
    if os.path.isfile(out) and os.path.getsize(out) > 8:
        return (name, "cached", 0)
    try:
        m = trimesh.load(obj, process=False, force="mesh")
        cm = coacd.Mesh(np.asarray(m.vertices), np.asarray(m.faces))
        t0 = time.time()
        parts = coacd.run_coacd(cm, threshold=THRESHOLD, max_convex_hull=MAX_HULL, seed=SEED)
        with open(out, "wb") as f:
            f.write(b"COAC"); f.write(struct.pack("<I", len(parts)))
            for v, faces in parts:
                hull = trimesh.Trimesh(np.asarray(v), np.asarray(faces)).convex_hull
                hv = np.asarray(hull.vertices, dtype=np.float32)
                f.write(struct.pack("<I", len(hv))); f.write(hv.tobytes())
        return (name, f"ok({len(parts)})", time.time() - t0)
    except Exception as e:
        return (name, f"ERR {type(e).__name__}", 0)

if __name__ == "__main__":
    valid_path = os.path.join(GSO, "_valid.txt")
    if not os.path.isfile(valid_path):
        print("no _valid.txt -- run the FILTER gate first"); sys.exit(1)
    names = [l.strip() for l in open(valid_path) if l.strip()]
    workers = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    print(f"CoACD-GSO: {len(names)} valid objects, threshold={THRESHOLD} max_hull={MAX_HULL}, {workers} threads", flush=True)
    ok = cached = err = 0
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = {ex.submit(gen, nm): nm for nm in names}
        for i, fu in enumerate(as_completed(futs)):
            nm, st, dt = fu.result()
            if st.startswith("ok"): ok += 1
            elif st == "cached": cached += 1
            else: err += 1
            if (i + 1) % 20 == 0 or st.startswith("ERR"):
                print(f"  [{i+1}/{len(names)}] ok={ok} cached={cached} err={err}  last={nm[:30]}:{st}", flush=True)
    print(f"DONE: ok={ok} cached={cached} err={err} (errors fall back to V-HACD at grasp time)", flush=True)
