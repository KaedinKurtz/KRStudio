// PyScript.cpp -- see PyScript.hpp. krs::py: the embedded CPython interpreter
// (pybind11), the curated `krs` scene module user scripts import, and the
// headless PY gate. MAIN-THREAD ONLY (documented in the header).
//
// INCLUDE ORDER MATTERS: pybind11 (hence Python.h) must be lexed BEFORE any
// Qt header defines the `slots` macro -- Python.h has struct members named
// `slots` and the classic Qt/CPython macro collision corrupts them otherwise.
#ifdef KR_WITH_PYTHON
#  include <pybind11/embed.h>
#  include <pybind11/stl.h>
#endif

#include "PyScript.hpp"

#include "components.hpp"   // TransformComponent, TagComponent, RenderableMeshComponent, BRepFace(Component), Vertex
#include "Scene.hpp"        // gate registry host (same pattern as the other gates)
#include "Measure.hpp"      // krs::measure::measureFeature -- the cited source of face area/radius exactness
#include "GateOutcome.hpp"  // krs::gate::skip() -- honest tri-state SKIP for the bench

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>       // GetModuleFileNameA (exe-relative python home probe)
#endif

// This TU never declares Qt signal/slot members; drop Qt's `slots` macro so it
// cannot collide with pybind11/CPython identifiers in code below the includes.
#ifdef slots
#  undef slots
#endif

