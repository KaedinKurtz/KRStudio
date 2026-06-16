#pragma once
// NodeEditorGate.hpp -- node-editor front-end gate declarations.
#include <memory>
#include <QtNodes/Definitions>   // QtNodes::NodeId
#include "Node.hpp"   // Port::Direction

class QString;
namespace QtNodes { class DataFlowGraphModel; class AbstractGraphModel; }

namespace krs::nodes {

// The catalog-drop instancing path (shared by DroppableGraphicsView::dropEvent and GATE DRAGDROP): add a
// node of `typeId` to `model` at the given scene position. Returns InvalidNodeId for an unknown/empty type.
QtNodes::NodeId instanceDroppedNode(QtNodes::AbstractGraphModel& model, const QString& typeId, double sceneX, double sceneY);

// GATE DRAGDROP (KRS_DRAGDROP_SELFTEST): a catalog drop (mime type-id -> instance) creates the CORRECT node
// type with its ports + mounted widget at the drop position; an unknown/empty type creates nothing.
bool runDragDropGate();

// a REAL QtNodes DataFlowGraphModel over the node registry -- the gates use the editor's actual
// addNode/connectionPossible/addConnection paths through it (shared by TYPE/TIME/CONNECT).
std::shared_ptr<QtNodes::DataFlowGraphModel> makeNodeGraphModel();
int portIndexByName(Node* n, Port::Direction dir, const char* name);


// GATE INPUT-BIND (KRS_INPUTBIND_SELFTEST): every input-bearing node type mounts a per-input widget in its
// body, and driving that widget changes the node's evaluated output (N-of-M coverage). Needs QApplication.
bool runInputBindGate();

// GATE TYPE (KRS_TYPE_SELFTEST): the unified port type ids let compatible ports connect and keep
// incompatible ones unconnectable (mirrors QtNodes connectionPossible). Needs QApplication.
bool runTypeGate();

// GATE TIME (KRS_TIME_SELFTEST): a sine driven by the LIVE time source oscillates over wall-clock;
// disconnected from the time source it is constant.
bool runTimeGate();

// GATE FRAME (KRS_FRAME_SELFTEST): every registered node type, instanced through the REAL QtNodes
// DataFlowGraphModel, exposes >=1 port (so QtNodes draws its full frame with draggable ports). Reports
// N of M. NEG-CTRL: a 0-port delegate is caught by the same predicate (not silently counted).
bool runFrameGate();

// GATE VIS (KRS_VIS_SELFTEST): a visualizer node's DISPLAYED value matches the value it is told to show
// to <tol and respects N-digits / decimals; a disconnected input does not update the display.
bool runVisGate();

// GATE DEMO-GRAPH (KRS_DEMOGRAPH_SELFTEST): the default boot graph (time->sine->drive) IS the robot's
// driver -- editing the canvas sine's amp dial changes the live robot's motion; a bypass would not.
bool runDemoGraphGate();

// GATE OWNERSHIP (KRS_OWNERSHIP_SELFTEST): the node command is the sole joint driver (FK <1e-4); with no
// graph the joint stays at rest (no second writer); rapid DOF-switching never crashes/corrupts.
bool runOwnershipGate();

// GATE PID (KRS_PID_SELFTEST): the PID node closes a 1st-order plant onto a step, matching an independent
// reference PID+plant simulation to <tol and removing steady-state error; a P-only loop retains an offset.
bool runPidGate();

// GATE FILTER (KRS_FILTER_SELFTEST): the scalar Kalman / low-pass / moving-average nodes each match an
// independent reference (and a meaningful negative control); no unverified filter ships.
bool runFilterGate();

// GATE THREAD (KRS_THREAD_SELFTEST): hammering UI edits through the async NodeEditQueue keeps the physics
// tick rate near baseline; the old synchronous (inline-recompute) path drops it.
bool runThreadGate();

// Recon/profile harness (KRS_NODEPROF_SELFTEST): dumps real NodeGraphicsObject geometry (framed vs
// frameless) and the eval cascade cost with/without the scene. No asserts -- it informs the fixes.
bool runNodeProfileDiag();

// GATE FRAME-GFX (KRS_FRAMEGFX_SELFTEST): one layer up from GATE FRAME -- every registered type builds a
// NodeGraphicsObject with a caption, a frame body, and boundary ports (graphics level, not just data model).
bool runFrameGfxGate();

// GATE PERF (KRS_PERF_SELFTEST): quiet eval cost is bounded + scales linearly with N (5/15/30); the old
// per-eval scene-repaint cascade is much costlier and scales worse (reproduces the ~45ms blowup).
bool runPerfGate();

// GATE RATE (KRS_RATE_SELFTEST): the eval rate is configurable (30Hz..20kHz) and changes eval frequency,
// while UI repaint stays capped (~60Hz) independent of it; a single-rate knob would repaint at kHz.
bool runRateGate();

// GATE HOVER-INTEGRITY (KRS_HOVER_SELFTEST): for every node type, the frame background (no
// WA_TranslucentBackground) + the exec-mode control's visibility survive a synthetic hoverEnter AND
// hoverLeave; NEG-CTRL: a WA_TranslucentBackground container + a hidden combo are caught.
bool runHoverIntegrityGate();

// GATE ZOOM-VISIBLE (KRS_ZOOM_SELFTEST): every node is NoCache + has no offscreen drop-shadow effect (no
// device pixmap to overflow at zoom), and the largest nodes' frame title renders at both terminal-zoom
// bounds (0.3x/2x); NEG-CTRL: an un-fixed node carries the overflow-prone DeviceCoordinateCache + effect.
bool runZoomVisibilityGate();

} // namespace krs::nodes
