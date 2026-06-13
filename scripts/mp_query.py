#!/usr/bin/env python3
"""Materials Project ground-truth query for KRStudio (Phase 4).

Resolves a Materials Project id (e.g. mp-13) to SI density / bulk / shear
modulus and prints JSON on stdout. The engine embeds an equivalent query and
calls `python` directly; this standalone copy is for manual use / testing:

    set MP_API_KEY=<your key>          (Windows)   /  export MP_API_KEY=...  (POSIX)
    pip install mp-api
    python scripts/mp_query.py mp-13

Output: {"name","rho"(kg/m^3),"K"(Pa),"G"(Pa)} or {"error": "..."}.
Units: Materials Project density is g/cm^3 (x1000 -> kg/m^3); elastic moduli are
GPa (x1e9 -> Pa).
"""
import sys
import os
import json


def main() -> int:
    if len(sys.argv) < 2:
        print(json.dumps({"error": "usage: mp_query.py <mp-id>"}))
        return 0
    mid = sys.argv[1]
    key = os.environ.get("MP_API_KEY", "")
    if not key:
        print(json.dumps({"error": "MP_API_KEY not set"}))
        return 0
    try:
        from mp_api.client import MPRester  # requires `pip install mp-api`
        with MPRester(key) as m:
            docs = m.materials.summary.search(
                material_ids=[mid],
                fields=["material_id", "formula_pretty", "density",
                        "bulk_modulus", "shear_modulus"],
            )
            if not docs:
                print(json.dumps({"error": "not found: " + mid}))
                return 0
            d = docs[0]
            print(json.dumps({
                "name": str(getattr(d, "formula_pretty", mid)),
                "rho": float(getattr(d, "density", 0) or 0) * 1000.0,   # g/cm^3 -> kg/m^3
                "K": float(getattr(d, "bulk_modulus", 0) or 0) * 1e9,   # GPa -> Pa
                "G": float(getattr(d, "shear_modulus", 0) or 0) * 1e9,  # GPa -> Pa
            }))
    except Exception as exc:  # noqa: BLE001 - report any failure to the caller as JSON
        print(json.dumps({"error": str(exc)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
