# KRStudio Save Architecture — the `.k*` file family

Status: **v1 shipped** (`krs::ksave`, gated by `KRS_KSAVE_SELFTEST`). This doc pins the contracts
so future file types (`.kactuator`, `.kmotor`, `.klayout`) extend the same rules instead of
inventing new ones.

## The model: composition by reference

Definitions are reusable files; scenes instance them; sessions overlay them. Three layers with
different owners and lifetimes:

| Layer      | Files                              | Owner            | Git      |
|------------|------------------------------------|------------------|----------|
| Definition | `.krobot`, `.kjoint` (`.kactuator`, `.kmotor`, `.kgearbox` planned) | library / vendor | commit   |
| Instance   | `.kscene`                          | the project      | commit   |
| Session    | `.kstate` (`.klayout` planned)     | the user/machine | ignore   |

The discriminating question for any value: **"should Revert-to-authored reset it?"**
Yes → session state. No → definition or instance.

## Reference semantics (every cross-file link)

```json
{ "ref": "robots/FANUC-430.krobot", "id": "<uuid>", "contentHash": "<sha1 of file bytes>" }
```

- `ref`: RELATIVE path from the referencing file's directory. Human-readable, zippable, emailable.
- `id`: stable identity minted at creation (survives renames; future loaders may search by id).
- `contentHash`: the lockfile. At load, a mismatch means the definition changed on disk since the
  scene was saved → the loader **re-parses the new content and reports it**. Dropping an
  identically-named `.krobot` with different internals into the folder is a *supported workflow*,
  not an error — but it is never silent.

## File formats (all JSON, all versioned)

Every file starts with `"format": "<family>/<major>"`. Unknown major → refuse the file. Minor
evolution = added optional fields (loaders ignore unknowns).

### `.kscene` — instances
```json
{ "format": "kscene/1", "name": "cell",
  "robots": [ { "ref": "robots/FANUC-430.krobot", "id": "…", "contentHash": "…",
                "robotId": 0, "name": "FANUC-430", "ownsDrive": true,
                "basePlacement": [16 doubles, row-major] } ] }
```

