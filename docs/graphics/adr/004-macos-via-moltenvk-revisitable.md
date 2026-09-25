# ADR-004: macOS via MoltenVK, with a native-Metal escape valve

Date: 2026-09-25 · Status: accepted · Deciders: owner (Kaedin), Claude

## Context
Direct Vulkan (ADR-001) reaches macOS only through MoltenVK. The red-team pass showed the feared
hazards are smaller than assumed (zero float atomics; the 2 live geometry shaders are rewritten
under ANY backend) but tessellation emulation, portability subset, and translation overhead are
real.

## Decision
Ship macOS on MoltenVK. WP2 builds the capability/portability table and measures the known risk
spots (tessellation pilot, image atomics, timeline semaphore emulation) on CI's Apple-silicon
runners before any dependent work. The public API stays backend-clean so a native-Metal L0
backend remains a backend swap, not an API break, if measurements demand it.

## Consequences
+ One shader tree (SPIR-V), one backend to maintain now; macOS decision is evidence-gated.
- If MoltenVK disappoints, the Metal backend is new work — bounded by the sealed L0 contract.
