#!/usr/bin/env bash
# Download a curated, diverse subset of the YCB object set (google_16k textured meshes) for grasp planning.
# Source: YCB benchmark S3 (Calli et al., "Benchmarking in Manipulation Research", IEEE RA-M 2015 / IJRR 2017).
# Yale-CMU-Berkeley object set; freely available for research use. See assets/ycb/LICENSE.md.
set -u
DEST="assets/ycb"
BASE="https://ycb-benchmarks.s3.amazonaws.com/data/google"
mkdir -p "$DEST"

# Curated 20: boxes / cylinders / bottles / cans / concavities (bowl,mug,cup,pitcher) / tools / spheres / thin.
OBJECTS=(
  003_cracker_box 004_sugar_box 005_tomato_soup_can 006_mustard_bottle 007_tuna_fish_can
  009_gelatin_box 010_potted_meat_can 011_banana 019_pitcher_base 021_bleach_cleanser
  024_bowl 025_mug 035_power_drill 036_wood_block 040_large_marker
  048_hammer 056_tennis_ball 061_foam_brick 065-b_cups 077_rubiks_cube
)

ok=0; fail=0
for obj in "${OBJECTS[@]}"; do
  if [ -d "$DEST/$obj/google_16k" ]; then echo "[skip] $obj (present)"; ok=$((ok+1)); continue; fi
  url="$BASE/${obj}_google_16k.tgz"
  echo "[get ] $obj"
  if curl -sS -L --max-time 120 "$url" -o "$DEST/${obj}.tgz"; then
    if tar -xzf "$DEST/${obj}.tgz" -C "$DEST" 2>/dev/null; then
      rm -f "$DEST/${obj}.tgz"
      # keep only geometry we need (obj/ply/stl + kinbody); drop big textures to save space
      rm -f "$DEST/$obj/google_16k/texture_map.png" 2>/dev/null
      ok=$((ok+1)); echo "       ok"
    else echo "       EXTRACT FAILED"; fail=$((fail+1)); rm -f "$DEST/${obj}.tgz"; fi
  else echo "       DOWNLOAD FAILED"; fail=$((fail+1)); fi
done
echo "==== downloaded $ok ok, $fail failed of ${#OBJECTS[@]} ===="