namespace krs::py {

// ===========================================================================
// Run-scoped state. The registry pointer is only non-null INSIDE runScript
// (main-thread-only contract; no locking -- see PyScript.hpp caveats).
// ===========================================================================
namespace {

entt::registry* g_reg = nullptr;
std::vector<std::map<std::string, double>> g_emitted;

#ifdef KR_WITH_PYTHON
bool        g_inited = false;
bool        g_initTried = false;
std::string g_initFail = "interpreter not initialized yet";
#endif

} // namespace

#ifdef KR_WITH_PYTHON

namespace pyb = pybind11;

// ===========================================================================
// Interpreter home resolution. CPython FATAL-ABORTS the whole process when it
// initializes without a locatable stdlib, so we verify os.py exists at a
// candidate home BEFORE booting and refuse init (available()==false) when no
// candidate validates. Order:
//   1) KRS_PYTHONHOME env var (explicit user override)
//   2) <exe dir>/python      (deployed layout: ship vcpkg tools/python3 there)
//   3) KRS_PYTHON_STDLIB     (compile-time vcpkg stdlib path -- dev machine)
// ===========================================================================
namespace {

bool homeHasStdlib(const std::filesystem::path& home) {
    std::error_code ec;
    return std::filesystem::exists(home / "Lib" / "os.py", ec);   // Windows layout (case-insensitive FS)
}

std::string resolvePythonHome(std::string& tried) {
    if (const char* env = std::getenv("KRS_PYTHONHOME")) {
        tried += std::string("KRS_PYTHONHOME=") + env + "; ";
        if (homeHasStdlib(env)) return env;
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        const std::filesystem::path cand = std::filesystem::path(exePath).parent_path() / "python";
        tried += cand.string() + "; ";
        if (homeHasStdlib(cand)) return cand.string();
    }
#endif
#ifdef KRS_PYTHON_STDLIB
    {
        const std::filesystem::path stdlib = KRS_PYTHON_STDLIB;   // .../tools/python3/Lib (forward slashes)
        tried += stdlib.string() + "; ";
        std::error_code ec;
        if (std::filesystem::exists(stdlib / "os.py", ec))
            return stdlib.parent_path().string();                 // home = .../tools/python3
    }
#endif
    return {};
}

// Lazy singleton boot. The scoped_interpreter is deliberately LEAKED (never
// finalized): CPython teardown interleaved with Qt/static destruction is a
// known deadlock family -- documented in PyScript.hpp. Signal handlers are NOT
// installed (init_signal_handlers=false) so embedding never steals SIGINT.
bool ensureInterpreter() {
    if (g_inited) return true;
    if (g_initTried) return false;             // remember the failure, don't re-abort
    g_initTried = true;

    std::string tried;
    const std::string home = resolvePythonHome(tried);
    if (home.empty()) {
        g_initFail = "no CPython stdlib found; refused init (CPython would fatal-abort). Tried: " + tried;
        return false;
    }
#ifdef _WIN32
    _putenv_s("PYTHONHOME", home.c_str());
#else
    setenv("PYTHONHOME", home.c_str(), 1);
#endif
    try {
        static pyb::scoped_interpreter* s_interp =
            new pyb::scoped_interpreter(/*init_signal_handlers=*/false);   // deliberate leak, see above
        (void)s_interp;
        g_inited = true;
        g_initFail.clear();
    } catch (const std::exception& e) {
        g_initFail = std::string("interpreter init threw: ") + e.what();
    }
    return g_inited;
}

// World transform used consistently by transform()/faces() -- the SAME matrix
// krs::measure applies (TransformComponent::getTransform(), identity when absent).
glm::mat4 worldMatrixOf(entt::registry& reg, entt::entity e) {
    if (const auto* xf = reg.try_get<TransformComponent>(e)) return xf->getTransform();
    return glm::mat4(1.0f);
}

const char* faceTypeName(int t) {
    switch (t) {
    case 0: return "plane";
    case 1: return "cylinder";
    case 2: return "cone";
    case 3: return "sphere";
    default: return "other";
    }
}

entt::entity toEntity(std::uint64_t id) {
    return static_cast<entt::entity>(static_cast<entt::id_type>(id));
}

} // namespace

#endif // KR_WITH_PYTHON

// ===========================================================================
// public: drainEmitted -- rows accumulated by krs.emit_candidate during the
// LAST run (flat str->double maps; schema in PyScript.hpp). Destructive read.
// ===========================================================================
std::vector<std::map<std::string, double>> drainEmitted() {
    std::vector<std::map<std::string, double>> out = std::move(g_emitted);
    g_emitted.clear();
    return out;
}

#ifndef KR_WITH_PYTHON

// ===========================================================================
// COMPILED-OUT STUBS (KR_WITH_PYTHON off / python3+pybind11 not found).
// Soft-fail everything; the gate reports an honest SKIP.
// ===========================================================================
bool available() { return false; }

PyResult runScript(entt::registry&, const std::string&, const std::string&) {
    PyResult r;
    r.error = "krs::py compiled out (KR_WITH_PYTHON off at configure: python3/pybind11 not found)";
    return r;
}

bool runPyGate() {
    std::printf("[py] ================ PY GATE (krs::py embedded python) ================\n");
    std::printf("[py] SKIP  krs::py compiled out (KR_WITH_PYTHON off: python3/pybind11 not found at configure)\n");
    std::fflush(stdout);
    krs::gate::skip();
    return true;
}

#else // KR_WITH_PYTHON ------------------------------------------------------

bool available() { return ensureInterpreter(); }

// ===========================================================================
// runScript -- one isolated execution (fresh globals dict; per-run stdout/
// stderr StringIO capture; exceptions -> PyResult.error with traceback).
// ===========================================================================
PyResult runScript(entt::registry& reg, const std::string& source, const std::string& entryFn) {
    PyResult r;
    if (!ensureInterpreter()) {
        r.error = QString::fromStdString(g_initFail);
        return r;
    }

    g_reg = &reg;              // run-scoped: the krs module reads the scene through this
    g_emitted.clear();         // candidates are per-run

    pyb::gil_scoped_acquire gil;   // no-op re-entry on the main thread; defensive elsewhere
    try {
        pyb::module_ sys = pyb::module_::import("sys");
        pyb::module_ io  = pyb::module_::import("io");
        pyb::object oldOut = sys.attr("stdout");
        pyb::object oldErr = sys.attr("stderr");
        pyb::object outBuf = io.attr("StringIO")();
        pyb::object errBuf = io.attr("StringIO")();
        sys.attr("stdout") = outBuf;
        sys.attr("stderr") = errBuf;

        // FRESH __main__-style globals per run: scripts cannot leak names into
        // each other (sys.modules is still process-wide -- header caveat).
        pyb::dict globals;
        globals["__builtins__"] = pyb::module_::import("builtins");
        globals["__name__"]     = pyb::str("__main__");

        try {
            pyb::exec(source, globals, globals);
            if (!entryFn.empty()) {
                const pyb::str fnName(entryFn);
                if (!globals.contains(fnName))
                    throw std::runtime_error("entry function '" + entryFn + "' is not defined by the script");
                globals[fnName]();                    // no-arg entry point, by contract
            }
            r.ok = true;
        } catch (pyb::error_already_set& e) {
            r.ok = false;
            r.error = QString::fromUtf8(e.what());    // type + message + traceback text
        } catch (const std::exception& e) {
            r.ok = false;
            r.error = QString::fromUtf8(e.what());
        }

        // ALWAYS restore the real streams and collect the capture.
        sys.attr("stdout") = oldOut;
        sys.attr("stderr") = oldErr;
        r.stdoutText = QString::fromStdString(pyb::cast<std::string>(outBuf.attr("getvalue")()));
        const std::string errTxt = pyb::cast<std::string>(errBuf.attr("getvalue")());
        if (!errTxt.empty())
            r.stdoutText += QString::fromUtf8("\n[stderr]\n") + QString::fromStdString(errTxt);
    } catch (pyb::error_already_set& e) {
        r.ok = false;
        r.error = QString::fromUtf8(e.what());        // capture plumbing itself failed
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = QString::fromUtf8(e.what());
    }

    g_reg = nullptr;
    return r;
}

#endif // KR_WITH_PYTHON

} // namespace krs::py

