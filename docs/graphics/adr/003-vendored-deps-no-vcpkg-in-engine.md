# ADR-003: Engine dependencies vendored via pinned FetchContent, not vcpkg

Date: 2026-09-25 · Status: accepted · Deciders: Claude (primary engineer)

## Context
The app's vcpkg dependency set takes hours to build cold in CI. The engine needs a
minutes-per-push verification loop to make test-first agent iteration viable.

## Decision
The engine's third-party set is tiny and vendored by pinned-SHA FetchContent: volk, VMA,
Vulkan-Headers. Host shader tools (glslangValidator, spirv-tools) are CI/system packages, never
linked. Every dependency addition is a new ADR.

## Consequences
+ graphics-ci runs in minutes on every push, independent of the app's 4-hour vcpkg builds.
+ The library builds anywhere (any 3D program, any machine) with plain CMake.
- Vendored pins need a documented update ritual (owned by WP12's extraction checklist).
