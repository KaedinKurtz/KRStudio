#pragma once
// RobotGraph.hpp -- the DEFAULT boot node graph (node-ecosystem sprint). spawnDefaultRobotGraph builds the
// REAL editor graph  time_source -> gen_sine -> physics_articulation_drive  that drives the live robot at
// boot: the SAME graph a user could build, and the SINGLE path to joint motion (the hardcoded demo sweep is
// gone). Used by MainWindow (the app boot) AND by the DEMO-GRAPH / OWNERSHIP / V.6 gates, so the boot path
// and the gate path are literally one helper.
#include <QtNodes/Definitions>

namespace QtNodes { class DataFlowGraphModel; }
class Scene;

namespace krs::nodes {

struct BootGraphHandles {
    QtNodes::NodeId timeId  = QtNodes::InvalidNodeId;
    QtNodes::NodeId sineId  = QtNodes::InvalidNodeId;
    QtNodes::NodeId driveId = QtNodes::InvalidNodeId;
    bool ok = false;
};

// Spawn time_source -> gen_sine -> physics_articulation_drive(Joint=jointIndex) into `model`, wire the real
// editor edges, set a gentle within-joint-limit sine (ampRad @ freqHz), inject the live `scene`, and return
// the node handles. The same nodes appear on the canvas AND drive the robot -- not a decorative copy.
BootGraphHandles spawnDefaultRobotGraph(QtNodes::DataFlowGraphModel& model, Scene* scene,
                                        int jointIndex = 0, double freqHz = 0.2, double ampRad = 0.5);

// Re-evaluate every time_source node in `model` (-> propagate through the wired edges). This is the 30Hz
// graph tick the app runs; calling it then SimulationController::tick() drives the robot from the canvas.
void tickRobotGraph(QtNodes::DataFlowGraphModel& model);

} // namespace krs::nodes
