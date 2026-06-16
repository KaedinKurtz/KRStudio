// RobotGraph.cpp -- see RobotGraph.hpp. The default boot graph, spawned through the REAL QtNodes model so
// the canvas nodes ARE the nodes driving the robot.

#include "RobotGraph.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"   // portIndexByName
#include "Node.hpp"
#include "Scene.hpp"

#include <QtNodes/DataFlowGraphModel>

namespace krs::nodes {

BootGraphHandles spawnDefaultRobotGraph(QtNodes::DataFlowGraphModel& model, Scene* scene,
                                        int jointIndex, double freqHz, double ampRad)
{
    BootGraphHandles h;
    const QtNodes::NodeId tId = model.addNode("time_source");
    const QtNodes::NodeId sId = model.addNode("gen_sine");
    const QtNodes::NodeId dId = model.addNode("physics_articulation_drive");
    auto* tDel = model.delegateModel<NodeDelegate>(tId);
    auto* sDel = model.delegateModel<NodeDelegate>(sId);
    auto* dDel = model.delegateModel<NodeDelegate>(dId);
    if (!tDel || !sDel || !dDel) return h;
    Node* tN = tDel->backendNode(); Node* sN = sDel->backendNode(); Node* dN = dDel->backendNode();
    if (!tN || !sN || !dN) return h;

    if (scene) { tN->setScene(scene); sN->setScene(scene); dN->setScene(scene); }
    sN->setParam<double>("freq", freqHz); sN->setParam<double>("amp", ampRad);
    sN->setParam<double>("phase", 0.0);   sN->setParam<double>("offset", 0.0);
    dN->setPortLiteral<int>("Joint", jointIndex);

    const int toi = portIndexByName(tN, Port::Direction::Output, "Time");
    const int sii = portIndexByName(sN, Port::Direction::Input,  "t");
    const int soi = portIndexByName(sN, Port::Direction::Output, "Out");
    const int dai = portIndexByName(dN, Port::Direction::Input,  "Angle");
    if (toi < 0 || sii < 0 || soi < 0 || dai < 0) return h;

    const QtNodes::ConnectionId tsConn{ tId, QtNodes::PortIndex(toi), sId, QtNodes::PortIndex(sii) };
    const QtNodes::ConnectionId saConn{ sId, QtNodes::PortIndex(soi), dId, QtNodes::PortIndex(dai) };
    if (model.connectionPossible(tsConn)) model.addConnection(tsConn);
    if (model.connectionPossible(saConn)) model.addConnection(saConn);

    h.timeId = tId; h.sineId = sId; h.driveId = dId; h.ok = true;
    return h;
}

void tickRobotGraph(QtNodes::DataFlowGraphModel& model)
{
    for (QtNodes::NodeId id : model.allNodeIds()) {
        auto* d = model.delegateModel<NodeDelegate>(id);
        if (d && d->backendNode() && d->backendNode()->getId() == "time_source")
            d->recomputeAndPropagate();
    }
}

} // namespace krs::nodes
