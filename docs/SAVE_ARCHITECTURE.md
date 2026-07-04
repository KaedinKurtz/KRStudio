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

## Known v1 limits (the v2 backlog)

- Non-robot entities (loose primitives, lights, fluids) are not yet in `.kscene` — needs the
  registry-driven component serializer.
- No instance-level **overrides** yet (scene-local deviations from a shared `.krobot`); today the
  scene folder owns its robot files. Overrides + a library search path (scene → project → user)
  are the ecosystem step, together with `.kactuator`/`.kmotor` vendor files.
- Split-off robots aren't savable until bodies carry durable ids independent of a rebuild source.
- `.klayout` (ADS dock state) is separate from `.kscene` by design; not yet written.
