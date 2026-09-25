# ADR-002: Monorepo now, repo extraction on API stability

Date: 2026-09-25 · Status: accepted · Deciders: owner (Kaedin), Claude

## Context
Two codebases (engine library, robotics app) with heavy API churn ahead during the port.

## Decision
`engine/` lives in this repo as a standalone CMake project; the boundary is machine-enforced by
`scripts/check_engine_boundary.py` (CI stage S0) from day one. Extraction to its own repository
(git filter-repo, history preserved) happens when the public API survives two consecutive work
packages without a breaking change; the app then consumes a versioned binary via a vcpkg overlay
port.

## Consequences
+ Atomic cross-boundary commits while the API churns; one CI to keep green.
- Boundary discipline rests on the lint, not repo walls — which is why S0 failures are hard errors.