### `.krobot` — one robot's master dict (`robots/<name>.krobot`)
```json
{ "format": "krobot/1", "id": "…", "name": "FANUC-430",
  "builder": 1, "sourceStep": "D:/…/fanuc430.step", "bodyCount": 7, "base": 0,
  "bodies": [ { "name": "430-base", "visSize": [x,y,z] } ],
  "joints": [ { "ref": "robots/FANUC-430/J0.kjoint", "id": "…", "contentHash": "…" } ],
  "connectorSets": [ { "body": 2, "slot": 0, "nextId": 3,
                       "connectors": [ { "id": 1, "name": "…", "pos": […], "z": […], "x": […],
                                         "key": "<faceKey u64 as string>", "ftype": 1, "radius": 0.05 } ] } ],
  "nextJointId": "7", "nextNodeId": 6 }
```
**Geometry is never inlined.** `builder` + `sourceStep` name the deterministic rebuild:
`0` = demo recipe (`buildDemoGraph`), `1` = named serial chain (`buildNamedSerialChain`),
`2` = parts spanning tree (`buildGraphFromParts`). Load re-imports the STEP, re-runs the builder,
checks `bodyCount`, then **overlays** the `.kjoint` files (whole overlay or refuse that robot).
Provenance is carried at runtime by the ctx `krs::ksave::RobotSourceRegistry` (graph mirrors
can't know their source), populated at boot / Import CAD / Load Demo / scene load.

### `.kjoint` — one joint (`robots/<name>/<joint>.kjoint`)
```json
{ "format": "kjoint/1", "id": "3", "nodeId": 2, "name": "elbow",
  "type": 0, "parent": 2, "child": 3, "prov": 1, "ambiguous": false, "residual": 0.0,
  "axisPos": […], "axisDir": […], "refDir": […],
  "limits": { "lower": -1.25, "upper": 0.75, "effort": 0, "velocity": 0, "enabled": true } }
```
Types: 0 revolute, 1 prismatic, 2 fixed. `limits.enabled=false` = continuous.
Planned: a `"actuator": { "ref": "…/EC45_GP42.kactuator", … }` reference; effort/velocity then
become *derivable* from the drive-train (motor Kt·I·gear·η) with `ManufacturerDerived` provenance.

### `.kstate` — session sidecar (`<scene>.kstate`, gitignore it)
```json
{ "format": "kstate/1",
  "domains": { "robots": { "0": { "q": [0.15, 0.30] } },
               "camera": { "pos": […], "target": […] } } }
```
**Contract:** sim-agnostic deterministic state only — per-robot pose `q`, camera. Explicitly NO
PhysX/fluid/MPM buffers (a future explicit `.kcheckpoint` would own those, with weaker promises).
Domains are independent and optional; loaders skip unknown domains. Best-effort by design: a
stale entry (robot gone, DOF count changed) is **skipped with a report**; a missing/corrupt
sidecar never blocks a scene load. Rewritten on every Save Scene and at app exit.

## Failure policy (uniform across the family)

| Situation                          | Behavior |
|------------------------------------|----------|
| unknown format major               | refuse the file |
| `.krobot` missing/corrupt          | skip that instance, warn, keep loading others |
| rebuilt bodyCount ≠ definition     | refuse that robot (source changed), spawned geometry cleaned up |
| any `.kjoint` missing/malformed    | refuse that robot (whole overlay or nothing) |
| contentHash mismatch               | load the NEW content + warn ("changed on disk") |
| `.kstate` entry no longer binds    | skip entry + warn; scene loads at authored state |

Nothing silently rebinds; nothing partially applies a definition.

## App integration

- **Save Scene / Load Scene…** on the Engineering toolbar; both show a validation report
  (robots/joints written or loaded, every warning). Save remembers the path (ctx
  `OpenSceneInfo`); Load REPLACES the scene's robots wholesale (registry + member entities
  cleared; first robot's graph becomes the active authoring graph, the rest park).
- **Boot reopen:** `QSettings("ksave/lastScene")` points at the current document; on a normal
  boot the app reopens it (replacing the default FANUC boot) — the program comes back where it
  closed. Suppressed whenever any `KRS_*` env var is set, so every gate/bench measures the
  pristine boot; the KSAVE gate additionally captures+restores the setting.
- **Exit hook:** rewrites only the `.kstate` sidecar (cheap; authored files are never silently
  rewritten on quit).
- Robots without a rebuildable source (e.g. a split-off branch) are skipped at save with a
  warning — re-merge before saving, or wait for durable BodyId (v2) which lifts this.

## Parts ecosystem (`krs::parts`, shipped — `KRS_KPARTS_SELFTEST`)

Reusable component definitions, same reference/hash rules as the scene family:

- `.kmotor` — vendor datasheet motor (`torqueConstant`, `maxCurrent`, `maxSpeed`, …).
- `.kactuator` — motor + gearbox (`gearRatio`, `efficiency`, `motor:{ref,contentHash}`).
  `krs::ksave::resolveActuator` derives joint effort = Kt·Imax·ratio·η, velocity = maxSpeed/ratio.
- `.kgearbox` / `.kencoder` / `.kcamera` / `.kimu` / `.kmaterial` — indexed by type; schemas grow
  as needed (unknown fields ignored).

**Library** (`PartLibrary`): a SEARCH PATH — `<scene>/parts` → `<project>/parts` (KRS_SOURCE_DIR)
→ `<user>/parts` — most-specific wins; new ids union in. The Manufacturer Parts panel lists it;
a row drags as `application/x-krstudio-asset` (the mime the viewport/joint drops accept). A
`.kactuator` dropped on a selected joint derives its limits + records `RBJoint.actuatorRef`.

**Repository** (`PartRepository`): content-addressed store keyed by (id, hash) so parts de-dupe and
a changed part is a NEW version. `LocalRepository` (shipped) is the offline cache and the on-disk
wire format a remote mirrors. `RemoteRepository` is an honest offline stub; its HTTP contract
(`GET /index`, `GET /part/<id>/<hash>`, `POST /part`, Bearer auth) is documented at the definition
so a live QtNetwork backend swaps in behind the same signatures with zero caller changes.

Starter library ships under `<repo>/parts` (Maxon EC-45/DCX, an EC45+GP42 actuator, RealSense
D435, Bosch BMI088, an anodized-aluminum material).

## Materials (`krs::facemat`, shipped — `KRS_FACEMAT_SELFTEST`)

`FaceMaterialComponent` maps B-Rep faceId → {albedo, metallic, roughness}. Whole-body writes
`MaterialComponent`; per-face generates overlay sub-meshes (that face's triangles, offset 0.5 mm
proud, a child entity with the override material) — reuses the existing render path, no shader
change. The Material Editor panel drives both via a Whole-body / This-face toggle + a Pick-Face
mode over the feature-selection service.

## `.klayout` (shipped)

ADS `CDockManager::saveState()` → `QSettings("layout/dockState")` at exit, `restoreState()` at
boot. A per-user preference, separate from `.kscene`; suppressed under any `KRS_*` env.

## Delivered since v1

- **Non-robot scene content (v1.1)** — `saveScene`/`loadScene` now persist, alongside robots: the
  environment/skybox knobs (IBL intensity, sun dir/color/intensity, exposure/tonemap, HDR toggle) via
  an `EnvironmentSettings` ctx singleton the app syncs to/from the live `RenderingSystem`; fog +
  background via `SceneProperties`; every non-robot `LightComponent` (type/color/intensity/cone/
  size/range/enabled) with its transform + emissive material; and loose objects that carry a
  `SceneObjectComponent` recipe (primitive index or mesh path) with their transform (incl. rotation)
  and full `MaterialComponent` (appearance + engineering fields). An empty robot registry is no
  longer fatal; REPLACE-on-load also clears loose objects + lights. Gate `KRS_SCENESAVE_SELFTEST`
  (`runSceneObjectsGate`). *Still open:* mesh-asset object reload (skipped with a warning today, a
  v1.2 follow-up), fluids/MPM buffers, and a fully generic registry-driven component serializer
  (this pass whitelists the components above rather than walking every component type).

## Remaining v2 backlog

- Instance-level **overrides** (scene-local sparse deviations from a SHARED library `.krobot`,
  applied on load, orphan-reported when they no longer bind) + a robot library search path. This
  is the step that turns scene-owned robot files into a shared-definition ecosystem.
- Split-off robots aren't savable until bodies carry durable ids independent of a rebuild source
  (**durable BodyId**).
- **Live cloud backend** behind `RemoteRepository` (QtNetwork); the seam + contract are in place.
- Per-face material **visual verification**: the overlay-mesh render is gated on data/geometry but
  needs an eyes-on pass in the running app (z-offset tuning, robot-link tracking under motion).
