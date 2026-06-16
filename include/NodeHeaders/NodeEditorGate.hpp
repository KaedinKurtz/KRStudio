#pragma once
// NodeEditorGate.hpp -- node-editor front-end gate declarations.
#include <memory>
#include "Node.hpp"   // Port::Direction

namespace QtNodes { class DataFlowGraphModel; }

namespace krs::nodes {

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

} // namespace krs::nodes
