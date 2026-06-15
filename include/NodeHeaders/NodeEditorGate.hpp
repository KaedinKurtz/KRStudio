#pragma once
// NodeEditorGate.hpp -- node-editor front-end gate declarations.
namespace krs::nodes {

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