#ifdef KR_WITH_PYTHON

// ===========================================================================
// THE BOUND `krs` MODULE -- curated scene surface v1 (PyScript.hpp documents
// every function). File scope by pybind11 requirement; reads the run-scoped
// registry, so calling it outside runScript raises RuntimeError instead of
// dereferencing null.
// ===========================================================================
namespace {

entt::registry& requireRegistry() {
    if (!krs::py::g_reg)
        throw std::runtime_error("krs: no scene bound (functions are only valid during krs::py::runScript)");
    return *krs::py::g_reg;
}

} // namespace

PYBIND11_EMBEDDED_MODULE(krs, m) {
    namespace pyb = pybind11;
    m.doc() = "KRStudio curated scene API v1 (main-thread only; valid during runScript). "
              "Lengths in metres, world space.";

    m.def("bodies", []() {
        entt::registry& reg = requireRegistry();
        std::vector<std::uint64_t> out;
        for (auto e : reg.view<BRepFaceComponent>())
            out.push_back(static_cast<std::uint64_t>(entt::to_integral(e)));
        return out;
    }, "Entity ids of every body that carries a B-Rep (BRepFaceComponent).");

    m.def("body_name", [](std::uint64_t id) {
        entt::registry& reg = requireRegistry();
        const entt::entity e = krs::py::toEntity(id);
        if (!reg.valid(e)) throw pyb::value_error("body_name: invalid entity id");
        if (const auto* tag = reg.try_get<TagComponent>(e)) return tag->tag;
        return std::string();
    }, "TagComponent tag of the entity ('' when untagged).");

    m.def("faces", [](std::uint64_t id) {
        entt::registry& reg = requireRegistry();
        const entt::entity e = krs::py::toEntity(id);
        if (!reg.valid(e)) throw pyb::value_error("faces: invalid entity id");
        pyb::list out;
        const auto* bc = reg.try_get<BRepFaceComponent>(e);
        if (!bc) return out;                                   // no B-Rep -> honest empty list
        const glm::mat4 M = krs::py::worldMatrixOf(reg, e);
        const glm::mat3 L(M);                                  // linear part: directions
        const glm::mat3 N = glm::transpose(glm::inverse(L));   // normal transform (non-uniform-scale safe)
        for (int i = 0; i < int(bc->faces.size()); ++i) {
            const BRepFace& f = bc->faces[std::size_t(i)];
            // Exact area / world radius come from krs::measure::measureFeature
            // (Measure.hpp) -- the SAME math the Measure tool displays, honest
            // 0 for faces no triangle references.
            const krs::measure::Measurement meas = krs::measure::measureFeature(reg, e, i);
            const glm::vec3 wNrm = [&] {
                const glm::vec3 n = N * f.normal;
                const float len = glm::length(n);
                return (len > 1e-12f) ? n / len : glm::vec3(0.0f, 0.0f, 1.0f);
            }();
            const glm::vec3 wAxis = [&] {
                const glm::vec3 a = L * f.axisDir;
                const float len = glm::length(a);
                return (len > 1e-12f) ? a / len : glm::vec3(0.0f, 0.0f, 1.0f);
            }();
            const glm::vec3 wPos = glm::vec3(M * glm::vec4(f.axisPos, 1.0f));
            pyb::dict d;
            d["id"]     = i;
            d["key"]    = f.faceKey;                           // stable topological id (0 = unset)
            d["type"]   = krs::py::faceTypeName(f.type);
            d["normal"] = pyb::make_tuple(wNrm.x, wNrm.y, wNrm.z);
            d["axis"]   = pyb::make_tuple(wAxis.x, wAxis.y, wAxis.z);
            d["pos"]    = pyb::make_tuple(wPos.x, wPos.y, wPos.z);
            d["radius"] = meas.diameterM * 0.5;                // world mean-scale radius (0 for planes)
            d["area"]   = meas.areaM2;                         // world m^2 (0 when no triangles)
            out.append(std::move(d));
        }
        return out;
    }, "Per-face dicts (id/key/type/normal/axis/pos/radius/area), WORLD space, SI metres.");

    m.def("face_role", [](std::uint64_t id, std::uint64_t /*faceKey*/) {
        entt::registry& reg = requireRegistry();
        const entt::entity e = krs::py::toEntity(id);
        if (!reg.valid(e)) throw pyb::value_error("face_role: invalid entity id");
        // RESERVED: no FaceRoleComponent header exists in the tree yet (it is
        // being developed in parallel). Returns 0 ("no role") for every face
        // so scripts written today keep working when roles land.
        return 0;
    }, "Semantic role of a face by faceKey. Currently always 0 (FaceRoleComponent not landed yet).");

    m.def("transform", [](std::uint64_t id) {
        entt::registry& reg = requireRegistry();
        const entt::entity e = krs::py::toEntity(id);
        if (!reg.valid(e)) throw pyb::value_error("transform: invalid entity id");
        const glm::mat4 M = krs::py::worldMatrixOf(reg, e);
        pyb::list rows;
        for (int r = 0; r < 4; ++r) {                          // ROW-major rows (glm stores columns)
            pyb::list row;
            for (int c = 0; c < 4; ++c) row.append(double(M[c][r]));
            rows.append(std::move(row));
        }
        return rows;
    }, "World transform as 4 ROW-major rows of 4 floats.");

    m.def("emit_candidate", [](const pyb::dict& d) {
        std::map<std::string, double> row;
        for (auto item : d) {
            const std::string k = pyb::cast<std::string>(pyb::str(item.first));
            double v = 0.0;
            try {
                v = pyb::cast<double>(pyb::reinterpret_borrow<pyb::object>(item.second));
            } catch (const pyb::cast_error&) {
                throw pyb::value_error("emit_candidate: value for key '" + k +
                                       "' is not numeric. Schema: FLAT dict of str -> number "
                                       "(convention: pos_x/pos_y/pos_z + 'score').");
            }
            row[k] = v;
        }
        krs::py::g_emitted.push_back(std::move(row));
    }, "Accumulate one flat str->number result row; drained C++-side via krs::py::drainEmitted().");
}

