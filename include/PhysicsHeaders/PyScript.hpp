#pragma once
// ===========================================================================
// krs::py -- EMBEDDED PYTHON for user-scriptable robotics algorithms.
//
// Hosts a CPython interpreter (pybind11::embed) inside the app so research
// scripts -- e.g. a caliper-search grasp strategy written from scratch --
// run against a small, CURATED scene API instead of poking engine internals.
// The user chose Python over Lua for its research ecosystem (numpy & friends
// import fine when present in the interpreter's site-packages).
//
// THE BOUND MODULE `krs` (curated surface v1, importable from any script):
//   krs.bodies() -> list[int]          entity ids that carry a B-Rep
//                                      (BRepFaceComponent), i.e. CAD bodies
//   krs.body_name(e) -> str            TagComponent tag ("" when untagged)
//   krs.faces(e) -> list[dict]         one dict per B-Rep face, WORLD space:
//                                        id     : int   face index (the same
//                                                 (entity, faceId) identity the
//                                                 selection/measure stack uses)
//                                        key    : int   BRepFace.faceKey -- the
//                                                 STABLE topological id (0 =
//                                                 unset/synthetic)
//                                        type   : str   'plane'|'cylinder'|
//                                                 'cone'|'sphere'|'other'
//                                        normal : (x,y,z) world plane normal
//                                        axis   : (x,y,z) world axis direction
//                                        pos    : (x,y,z) world anchor point
//                                        radius : float world radius, metres
//                                        area   : float world m^2 -- computed by
//                                                 krs::measure::measureFeature
//                                                 (Measure.hpp), so it carries
//                                                 that module's exactness rules;
//                                                 a face no triangle references
//                                                 honestly reports 0.
//   krs.face_role(e, key) -> int       reserved: returns 0 until a
//                                      FaceRoleComponent lands (no such header
//                                      exists yet); the signature is stable so
//                                      scripts written today keep working
//   krs.transform(e) -> [[float x4]x4] world transform, ROW-major rows
//   krs.emit_candidate(dict) -> None   accumulate one result row (e.g. a grasp
//                                      candidate). SCHEMA: a FLAT dict of
//                                      str -> number (int/float/bool), by
//                                      convention pos_x/pos_y/pos_z plus a
//                                      'score'; non-numeric values raise
//                                      ValueError. Drained C++-side via
//                                      drainEmitted() after the run.
//
// EXECUTION MODEL / HONEST CAVEATS:
//   * MAIN-THREAD ONLY. The registry pointer flows through a run-scoped
//     global; there is no locking. Never call runScript off the GUI thread.
//   * Lazy singleton interpreter, initialized on the first runScript()/
//     available() and NEVER finalized (CPython teardown during static/Qt
//     destruction is a known deadlock family; the OS reclaims at exit --
//     deliberate, documented leak; atexit handlers inside scripts won't run).
//   * Each run executes in a FRESH __main__-style globals dict, so script
//     variables do NOT leak between runs. sys.modules is still process-wide:
//     state stashed inside an imported module DOES persist across runs.
//   * stdout/stderr are redirected to per-run StringIO buffers; stdout lands
//     in PyResult.stdoutText (stderr is appended under a "[stderr]" marker).
//   * Python exceptions never propagate to C++ callers: PyResult.ok=false and
//     PyResult.error carries the exception type, message and traceback text.
//   * NO WALL-CLOCK GUARD: true preemption of CPython is out of scope -- an
//     infinite loop in a script hangs the calling (GUI) thread. Keep scripts
//     terminating; a watchdog/subprocess runner is future work.
//   * Interpreter home: the CPython stdlib is located at init (see PyScript.cpp
//     resolution order: KRS_PYTHONHOME env -> <exe>/python -> the build
//     machine's vcpkg stdlib baked in via KRS_PYTHON_STDLIB). If none of them
//     contains a stdlib, available() stays false -- init is REFUSED rather
//     than letting CPython fatal-abort the process.
//
// Compiled out cleanly when KR_WITH_PYTHON is not defined (CMake option
// KR_WITH_PYTHON=OFF or python3/pybind11 missing): available()==false,
// runScript() fails softly, the gate reports an honest SKIP.
//
// Gate: runPyGate() (env hook KRS_PY_SELFTEST, wired by the main loop) --
// headless, [py] rows, synthetic B-Rep box scene, real NEG-CTRLs.
// ===========================================================================
#include <entt/entt.hpp>
#include <QString>

#include <map>
#include <string>
#include <vector>

namespace krs::py {

struct PyResult {
    bool ok = false;         // script ran to completion (entry fn included)
    QString error;           // exception type + message + traceback ("" when ok)
    QString stdoutText;      // captured sys.stdout ("[stderr]"-marked tail when any)
};

// Compiled in (KR_WITH_PYTHON) AND the interpreter initialized. Triggers the
// lazy init on first call; false when compiled out or the stdlib can't be
// located (see header caveats). Cheap after the first call.
bool available();

// Run `source` in a fresh isolated globals dict. entryFn=="" runs the module
// top-level only; otherwise the top-level runs first (defs), then entryFn()
// is called with no arguments (missing entryFn -> ok=false). MAIN THREAD ONLY.
PyResult runScript(entt::registry& reg, const std::string& source,
                   const std::string& entryFn = "");

// Rows accumulated by krs.emit_candidate(dict) during the LAST runScript, as
// flat key->double maps (schema documented above). Clears the buffer (a second
// call returns empty); runScript also clears it at run start.
std::vector<std::map<std::string, double>> drainEmitted();

// Headless self-test (env KRS_PY_SELFTEST): interpreter boot, arithmetic,
// stdout capture, exception -> error text, per-run state isolation, the bound
// krs module over a synthetic B-Rep box (bodies/faces/plane normal/transform/
// emit_candidate -> drainEmitted round-trip), entry-fn dispatch, and a numpy
// availability INFO line (never a failure). Honest SKIP (krs::gate::skip)
// when !available().
bool runPyGate();

} // namespace krs::py
