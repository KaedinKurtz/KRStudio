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

} // namespace krs::nodes
