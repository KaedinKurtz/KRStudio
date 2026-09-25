# KRS Graphics — CI/CD Verification Pipeline

The contract of this pipeline: **no engine change merges unverified, and every work package's tests
exist and run in CI *before* its implementation lands** (test-first is a hard rule of the port, per
the owner). The pipeline is implemented in `.github/workflows/graphics-ci.yml` and is deliberately
independent of the app's heavy vcpkg CI (`docs/CI.md`): the engine vendors its tiny dependency set,
so this pipeline runs in **minutes on every push**, which is what makes gate-driven agent iteration
possible.

Honest boundary (same convention as `docs/CI.md`): stages S0–S4 prove compile/link/logic and
software-GPU execution. lavapipe (Mesa software Vulkan) and MoltenVK-on-CI validate *correctness*,
not interactive performance; hardware acceptance remains OPERATOR VERIFICATION at the milestones
marked in `IMPLEMENTATION_PLAN.md`.

---

## 1. Stages

All stages are required checks for any change touching `engine/**`, `scripts/check_engine_boundary.py`,
or the workflow itself. A stage that cannot run is a **failure**, never a silent skip (the one
sanctioned skip is exit-77 perf SKIP in S7, recorded as SKIP in the job summary).

### S0 — Boundary & hygiene (seconds)
- `scripts/check_engine_boundary.py` fails the build on:
  - any Qt/OpenGL/GLM/app include inside `engine/` (except `engine/adapters/qt/**`, wh­ere only Qt
    is additionally allowed);
  - any Vulkan include or `Vk*`/`vk*` symbol in `engine/include/krsg/**` (public API stays
    backend-clean) or outside `engine/src/rhi/vulkan/**`;
  - any app source including engine internals (`engine/src/**`) rather than `krsg/…` public
    headers;
  - upward/sideways layer includes (L0←L1←L2←adapters only);
  - new raw `gl->`/`GLuint` surface in app public headers (carries forward the raw-handle lint from
    the port plan, so the GL-side seam only shrinks).
- Each public header compiles standalone (IWYU/self-containedness check).
- `clang-format --dry-run -Werror` over `engine/` (engine code is format-clean from birth; the app
  tree is exempt until its own adoption decision).

### S1 — Shader gate (seconds)
- Every file in `engine/shaders/` compiles with `glslangValidator --target-env vulkan1.2` and
  passes `spirv-val`. Any warning is an error.
- `engine/shaders/manifest.json` is the single source of truth: a shader file absent from the
  manifest, or a manifest entry with no file, or a **retired** file reappearing = build error
  (the census showed orphaned shaders silently rotting in the GL tree — that class dies here).
- Entry points, stages, and declared push-constant/set layouts in the manifest are cross-checked
  against SPIR-V reflection (spirv-reflect step added in WP4 when the reflection tool lands).
- MoltenVK preflight: `spirv-cross --msl` smoke over the same set (added in WP2 with the
  capability table) so Metal-incompatible constructs fail in seconds on Linux, not hours later on
  the mac runner.

