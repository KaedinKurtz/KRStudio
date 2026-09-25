#!/usr/bin/env python3
"""Engine/app boundary lint (CI stage S0). See docs/graphics/CI_PIPELINE.md.

Enforced rules:
  E1  engine public API (engine/include/krsg/**) includes nothing but C/C++ std headers
      and other krsg/ headers, and never mentions Vulkan/Qt/GL/GLM symbols.
  E2  engine sources never include Qt/GLM/OpenGL/app headers; Vulkan headers are allowed
      only under engine/src/rhi/vulkan/.  Exception: engine/adapters/qt/** may use Qt.
  E3  layer direction: rhi -> core only; graph -> {core,rhi}; render -> {core,rhi,graph};
      adapters -> anything in the engine; nothing includes adapters.
  A1  app code (src/, include/) never includes engine internals (only <krsg/...>).
  A2  ratchet: the count of GLuint/GLenum tokens in app public headers (include/**) must
      not exceed the recorded baseline (scripts/engine_boundary_baseline.json) — the GL
      leak surface only shrinks.  Regenerate the baseline ONLY in a dedicated commit via
      --update-baseline after removing leaks.

Exit code 0 = clean, 1 = violations (each printed with file:line).
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE_FILE = ROOT / "scripts" / "engine_boundary_baseline.json"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

# Foreign-library include patterns (matched against the include path).
QT_RE = re.compile(r"^Q[A-Z]|^Qt[A-Z/]|^qt[a-z]*/")
GL_RE = re.compile(r"^GL/|^gl(ad|ew|fw)|^qopengl", re.IGNORECASE)
GLM_RE = re.compile(r"^glm[/.]")
VK_RE = re.compile(r"^vulkan/|^vk_|^volk|^vk_mem_alloc")
APP_RE = re.compile(
    r"Headers/|^components\.hpp|^entt|^imgui|^ads/|^QtNodes|^opencv|^urdf"
)

# C/C++ standard headers permitted in the public API (E1).
STD_ALLOW = re.compile(
    r"^(c(assert|ctype|errno|float|limits|locale|math|setjmp|signal|stdarg|stddef|"
    r"stdint|stdio|stdlib|string|time|uchar|wchar|wctype)|algorithm|array|atomic|"
    r"bitset|charconv|chrono|codecvt|complex|condition_variable|deque|exception|"
    r"filesystem|forward_list|fstream|functional|future|initializer_list|iomanip|"
    r"ios|iosfwd|iostream|istream|iterator|limits|list|locale|map|memory|"
    r"memory_resource|mutex|new|numeric|optional|ostream|queue|random|ratio|regex|"
    r"scoped_allocator|set|shared_mutex|sstream|stack|stdexcept|streambuf|string|"
    r"string_view|system_error|thread|tuple|type_traits|typeindex|typeinfo|utility|"
    r"valarray|variant|vector)$"
)

# Symbol-level bans in public headers (E1): backend/toolkit types must not leak.
PUBLIC_SYMBOL_BAN = re.compile(r"\b(Vk[A-Z]\w*|vk[A-Z]\w*|GLuint|GLenum|GLint|QWidget|QWindow|glm::)")

violations = []


def bad(path, lineno, rule, msg):
    violations.append(f"{path.relative_to(ROOT)}:{lineno}: [{rule}] {msg}")


def includes(path):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        bad(path, 0, "IO", str(e))
        return []
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        m = INCLUDE_RE.match(line)
        if m:
            out.append((i, m.group(1)))
    return out, text


def check_engine_public(path):
    incs, text = includes(path)
    for lineno, inc in incs:
        if inc.startswith("krsg/") or STD_ALLOW.match(inc):
            continue
        bad(path, lineno, "E1", f"public API may not include '{inc}'")
    for i, line in enumerate(text.splitlines(), 1):
        stripped = line.split("//")[0]
        m = PUBLIC_SYMBOL_BAN.search(stripped)
        if m:
            bad(path, i, "E1", f"foreign symbol '{m.group(1)}' in public API")


LAYER_ALLOWED_PREFIXES = {
    "src/core": ("krsg/", "core/"),
    "src/rhi": ("krsg/", "core/", "rhi/"),
    "src/null": ("krsg/", "core/", "rhi/", "null/"),
    "src/graph": ("krsg/", "core/", "rhi/", "graph/"),
    "src/render": ("krsg/", "core/", "rhi/", "graph/", "render/"),
    "adapters": ("krsg/", "core/", "rhi/", "graph/", "render/", "adapters/"),
}


def check_engine_source(path):
    rel = path.relative_to(ROOT / "engine").as_posix()
    in_qt_adapter = rel.startswith("adapters/qt/")
    in_vulkan = rel.startswith("src/rhi/vulkan/")
    incs, _ = includes(path)
    for lineno, inc in incs:
        if QT_RE.match(inc) and not in_qt_adapter:
            bad(path, lineno, "E2", f"Qt include '{inc}' outside adapters/qt")
        if GL_RE.match(inc):
            bad(path, lineno, "E2", f"OpenGL include '{inc}' in engine")
        if GLM_RE.match(inc):
            bad(path, lineno, "E2", f"GLM include '{inc}' in engine (math is POD at the seam)")
        if APP_RE.search(inc):
            bad(path, lineno, "E2", f"app include '{inc}' in engine (dependency arrow is app->engine)")
        if VK_RE.match(inc) and not in_vulkan:
            bad(path, lineno, "E2", f"Vulkan include '{inc}' outside src/rhi/vulkan")
        # E3 layer direction for quoted engine-internal includes.
        for layer, allowed in LAYER_ALLOWED_PREFIXES.items():
            if rel.startswith(layer + "/") or rel.startswith(layer.split("/")[-1] + "/"):
                if "/" in inc and not inc.startswith(allowed) and not STD_ALLOW.match(inc) \
                        and not VK_RE.match(inc) and not QT_RE.match(inc):
                    if inc.startswith(("core/", "rhi/", "graph/", "render/", "adapters/")):
                        bad(path, lineno, "E3", f"layer violation: {layer} -> '{inc}'")
                break


def check_app_source(path):
    incs, _ = includes(path)
    for lineno, inc in incs:
        if inc.startswith("engine/") or (inc.startswith(("core/", "rhi/", "graph/")) and "krsg" in inc):
            bad(path, lineno, "A1", f"app includes engine internals '{inc}' (use <krsg/...>)")


def app_gl_leak_count():
    n = 0
    token = re.compile(r"\b(GLuint|GLenum)\b")
    for p in sorted((ROOT / "include").rglob("*.hpp")):
        if "Eigen" in p.parts:
            continue
        try:
            n += len(token.findall(p.read_text(encoding="utf-8", errors="replace")))
        except OSError:
            pass
    return n


def main():
    update = "--update-baseline" in sys.argv

    eng = ROOT / "engine"
    for p in sorted((eng / "include" / "krsg").rglob("*.h")):
        check_engine_public(p)
    for sub in ("src", "adapters", "tests", "tools"):
        d = eng / sub
        if d.is_dir():
            for p in sorted(d.rglob("*")):
                if p.suffix in (".cpp", ".h", ".hpp") and "tests/consume" not in p.as_posix():
                    check_engine_source(p)
    for d in (ROOT / "src", ROOT / "include"):
        if d.is_dir():
            for p in sorted(d.rglob("*")):
                if p.suffix in (".cpp", ".hpp", ".h") and "Eigen" not in p.parts:
                    check_app_source(p)

    leaks = app_gl_leak_count()
    if update:
        BASELINE_FILE.write_text(json.dumps({"app_header_gl_tokens": leaks}, indent=2) + "\n")
        print(f"baseline updated: app_header_gl_tokens = {leaks}")
    else:
        try:
            baseline = json.loads(BASELINE_FILE.read_text())["app_header_gl_tokens"]
        except (OSError, KeyError, ValueError):
            print("ERROR: missing/corrupt baseline; run --update-baseline in a dedicated commit")
            return 1
        if leaks > baseline:
            violations.append(
                f"[A2] GL leak ratchet: app headers now contain {leaks} GLuint/GLenum tokens "
                f"(baseline {baseline}) — the GL surface may only shrink"
            )
        elif leaks < baseline:
            print(f"note: GL leak count improved {baseline} -> {leaks}; "
                  f"tighten with --update-baseline in a dedicated commit")

    if violations:
        print(f"BOUNDARY VIOLATIONS ({len(violations)}):")
        for v in violations:
            print("  " + v)
        return 1
    print(f"boundary clean (app GL-token count {leaks} <= baseline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