// ===========================================================================
// PY gate (env KRS_PY_SELFTEST, wired by the main loop) -- headless, [py]
// rows, ALL PASS / FAILURES PRESENT summary, honest SKIP when unavailable.
// ===========================================================================
namespace krs::py {

namespace {

struct PSuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[py]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};

// The synthetic scene: ONE B-Rep box body -- a unit cube [0,1]^3, identity
// transform, 6 planar faces with stable keys 101..106 (105=-Z, 106=+Z), two
// triangles per face so every face has an EXACT 1 m^2 world area.
entt::entity buildBoxEntity(entt::registry& reg) {
    const auto e = reg.create();
    reg.emplace<TagComponent>(e, "py_box");
    reg.emplace<TransformComponent>(e);   // identity
    auto& bc = reg.emplace<BRepFaceComponent>(e);
    struct FaceSpec { glm::vec3 n; glm::vec3 c; };
    const FaceSpec fs[6] = {
        { {-1, 0, 0}, {0.0f, 0.5f, 0.5f} },   // 0: -X  key 101
        { { 1, 0, 0}, {1.0f, 0.5f, 0.5f} },   // 1: +X  key 102
        { { 0,-1, 0}, {0.5f, 0.0f, 0.5f} },   // 2: -Y  key 103
        { { 0, 1, 0}, {0.5f, 1.0f, 0.5f} },   // 3: +Y  key 104
        { { 0, 0,-1}, {0.5f, 0.5f, 0.0f} },   // 4: -Z  key 105
        { { 0, 0, 1}, {0.5f, 0.5f, 1.0f} },   // 5: +Z  key 106
    };
    for (int i = 0; i < 6; ++i) {
        BRepFace f;
        f.type = 0;
        f.normal  = fs[i].n;
        f.axisPos = fs[i].c;
        f.faceKey = std::uint64_t(101 + i);
        bc.faces.push_back(f);
    }
    auto& mesh = reg.emplace<RenderableMeshComponent>(e);
    const glm::vec3 P[8] = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},    // z=0 quad
        {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},    // z=1 quad
    };
    for (const auto& p : P) { Vertex v; v.position = p; mesh.vertices.push_back(v); }
    const int quads[6][4] = {
        {0, 3, 7, 4},   // -X
        {1, 2, 6, 5},   // +X
        {0, 1, 5, 4},   // -Y
        {3, 2, 6, 7},   // +Y
        {0, 1, 2, 3},   // -Z
        {4, 5, 6, 7},   // +Z
    };
    for (int f = 0; f < 6; ++f) {
        const auto* q = quads[f];
        mesh.indices.insert(mesh.indices.end(),
            { unsigned(q[0]), unsigned(q[1]), unsigned(q[2]),
              unsigned(q[0]), unsigned(q[2]), unsigned(q[3]) });
        mesh.triFace.push_back(f);
        mesh.triFace.push_back(f);
    }
    return e;
}

} // namespace

