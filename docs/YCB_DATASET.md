# YCB Object Library — source, license, attribution

The grasp-planning pipeline (ROADMAP §GR) is measured against a curated subset of the **YCB Object and Model
Set** (the Yale-CMU-Berkeley object set).

## Source
- Meshes downloaded from the official YCB benchmark S3 bucket:
  `https://ycb-benchmarks.s3.amazonaws.com/data/google/<OBJECT>_google_16k.tgz`
- Acquisition is scripted in `scripts/download_ycb.sh` (curl + tar). The meshes (~117 MB) are **not committed**
  to the repository (`.gitignore`: `assets/ycb/`); run the script to populate `assets/ycb/`.
- Geometry used for physics: `nontextured.ply` (clean watertight scan, no UV/material overhead).

## License / terms
The YCB Object and Model Set is provided by Yale University / Carnegie Mellon / UC Berkeley and is **freely
available for research use**. It is the de-facto standard benchmark for manipulation/grasping research. The S3
bucket is public (no authentication, no EULA gate). Use here is research/engineering on a grasp planner.

Cite if used in published work:
- B. Calli, A. Singh, A. Walsman, S. Srinivasa, P. Abbeel, A. M. Dollar, *"The YCB Object and Model Set:
  Towards Common Benchmarks for Manipulation Research"*, IEEE ICAR 2015.
- B. Calli et al., *"Yale-CMU-Berkeley dataset for robotic manipulation research"*, IJRR 2017.

## Curated subset (20 objects)
Spanning the grasp-relevant geometry classes (`YcbCatalog.cpp`):

| class | objects |
|---|---|
| box | 003_cracker_box, 004_sugar_box, 009_gelatin_box, 036_wood_block, 061_foam_brick, 077_rubiks_cube |
| can | 005_tomato_soup_can, 007_tuna_fish_can, 010_potted_meat_can |
| bottle | 006_mustard_bottle, 021_bleach_cleanser |
| concave (cavity) | 019_pitcher_base, 024_bowl, 025_mug, 065-b_cups |
| tool | 035_power_drill, 048_hammer |
| thin | 011_banana, 040_large_marker |
| sphere | 056_tennis_ball |

Scale anchors (verified against published YCB dimensions, used by GATE IMPORT): tennis ball ≈ 0.067 m diameter,
tomato soup can ≈ 0.102 m tall, bowl ≈ 0.159 m rim diameter. All objects are real-meter scale; the universal
manipulation band is the AABB longest axis ∈ [0.02, 0.40] m.
