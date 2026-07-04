#pragma once
// ===========================================================================
// SKILL -- the Process & Skills layer's core contract (krs::skill).
//
// A SKILL is a named, parameterized, shareable behavior: its BODY realizes the
// motion/effect, its exposed inputs are typed PARAMETERS (ExposedPort role +
// default on the .knode), its POSTCONDITION decides success, and it actuates
// ONLY through the robot-keyed command bus (ArticulationCommandComponent) --
// re-asserted every control tick, so releasing/deleting a skill releases its
// DOFs to manual control per the bus's per-pass contract.
//
// HYBRID BODIES (the chosen architecture): a body is any Node. A pure-subgraph
// skill instantiates a SubgraphNode from a .knode (fully shareable via
// .knodepack); a motion skill may instead wrap the proven C++ cinematics tower
// (planCartesianMotion -> playTrajectory) behind the same contract. Either way
// the task layer sees ONE thing: a BT Action leaf (krs::policy::Action::Fn)
// that is Running until the postcondition holds -- wrap it in policy::Timeout
// for an honest Failure instead of an infinite wait.
// ===========================================================================
#include "BehaviorTree.hpp"    // krs::policy::Action::Fn / Status
#include <Eigen/Core>
#include <functional>
#include <map>
#include <memory>
#include <string>

class Node;
class Scene;
namespace krs::robot { struct LiveRobot; }

namespace krs::skill {

// A parameter value bound at invocation. P1 covers the two kinds the worked skills
// need -- scalars and whole joint configs; further kinds ride the kdoc union later.
struct ParamValue {
    enum class Kind { Number, Config };
    Kind kind = Kind::Number;
    double number = 0.0;
    Eigen::VectorXd config;
    static ParamValue num(double v) { ParamValue p; p.kind = Kind::Number; p.number = v; return p; }
    static ParamValue cfg(Eigen::VectorXd q) { ParamValue p; p.kind = Kind::Config; p.config = std::move(q); return p; }
};
using ParamMap = std::map<std::string, ParamValue>;

// Build the BT Action leaf realizing a skill:
//  - `body` is ticked (process()) EVERY control tick, so its command-bus entries re-assert per pass;
//  - `params` bind onto the body's exposed INPUT ports by name (Number -> literal, Config -> packet)
//    on the first tick;
//  - `post` is evaluated after each body tick: true => Success, else Running (Timeout => Failure).
// The returned Fn owns its own bound-state, so one functor is re-enterable across ticks.
krs::policy::Action::Fn makeSkillLeaf(std::shared_ptr<Node> body, Scene* scene,
                                      ParamMap params, std::function<bool()> post);

// Canonical P1 postcondition: the live robot's q is within `tol` of `target` on every DOF.
std::function<bool()> qNear(const krs::robot::LiveRobot* lr, const Eigen::VectorXd& target, double tol = 1e-4);

// Headless gate (KRS_SKILL_SELFTEST): a "move_to_config" skill -- a .knode whose interior is a
// physics_config_drive, exposed input "target" tagged role=targetConfig with a manifest -- is
// instantiated, its leaf ticked under the real per-pass bus lifecycle (clearForEvalPass -> tick ->
// drainCommandBusIntoRobots) against a live demo robot: q reaches the target and the leaf returns
// Success; after success nothing re-asserts (the DOFs release). The manifest + param role/default
// round-trip the .knode file, and a .knodepack now carries the description. NEG-CTRL: a postcondition
// that never holds hits policy::Timeout and returns Failure (not an infinite Running).
bool runSkillGate();

} // namespace krs::skill