bool runPyGate() {
    std::printf("[py] ================ PY GATE (krs::py embedded python) ================\n");
    if (!available()) {
        std::printf("[py] SKIP  interpreter unavailable on this machine: %s\n", g_initFail.c_str());
        std::fflush(stdout);
        krs::gate::skip();
        return true;
    }
    PSuite S;
    Scene scene;
    auto& reg = scene.getRegistry();
    buildBoxEntity(reg);

    // ---- (1) boot + arithmetic ------------------------------------------------
    {
        const PyResult r = runScript(reg, "import sys\nprint(sys.version.split()[0])\nassert 2 + 2 == 4\n");
        std::printf("[py] boot: ok=%d python=%s\n", int(r.ok), qPrintable(r.stdoutText.trimmed()));
        S.check("INTERP-BOOT       interpreter boots and runs a script", r.ok);
        S.check("ARITH             2 + 2 == 4 asserts clean", r.ok && r.error.isEmpty());
    }

    // ---- (2) stdout capture ---------------------------------------------------
    {
        const PyResult r = runScript(reg, "print(7)\n");
        std::printf("[py] stdout: ok=%d captured='%s' (want '7')\n", int(r.ok), qPrintable(r.stdoutText.trimmed()));
        S.check("STDOUT-CAPTURE    print(7) lands in PyResult.stdoutText as '7'",
                r.ok && r.stdoutText.trimmed() == "7");
    }

    // ---- (3) exception -> error text with the type name -----------------------
    {
        const PyResult r = runScript(reg, "1 / 0\n");
        std::printf("[py] exc: ok=%d error-head='%s'\n", int(r.ok),
                    qPrintable(r.error.left(120).replace('\n', " | ")));
        S.check("EXC-TO-ERROR      1/0 -> ok=false, 'ZeroDivisionError' in error text",
                !r.ok && r.error.contains("ZeroDivisionError"));
    }

    // ---- (4) per-run state ISOLATION ------------------------------------------
    {
        const PyResult r1 = runScript(reg, "x = 1\n");
        const PyResult r2 = runScript(reg, "print(x)\n");
        std::printf("[py] isolation: run1 ok=%d; run2 ok=%d error-head='%s'\n",
                    int(r1.ok), int(r2.ok), qPrintable(r2.error.left(80).replace('\n', " | ")));
        S.check("STATE-ISOLATION   run1 sets x; run2 reading x raises NameError",
                r1.ok && !r2.ok && r2.error.contains("NameError"));
    }

    // ---- (5) entry-function dispatch (+ missing-entry NEG-CTRL) ----------------
    {
        const char* src = "def strategy():\n    print('ENTRY 9')\n";
        const PyResult r = runScript(reg, src, "strategy");
        const PyResult neg = runScript(reg, src, "no_such_fn");
        std::printf("[py] entryFn: ok=%d out='%s'; missing-fn ok=%d\n",
                    int(r.ok), qPrintable(r.stdoutText.trimmed()), int(neg.ok));
        S.check("ENTRY-FN          runScript(src, 'strategy') calls it (stdout 'ENTRY 9')",
                r.ok && r.stdoutText.contains("ENTRY 9"));
        S.check("NEG-CTRL          missing entry fn -> ok=false with its name in the error",
                !neg.ok && neg.error.contains("no_such_fn"));
    }

    // ---- (6) the krs module over the synthetic B-Rep box ----------------------
    {
        const char* src = R"PY(
import krs
bs = krs.bodies()
assert len(bs) == 1, f'bodies: {bs}'
e = bs[0]
assert krs.body_name(e) == 'py_box', krs.body_name(e)
fs = krs.faces(e)
assert len(fs) == 6, len(fs)
assert all(f['type'] == 'plane' for f in fs)
top = [f for f in fs if f['normal'][2] > 0.9]
assert len(top) == 1, top
f = top[0]
assert f['key'] == 106, f['key']
assert abs(f['area'] - 1.0) < 1e-6, f['area']
assert abs(f['pos'][2] - 1.0) < 1e-6, f['pos']
assert krs.face_role(e, f['key']) == 0
M = krs.transform(e)
assert len(M) == 4 and all(len(row) == 4 for row in M), M
assert abs(M[0][0] - 1.0) < 1e-9 and abs(M[3][3] - 1.0) < 1e-9, M
krs.emit_candidate({'pos_x': f['pos'][0], 'pos_y': f['pos'][1], 'pos_z': f['pos'][2], 'score': 0.75})
print('SCENE-OK')
)PY";
        const PyResult r = runScript(reg, src);
        if (!r.ok)
            std::printf("[py] krs-module FAILURE detail: %s\n", qPrintable(r.error));
        std::printf("[py] krs-module: ok=%d out='%s'\n", int(r.ok), qPrintable(r.stdoutText.trimmed()));
        S.check("KRS-MODULE        box scene: bodies/name/6 faces/+Z normal/key/area/transform",
                r.ok && r.stdoutText.contains("SCENE-OK"));

        const auto rows = drainEmitted();
        const bool oneRow = (rows.size() == 1);
        const bool fieldsOk = oneRow
            && rows[0].count("pos_x") && rows[0].count("pos_y")
            && rows[0].count("pos_z") && rows[0].count("score")
            && std::abs(rows[0].at("score") - 0.75) < 1e-12
            && std::abs(rows[0].at("pos_x") - 0.5) < 1e-6
            && std::abs(rows[0].at("pos_y") - 0.5) < 1e-6
            && std::abs(rows[0].at("pos_z") - 1.0) < 1e-6;
        if (oneRow)
            std::printf("[py] candidate: pos=(%.3f, %.3f, %.3f) score=%.3f\n",
                        rows[0].at("pos_x"), rows[0].at("pos_y"), rows[0].at("pos_z"), rows[0].at("score"));
        S.check("EMIT-DRAIN        emit_candidate dict -> drainEmitted: 1 row, right fields/values",
                fieldsOk);
        S.check("DRAIN-CLEARS      second drainEmitted() returns empty", drainEmitted().empty());
    }

    // ---- (7) emit_candidate schema NEG-CTRL ------------------------------------
    {
        const PyResult r = runScript(reg, "import krs\nkrs.emit_candidate({'name': 'grip'})\n");
        S.check("NEG-CTRL          non-numeric candidate value -> ValueError, run fails",
                !r.ok && r.error.contains("ValueError"));
    }

    // ---- (8) numpy availability -- INFO only, NEVER a pass/fail check ----------
    {
        const PyResult r = runScript(reg, "import numpy\nprint(numpy.__version__)\n");
        if (r.ok)
            std::printf("[py] INFO  numpy available: %s\n", qPrintable(r.stdoutText.trimmed()));
        else
            std::printf("[py] INFO  numpy absent in this interpreter (pip install into its site-packages to use it)\n");
    }

    std::printf("[py] %d/%d checks\n", S.pass, S.total);
    const bool pass = (S.pass == S.total);
    std::printf("[py] %s\n", pass ? "ALL PASS" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::py

#endif // KR_WITH_PYTHON
