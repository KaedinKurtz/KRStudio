#pragma once
// ===========================================================================
// SNAP SESSION (mate-selector P4) -- the live state behind the Onshape-style
// inference UI. The viewport's hover path fills it (candidates for the hovered
// feature, nearest-to-cursor active pick, Shift face-lock, Ctrl axis-reveal,
// F/R frame corrections); the SnapOverlayPass DRAWS it (glyph dots + the
// triad/disk on the active candidate); consumers (mate-connector authoring,
// joint definition, constraint picks) read `result` after a click commit.
// ctx singleton, mirroring SelectionState.
// ===========================================================================
#include <entt/entt.hpp>
#include <vector>

#include "Snap.hpp"   // krs::snap::SnapCandidate / SnapKind

namespace krs::snapui {

struct SnapSessionState {
    // ---- input state (viewport writes) ----
    bool armed = false;                    // dots/triad visible; armed WITH feature picking
    entt::entity hoverEntity = entt::null; // feature the candidates came from
    int hoverFaceId = -1, hoverEdgeId = -1, hoverVertexId = -1;
    bool shiftLock = false;                // Shift held: candidate set frozen to lockedEntity/Face
    entt::entity lockedEntity = entt::null;
    int lockedFaceId = -1;
    bool ctrlReveal = false;               // Ctrl held: hovered ENTITY's cylinder-axis points added

    // ---- inference (viewport computes, overlay draws) ----
    std::vector<krs::snap::SnapCandidate> candidates;
    int activeIdx = -1;                    // nearest to cursor within the wake radius (-1 = none)
    int rotSteps = 0;                      // R key: 90-degree X-about-Z steps applied to the frame
    bool flipped = false;                  // F key: Z flipped

    // ---- commit (click writes; consumers read + clear) ----
    bool hasResult = false;
    krs::snap::SnapCandidate result;       // corrections already applied

    // a consumer LATCH: when true, the next committed result becomes a persistent
    // MateConnector on its body (the Constraints panel's Place-Connector button arms this).
    bool connectorAuthoring = false;

    // viewport layer toggle: every placed MateConnector renders as the orange/white
    // quadrant glyph (UR orange, UL white, LL orange, LR white) at its body-local frame.
    bool showConnectors = true;
};

inline SnapSessionState& snapSession(entt::registry& reg) {
    auto* s = reg.ctx().find<SnapSessionState>();
    return s ? *s : reg.ctx().emplace<SnapSessionState>();
}

} // namespace krs::snapui
