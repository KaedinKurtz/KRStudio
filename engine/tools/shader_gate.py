#!/usr/bin/env python3
"""Shader gate (CI stage S1). See docs/graphics/CI_PIPELINE.md.

- engine/shaders/manifest.json is the source of truth: every .vert/.frag/.comp/.tesc/.tese
  file on disk must have a manifest row and vice versa; files in "retired" must NOT exist
  (re-entry of a retired shader is a build error, per the census's rotting-orphan finding).
- Every shader compiles with glslangValidator --target-env vulkan1.2 and passes spirv-val.
  Any glslang warning is an error.
- Banned constructs (IMPLEMENTATION_PLAN.md 'shader changeover' rules): reconstructing depth
  from projection-matrix elements.

Exit 0 = clean; 1 = violations.
"""

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # engine/
SHADER_DIR = ROOT / "shaders"
MANIFEST = SHADER_DIR / "manifest.json"

STAGE_FLAGS = {
    "vertex": "vert",
    "fragment": "frag",
    "compute": "comp",
    "tess_control": "tesc",
    "tess_eval": "tese",
}
EXTS = {".vert", ".frag", ".comp", ".tesc", ".tese"}

BANNED = [
    (re.compile(r"u_?[Pp]rojection\s*\[\s*[23]\s*\]\s*\[\s*[23]\s*\]"),
     "depth reconstruction from projection-matrix elements is banned; use explicit zA/zB uniforms"),
]

errors = []


def err(msg):
    errors.append(msg)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def main():
    glslang = shutil.which("glslangValidator") or shutil.which("glslang")
    spirv_val = shutil.which("spirv-val")
    if not glslang or not spirv_val:
        print("ERROR: glslangValidator/spirv-val not on PATH — the shader gate cannot run "
              "(a gate that cannot run is a failure, not a skip)")
        return 1

    manifest = json.loads(MANIFEST.read_text())
    rows = {r["file"]: r for r in manifest["shaders"]}
    retired = set(manifest.get("retired", []))

    on_disk = {p.name for p in SHADER_DIR.iterdir() if p.suffix in EXTS}
    for f in sorted(on_disk - set(rows)):
        err(f"{f}: on disk but not in manifest.json")
    for f in sorted(set(rows) - on_disk):
        err(f"{f}: in manifest.json but missing on disk")
    for f in sorted(retired & on_disk):
        err(f"{f}: RETIRED shader re-entered the tree (see manifest 'retired'; this is a build error)")
    for f in sorted(retired & set(rows)):
        err(f"{f}: listed both as active and retired in manifest.json")

    with tempfile.TemporaryDirectory() as td:
        for name in sorted(on_disk & set(rows)):
            row = rows[name]
            stage = row.get("stage")
            if STAGE_FLAGS.get(stage) != Path(name).suffix.lstrip("."):
                err(f"{name}: manifest stage '{stage}' does not match file extension")
                continue
            src = SHADER_DIR / name
            text = src.read_text(encoding="utf-8", errors="replace")
            for pattern, why in BANNED:
                for i, line in enumerate(text.splitlines(), 1):
                    if pattern.search(line.split("//")[0]):
                        err(f"{name}:{i}: {why}")
            spv = Path(td) / (name + ".spv")
            r = run([glslang, "--target-env", "vulkan1.2",
                     "-S", STAGE_FLAGS[stage], "-o", str(spv), str(src)])
            out = (r.stdout + r.stderr).strip()
            if r.returncode != 0 or "WARNING" in out.upper():
                err(f"{name}: glslang failed or warned:\n{out}")
                continue
            v = run([spirv_val, str(spv)])
            if v.returncode != 0:
                err(f"{name}: spirv-val failed:\n{(v.stdout + v.stderr).strip()}")

    if errors:
        print(f"SHADER GATE FAILURES ({len(errors)}):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"shader gate clean: {len(on_disk)} shaders validated, {len(retired)} retirements enforced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
