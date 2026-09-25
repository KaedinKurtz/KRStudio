# ADR-001: Direct Vulkan core with a Qt-free public API

Date: 2026-09-25 · Status: accepted · Deciders: owner (Kaedin), Claude (primary engineer)

## Context
The renderer port plan (`docs/VULKAN_PORT_PLAN.md`) recommended QRhi (unanimous judge panel).
The owner then added a requirement the panel never scored: the new backend must be a standalone,
precompiled graphics library, reusable from any 3D program, with a hard split from the robotics
app. QRhi makes Qt a permanent dependency of the library and its API carries Qt types.

## Decision
Build the library on Vulkan 1.2 directly (volk + VMA + our own RHI/render-graph), Qt confined to
an optional adapter target (`krsg-qt`). macOS runs through MoltenVK (see ADR-004).

## Consequences
+ Qt-free public API; reusable anywhere; push constants, indirect execution, and integer texture
  formats return (pick pass ports as-is; fluid live-count readback loop dies).
- We own device/memory/sync plumbing (~+40-60 engineer-days vs QRhi) and MoltenVK is in the macOS
  path; mitigations: capability table (WP2), sync-validation-as-failure in CI, null-first tests.
The QRhi plan's inventory, hazards, and red-team corrections remain the evidence base.