### S2 — Build & package matrix (minutes)
Platforms: ubuntu-22.04 (GCC 11 **and** Clang 14 — the app's CI floor), windows-latest (MSVC),
macos-14 (AppleClang, arm64). For each:
1. Configure + build `engine/` **standalone** (`-DKRSG_WERROR=ON`), Vulkan backend ON everywhere
   (headers vendored; no SDK needed to compile), Debug on Linux-GCC + Release everywhere else.
2. `cmake --install` into a scratch prefix.
3. Build `engine/tests/consume/` against **only** that installed prefix (a separate CMake project
   using `find_package(KRSGraphics CONFIG REQUIRED)`). This is the packaging/boundary contract
   test: if a public header secretly needs an internal or vendored header, this—not a user—breaks.

### S3 — Unit tests (minutes)
`ctest` on every S2 platform: API-contract tests against the **null backend** (handle lifetime,
double-destroy detection, descriptor validation, render-graph ordering/barrier derivation as pure
logic, result/error paths). The null backend is a real backend with checkable semantics, not a pile
of no-ops — graph tests assert the exact barrier/layout sequence it records.

### S4 — GPU execution, Linux/lavapipe (minutes)
- `mesa-vulkan-drivers` + `vulkan-tools` + validation layers installed; **ICD preflight** asserts
  lavapipe enumerates with the features the engine's capability table requires — a missing feature
  fails loudly with the table row named.
- The `engine/tests/gpu/` suite runs headless with `VK_LAYER_KHRONOS_validation` +
  synchronization validation active, under a wrapper that turns **any validation message into a
  test failure** (message-ID allowlist file for documented false positives; adding to it requires
  an ADR reference in the same commit).
- Test classes: clear/draw/readback smoke; compute checksum tests (deterministic SSBO in/out
  hashes — the fixed-point sim kernels' home turf); render-graph execution tests (transient
  aliasing, layout transitions); **golden-image tests** (PNG goldens in
  `engine/tests/goldens/`, per-channel tolerance + max-diff-pixel-count budget per test; on
  failure the actual/diff images upload as artifacts).
- Golden update ritual: goldens change only in a commit whose message starts `goldens:` and whose
  body names the test and the visual cause; CI rejects golden diffs in mixed commits. (Prevents
  "regenerate until green".)

### S5 — GPU execution, macOS/MoltenVK (minutes; required from WP2 on)
Same `tests/gpu` suite on macos-14 (Apple-silicon GPU is present on these runners) through
MoltenVK (brew: `molten-vk`, `vulkan-loader`; validation layers as available). Runs
`VK_KHR_portability_subset`-aware: the capability table decides which tests assert vs skip-fail.
Until WP2 lands the capability table, S5 is present but non-required (bring-up).

### S6 — GL↔Vulkan parity (added in WP6; nightly + on-demand label)
The scorecard from the port plan, mechanized: deterministic scenes rendered by the app's legacy GL
path (llvmpipe, `LIBGL_ALWAYS_SOFTWARE=1`) and by the engine (lavapipe) — both Mesa, shared
rasterizer core, near-zero noise — image-diffed within recorded tolerances; sim kernels compared by
SSBO checksum after N fixed-seed steps. Runs in the **app's** CI context (it needs the app build)
as a separate job, nightly and on a `parity` PR label, not per-push.

### S7 — Performance floor (added in WP7; nightly)
Frame-time and allocation budgets on lavapipe with generous software-GPU thresholds; exit 77 = SKIP
(recorded, not green) when the device is `CPU` type and the assert is wall-clock. Real perf
numbers come from the Metal runner and operator hardware runs; CI only catches order-of-magnitude
regressions (PSO churn, per-frame allocations, readback stalls).

## 2. Rules of engagement (the agent's own leash)

1. **Tests precede implementation.** A work package opens with a PR containing its tests (marked
   expected-fail / feature-gated) and any harness they need; the implementation PR flips them to
   passing. A WP is "done" only when `IMPLEMENTATION_PLAN.md`'s exit gate lists every new test
   green in CI — self-reported success does not count.
2. **Red CI stops the line.** No new WP work stacks on a red graphics-ci; fix or revert first.
   Force-push/history rewrite on the integration branch is banned.
3. **Validation messages are defects**, not noise — same for boundary-lint hits. The allowlist
   ritual (S4) is the only exception path and it leaves a paper trail.
4. **Goldens are evidence**: the S4 ritual above; a golden regenerated without a named visual
   cause is treated as a correctness bug.
5. **One WP, one integration branch, small PRs** onto `graphics/main` (integration branch);
   `graphics/main` merges to the default branch only at WP boundaries with its exit gate green,
   keeping the app releasable at all times (parallel-port requirement).
6. **The app's CI stays authoritative for the app**: ci-linux/win/mac must stay green on every
   merge to the default branch; the engine never breaks the GL path (S0's raw-handle lint plus the
   existing gate battery guard this).

## 3. Failure-diagnosis map

| Symptom | First look |
|---|---|
| S0 boundary hit | The include graph you just created — fix the dependency direction, don't widen the allowlist |
| S1 compile fail on one shader | The manifest row (stage/entry) then the GLSL — dialect drift from the GL tree is expected during conversion waves |
| S2 consume-test fail, engine build green | A public header leaking an internal include, or an install rule missing a file |
| S3 green, S4 red | Real GPU semantics: read the validation message first — it names the pass and resource |
| S4 green, S5 red | Portability subset / MoltenVK: check the capability table row before blaming the code |
| Golden drift, both backends | Intentional visual change → ritual; unintentional → bisect the pass |
| Golden drift, one backend | Coordinate-convention classes (Y, depth, point-coord) from the port plan §3.2 audit |
