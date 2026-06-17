#!/usr/bin/env python3
# download_gso.py -- fetch a large diverse subset of Google Scanned Objects (GSO) for grasp generalization.
# GSO: 1033 real scanned household objects, Creative Commons Attribution 4.0 International (CC-BY 4.0,
# permissive -- attribution only), hosted on Gazebo Fuel in REAL METRIC scale (meters). We extract only the
# geometry (meshes/model.obj) per object to assets/gso/<name>/model.obj (gitignored, like the YCB meshes).
# The objects are NOT pre-filtered here -- the C++ GATE FILTER decides validity/graspability; the rejected
# fraction is reported separately as a dataset-quality statistic.
import urllib.request, json, io, zipfile, os, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(REPO, "assets", "gso")
N    = int(sys.argv[1]) if len(sys.argv) > 1 else 500
FUEL = "https://fuel.gazebosim.org/1.0/GoogleResearch/models"

def list_models(n):
    names, page = [], 1
    while len(names) < n:
        url = f"{FUEL}?per_page=100&page={page}"
        d = json.loads(urllib.request.urlopen(url, timeout=60).read())
        if not d: break
        names += [m["name"] for m in d]
        page += 1
    return names[:n]

def fetch(name):
    out_dir = os.path.join(BASE, name)
    obj_path = os.path.join(out_dir, "model.obj")
    if os.path.isfile(obj_path) and os.path.getsize(obj_path) > 0:
        return (name, "cached")
    try:
        url = f"{FUEL}/{name}/1/{name}.zip"
        data = urllib.request.urlopen(url, timeout=180).read()
        z = zipfile.ZipFile(io.BytesIO(data))
        os.makedirs(out_dir, exist_ok=True)
        with open(obj_path, "wb") as f:
            f.write(z.read("meshes/model.obj"))     # geometry only; textures/mtl not needed for grasping
        return (name, "ok")
    except Exception as e:
        return (name, f"ERR {type(e).__name__}: {e}")

if __name__ == "__main__":
    os.makedirs(BASE, exist_ok=True)
    print(f"GSO download: listing {N} models from Fuel (CC-BY 4.0) ...", flush=True)
    names = list_models(N)
    print(f"got {len(names)} model names; downloading geometry (12 threads) ...", flush=True)
    ok = cached = err = 0
    with ThreadPoolExecutor(max_workers=12) as ex:
        futs = {ex.submit(fetch, nm): nm for nm in names}
        for i, fu in enumerate(as_completed(futs)):
            nm, status = fu.result()
            if status == "ok": ok += 1
            elif status == "cached": cached += 1
            else: err += 1
            if (i + 1) % 25 == 0 or status.startswith("ERR"):
                print(f"  [{i+1}/{len(names)}] ok={ok} cached={cached} err={err}  last={nm}:{status[:40]}", flush=True)
    print(f"DONE: {ok} downloaded, {cached} cached, {err} errors -> {BASE}", flush=True)
